#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <pci/pci.h>
#include <signal.h>
#include <nvml.h>


#define VRAM_REGISTER_OFFSET 0x0000E2A8
#define HOTSPOT_REGISTER_OFFSET 0x0002046c
#define PG_SZ sysconf(_SC_PAGE_SIZE)
#define MEM_PATH "/dev/mem"
// Define the maximum number of devices
#define MAX_DEVICES 32


int fd = -1;
void* map_base = MAP_FAILED; // Use MAP_FAILED instead of (void*)-1 for mapping
FILE *metrics_file = NULL;


typedef struct {
    unsigned long long reasonBit;
    const char* reasonString;
} ThrottleReasonInfo;


// Define throttle reasons
const ThrottleReasonInfo throttleReasons[] = {
    {nvmlClocksThrottleReasonGpuIdle, "GpuIdle"},
    {nvmlClocksThrottleReasonApplicationsClocksSetting, "ApplicationsClocksSetting"},
    {nvmlClocksThrottleReasonSwPowerCap, "SwPowerCap"},
    {nvmlClocksThrottleReasonHwSlowdown, "HwSlowdown"},
    {nvmlClocksThrottleReasonSyncBoost, "SyncBoost"},
    {nvmlClocksThrottleReasonSwThermalSlowdown, "SwThermalSlowdown"},
    {nvmlClocksThrottleReasonHwThermalSlowdown, "HwThermalSlowdown"},
    {nvmlClocksThrottleReasonHwPowerBrakeSlowdown, "HwPowerBrakeSlowdown"},
    {nvmlClocksThrottleReasonDisplayClockSetting, "DisplayClockSetting"},
    // Add more throttle reasons as necessary
};

typedef struct {
    char uuid[NVML_DEVICE_UUID_BUFFER_SIZE];
    unsigned int vram_temp;
    unsigned int hotspot_temp;
    unsigned int clock_throttle_reasons;
} DeviceData;

DeviceData devices[MAX_DEVICES];

void printPciInfo(const nvmlPciInfo_t *pciInfo);
void printPciDev(const struct pci_dev *dev);
void cleanup(int signal);
void cleanup_sig_handler(void);
void createMetricFile(int device_count);



// Cleanup function to release resources
void cleanup(int signal) {
    (void)signal; // Suppress unused parameter warning
    if (map_base != MAP_FAILED) {
        munmap(map_base, PG_SZ);
        map_base = MAP_FAILED; // Reset to indicate it's unmapped
    }
    if (fd != -1) {
        close(fd);
        fd = -1; // Reset to indicate it's closed
    }
    if (metrics_file != NULL) {
        fclose(metrics_file); // Close the metrics file if it's open
        metrics_file = NULL; // Reset to indicate it's closed
    }
    _exit(0); // Use _exit to immediately terminate the program
}

// Setup cleanup signal handler
void cleanup_sig_handler(void) {
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa)); // Clear structure
    sa.sa_handler = &cleanup; // No need for & with function pointer
    sa.sa_flags = SA_RESTART; // Optionally add SA_RESTART to restart syscalls if possible
    sigemptyset(&sa.sa_mask); // Using sigemptyset for minimal masking

    if (sigaction(SIGINT, &sa, NULL) < 0)
        perror("Cannot handle SIGINT");

    if (sigaction(SIGHUP, &sa, NULL) < 0)
        perror("Cannot handle SIGHUP");

    if (sigaction(SIGTERM, &sa, NULL) < 0)
        perror("Cannot handle SIGTERM");
}

// Function to create the metric.txt file
void createMetricFile(int device_count){
    metrics_file = fopen("metrics.txt", "w");
    if (!metrics_file) {
        fprintf(stderr, "Failed to open metric.txt for writing\n");
        return;
    }

    // Iterate through devices and write metrics to the file
    for (int i = 0; i < device_count; i++) {
        // Write VRAM temperature
        fprintf(metrics_file, "# HELP DCGM_FI_DEV_VRAM_TEMP VRAM temperature (in C).\n");
        fprintf(metrics_file, "# TYPE DCGM_FI_DEV_VRAM_TEMP gauge\n");
        fprintf(metrics_file, "DCGM_FI_DEV_VRAM_TEMP{gpu=\"%d\", UUID=\"%s\"} %u\n", i, devices[i].uuid, devices[i].vram_temp);

        // Write Hot Spot temperature
        fprintf(metrics_file, "# HELP DCGM_FI_DEV_HOT_SPOT_TEMP Hot Spot temperature (in C).\n");
        fprintf(metrics_file, "# TYPE DCGM_FI_DEV_HOT_SPOT_TEMP gauge\n");
        fprintf(metrics_file, "DCGM_FI_DEV_HOT_SPOT_TEMP{gpu=\"%d\", UUID=\"%s\"} %u\n", i, devices[i].uuid, devices[i].hotspot_temp);

        // Write Clocks Throttle Reason
        fprintf(metrics_file, "# HELP DCGM_FI_DEV_CLOCKS_THROTTLE_REASON Individual throttle reason for GPU clocks.\n");
        fprintf(metrics_file, "# TYPE DCGM_FI_DEV_CLOCKS_THROTTLE_REASON gauge\n");

        // Iterate through throttle reasons and write them to the file
        for (size_t j = 0; j < sizeof(throttleReasons) / sizeof(throttleReasons[0]); j++) {
            int isThrottling = (devices[i].clock_throttle_reasons & throttleReasons[j].reasonBit) ? 1 : 0;
            fprintf(metrics_file, "DCGM_FI_DEV_CLOCKS_THROTTLE_REASON{reason=\"%s\", gpu=\"%d\", UUID=\"%s\"} %d\n",
                throttleReasons[j].reasonString, i, devices[i].uuid, isThrottling);
        }
    }

    fflush(metrics_file);
    fclose(metrics_file);
    metrics_file=NULL;
}

int main(void) {
    // Open the metrics file in write mode to overwrite the existing content
    cleanup_sig_handler();

    while(1){
        // Clear the terminal screen using ANSI escape codes
        // Initialize PCI library
        struct pci_access *pacc = pci_alloc();
        nvmlReturn_t result = nvmlInit();
        int num_devices = 0;
        if (result != NVML_SUCCESS) {
         fprintf(stderr, "Failed to initialize NVML: %s\n", nvmlErrorString(result));
            return 1;
        }

        unsigned int device_count;
        result = nvmlDeviceGetCount(&device_count);
        if (result != NVML_SUCCESS) {
            fprintf(stderr, "Failed to get device count: %s\n", nvmlErrorString(result));
            nvmlShutdown();
            return 1;
        }

        pci_init(pacc);
        pci_scan_bus(pacc);

        printf("\033[2J\033[H");
        for (unsigned int i = 0; i < device_count; i++) {
        nvmlDevice_t nvml_device;
        unsigned long long clocksThrottleReasons;
        char device_name[NVML_DEVICE_NAME_BUFFER_SIZE];

                    // Store data in the devices array

        result = nvmlDeviceGetHandleByIndex(i, &nvml_device);
        if (result != NVML_SUCCESS) {
            fprintf(stderr, "Failed to get handle for device %u: %s\n", i, nvmlErrorString(result));
            continue;
        } //else fprintf(stderr, "NVML_SUCCESS to get handle for device %u: %s\n", i, nvmlErrorString(result));

        result = nvmlDeviceGetName(nvml_device, device_name, NVML_DEVICE_NAME_BUFFER_SIZE);
        if (result == NVML_SUCCESS) {
            printf("GPU Name: %s ", device_name);
        } else {
            fprintf(stderr, "Failed to get name for device: %s\n", nvmlErrorString(result));
        // Handle error
        }
        nvmlPciInfo_t pciInfo;
        result = nvmlDeviceGetPciInfo(nvml_device, &pciInfo);
        if (result != NVML_SUCCESS) {
            fprintf(stderr, "Failed to get PCI info for device %u: %s\n", i, nvmlErrorString(result));
            continue;
        }

        // Get GPU temperature
        unsigned int temp;
        result = nvmlDeviceGetTemperature(nvml_device, NVML_TEMPERATURE_GPU, &temp);
        if (result == NVML_SUCCESS) {
            printf("GPU %u: Temperature: %u C ", i, temp);
        } else {
            fprintf(stderr, "Failed to get temperature for device %u: %s\n", i, nvmlErrorString(result));
        }

        // Get GPU power usage
        unsigned int power;
        result = nvmlDeviceGetPowerUsage(nvml_device, &power);
        if (result == NVML_SUCCESS) {
            printf("Power Usage: %.2f W ", power / 1000.0); // Power is returned in milliwatts
        } else {
            fprintf(stderr, "Failed to get power usage for device %u: %s\n", i, nvmlErrorString(result));
        }

        for (struct pci_dev *pci_dev = pacc->devices; pci_dev; pci_dev = pci_dev->next) {
            pci_fill_info(pci_dev, PCI_FILL_IDENT | PCI_FILL_BASES | PCI_FILL_CLASS);
            unsigned int combinedDeviceId = (pci_dev->device_id << 16) | pci_dev->vendor_id;


            if (combinedDeviceId == pciInfo.pciDeviceId && 
                (unsigned int)pci_dev->domain == pciInfo.domain &&
                pci_dev->bus == pciInfo.bus &&
                pci_dev->dev == pciInfo.device) {

                //printf("Found matching NVIDIA Device - Vendor ID: %04x, Device ID: %04x, bus: %02x dev: %02x func: %02x  \n", pci_dev->vendor_id, pci_dev->device_id, pci_dev->bus, pci_dev->dev, pci_dev->func );
                //printPciDev(pci_dev);
                //printPciInfo(&pciInfo);
                //printf("PCI Subsystem combinedDeviceId (0x%08x)\n", combinedDeviceId);

                fd = open(MEM_PATH, O_RDWR | O_SYNC);
                if (fd < 0) {
                    perror("Failed to open /dev/mem");
                    continue;
                }
                
                uint32_t phys_addr =  (pci_dev->base_addr[0] & 0xFFFFFFFF) + VRAM_REGISTER_OFFSET;
                uint32_t base_offset = phys_addr & ~(PG_SZ-1);
                map_base = mmap(0, PG_SZ, PROT_READ | PROT_WRITE, MAP_SHARED, fd, base_offset);  
                if (map_base == MAP_FAILED) {
                    perror("Failed to map BAR0 memory");
                    close(fd);
                    continue;
                }
                
                uint32_t *vram_temp_reg = (uint32_t *)((char *)map_base + (phys_addr - base_offset));
                uint32_t vram_temp_value = *vram_temp_reg;
                vram_temp_value = ((vram_temp_value & 0x00000fff) / 0x20);
                printf(" VRAM Temp: %u ", vram_temp_value);
                if (i < MAX_DEVICES) {
                    devices[i].vram_temp = vram_temp_value;
                } else {
                    fprintf(stderr, "Exceeded maximum number of devices\n");
                }

                char uuid[NVML_DEVICE_UUID_BUFFER_SIZE];
                if (result == NVML_SUCCESS) {
	                result = nvmlDeviceGetUUID(nvml_device, uuid, sizeof(uuid));
        	        if (result == NVML_SUCCESS) {
        	            uuid[sizeof(uuid) - 1] = '\0'; // Ensure null termination
        	        } else {
        	            fprintf(stderr, "Failed to get UUID for device %d: %s\n", i, nvmlErrorString(result));
                    }
        	    }

               if (i < MAX_DEVICES) {
                    strncpy(devices[i].uuid, uuid, sizeof(devices[num_devices].uuid));
                } else {
                    fprintf(stderr, "Exceeded maximum number of devices\n");
                }
                
	            // Code to read and write the hot spot temperature for the current device
    	        uint32_t hotSpotRegAddr = (pci_dev->base_addr[0] & 0xFFFFFFFF) + HOTSPOT_REGISTER_OFFSET;
    	        uint32_t hotSpotBaseOffset = hotSpotRegAddr & ~(PG_SZ-1);
    	        void* hotSpotMapBase = mmap(0, PG_SZ, PROT_READ, MAP_SHARED, fd, hotSpotBaseOffset);
    	        if (hotSpotMapBase == MAP_FAILED) {
        	        fprintf(stderr, "Failed to mmap for hot spot temperature\n");
        	        continue; // Skip this device if we cannot map the register space
    	        }

    	        uint32_t hotSpotRegValue = *((uint32_t *)((char *)hotSpotMapBase + (hotSpotRegAddr - hotSpotBaseOffset)));
    	        munmap(hotSpotMapBase, PG_SZ); // Unmap immediately after reading

	            uint32_t hotSpotTemp = (hotSpotRegValue >> 8) & 0xff;
    	        if (hotSpotTemp < 0x7f) {
                    printf("HotSPotTemp: %u\n", hotSpotTemp);
        	        // Write hot spot temperature metric for this device

                    if (i < MAX_DEVICES) {
                        devices[i].hotspot_temp = hotSpotTemp;
                    } else {
                        fprintf(stderr, "Exceeded maximum number of devices\n");
                    }

    	        } 

                result = nvmlDeviceGetCurrentClocksThrottleReasons(nvml_device, &clocksThrottleReasons);
                if (NVML_SUCCESS != result) {
                    fprintf(stderr, "Failed to get clocks throttle reasons for device %d: %s\n", i, nvmlErrorString(result));
                    continue;
                }

                if (i < MAX_DEVICES) {
                        devices[i].clock_throttle_reasons = clocksThrottleReasons;
                } else {
                        fprintf(stderr, "Exceeded maximum number of devices\n");
                }
                // Inside the loop where you print out DCGM_FI_DEV_CLOCKS_THROTTLE_REASONS


                fflush(stdout);
                // Make sure to flush the stream to write to the file immediately
                if (map_base != MAP_FAILED) {
                    munmap(map_base, PG_SZ);
                    map_base = MAP_FAILED; // Reset to indicate it's unmapped
                }
                if (fd != -1) {
                    close(fd);
                    fd = -1; // Reset to indicate it's closed
                }
                break; // Break from the PCI device loop once matched
                
            }
            }
        }
        createMetricFile(device_count);
        pci_cleanup(pacc);
        nvmlShutdown();
        sleep(5);
    }

    return 0;
}

void printPciInfo(const nvmlPciInfo_t *pciInfo){
    printf("PCI Info:\n");
    printf("Legacy Bus ID: %s\n", pciInfo->busIdLegacy);
    printf("Domain: %u\n", pciInfo->domain);
    printf("Bus: %u\n", pciInfo->bus);
    printf("Device: %u\n", pciInfo->device);
    printf("PCI Device ID: %u (0x%08x)\n", pciInfo->pciDeviceId, pciInfo->pciDeviceId);
    printf("PCI Subsystem ID: %u (0x%08x)\n", pciInfo->pciSubSystemId, pciInfo->pciSubSystemId);
    printf("Bus ID: %s\n", pciInfo->busId);
}

void printPciDev(const struct pci_dev *dev) {
    printf("PCI Device Info:\n");
    printf("Domain: %d (16-bit backward compatible: %hu)\n", dev->domain, dev->domain_16);
    printf("Bus: %u, Device: %u, Function: %u\n", dev->bus, dev->dev, dev->func);
    printf("Vendor ID: 0x%04hx, Device ID: 0x%04hx\n", dev->vendor_id, dev->device_id);
    printf("Device Class: 0x%04hx, IRQ: %d\n", dev->device_class, dev->irq);

    for (int i = 0; i < 6; i++) {
        printf("Base Address[%d]: %" PRIxPTR ", Size: %" PRIxPTR ", Flags: %" PRIxPTR "\n",
               i, (uintptr_t)dev->base_addr[i], (uintptr_t)dev->size[i], (uintptr_t)dev->flags[i]);
    }

    printf("ROM Base Address: %" PRIxPTR ", ROM Size: %" PRIxPTR ", ROM Flags: %" PRIxPTR "\n",
           (uintptr_t)dev->rom_base_addr, (uintptr_t)dev->rom_size, (uintptr_t)dev->rom_flags);

    printf("Physical Slot: %s\n", dev->phy_slot ? dev->phy_slot : "N/A");
    printf("Kernel Module Alias: %s\n", dev->module_alias ? dev->module_alias : "N/A");
    printf("BIOS Label: %s\n", dev->label ? dev->label : "N/A");
    printf("NUMA Node: %d\n", dev->numa_node);
}