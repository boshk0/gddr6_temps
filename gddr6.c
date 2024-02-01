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

int debug_flag = 0;
#define HOTSPOT_REGISTER_OFFSET 0x0002046c

#define PG_SZ sysconf(_SC_PAGE_SIZE)
#define PRINT_ERROR()                                        \
      do {                                                     \
      fprintf(stderr, "Error at line %d, file %s (%d) [%s]\n", \
      __LINE__, __FILE__, errno, strerror(errno)); exit(1);    \
      } while(0)


// device struct
struct device
{
    uint32_t bar0;
    uint8_t bus, dev, func;
    uint32_t offset;
    uint16_t dev_id;
    const char *vram;
    const char *arch;
    const char *name;
    char pciBusId[NVML_DEVICE_PCI_BUS_ID_BUFFER_SIZE]; // Add this line
};


// variables
int fd;
void *map_base;
struct device devices[32];


// device table
struct device dev_table[] =
{
    { .offset = 0x0000E2A8, .dev_id = 0x26B1, .vram = "GDDR6",  .arch = "AD102", .name =  "RTX A6000" },
    { .offset = 0x0000E2A8, .dev_id = 0x2230, .vram = "GDDR6",  .arch = "AD102", .name =  "RTX A6000" },
    { .offset = 0x0000E2A8, .dev_id = 0x2231, .vram = "GDDR6",  .arch = "GA102", .name =  "RTX A5000" },
    { .offset = 0x0000E2A8, .dev_id = 0x2684, .vram = "GDDR6X", .arch = "AD102", .name =  "RTX 4090" },
    { .offset = 0x0000E2A8, .dev_id = 0x2704, .vram = "GDDR6X", .arch = "AD103", .name =  "RTX 4080" },
    { .offset = 0x0000E2A8, .dev_id = 0x2782, .vram = "GDDR6X", .arch = "AD104", .name =  "RTX 4070 Ti" },
    { .offset = 0x0000E2A8, .dev_id = 0x2786, .vram = "GDDR6X", .arch = "AD104", .name =  "RTX 4070" },
    { .offset = 0x0000E2A8, .dev_id = 0x2204, .vram = "GDDR6X", .arch = "GA102", .name =  "RTX 3090" },
    { .offset = 0x0000E2A8, .dev_id = 0x2208, .vram = "GDDR6X", .arch = "GA102", .name =  "RTX 3080 Ti" },
    { .offset = 0x0000E2A8, .dev_id = 0x2206, .vram = "GDDR6X", .arch = "GA102", .name =  "RTX 3080" },
    { .offset = 0x0000E2A8, .dev_id = 0x2216, .vram = "GDDR6X", .arch = "GA102", .name =  "RTX 3080 LHR" },
    { .offset = 0x0000EE50, .dev_id = 0x2484, .vram = "GDDR6",  .arch = "GA104", .name =  "RTX 3070" },
    { .offset = 0x0000EE50, .dev_id = 0x2488, .vram = "GDDR6",  .arch = "GA104", .name =  "RTX 3070 LHR" },
    { .offset = 0x0000E2A8, .dev_id = 0x2531, .vram = "GDDR6",  .arch = "GA106", .name =  "RTX A2000" },
    { .offset = 0x0000E2A8, .dev_id = 0x2571, .vram = "GDDR6",  .arch = "GA106", .name =  "RTX A2000" },
    { .offset = 0x0000E2A8, .dev_id = 0x2232, .vram = "GDDR6",  .arch = "GA102", .name =  "RTX A4500" },
    { .offset = 0x0000E2A8, .dev_id = 0x27b8, .vram = "GDDR6",  .arch = "AD104", .name =  "L4" },
    { .offset = 0x0000E2A8, .dev_id = 0x26b9, .vram = "GDDR6",  .arch = "AD102", .name =  "L40S" },
};

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


// Define human-readable names for throttle reasons
//static const char* throttle_reason_to_string(unsigned long long reason) {
//    switch (reason) {
//        case nvmlClocksThrottleReasonApplicationsClocksSetting: return "ApplicationsClocksSetting";
//        case nvmlClocksThrottleReasonDisplayClockSetting: return "DisplayClockSetting";
//        case nvmlClocksThrottleReasonGpuIdle: return "GpuIdle";
//        case nvmlClocksThrottleReasonHwPowerBrakeSlowdown: return "HwPowerBrakeSlowdown";
//        case nvmlClocksThrottleReasonHwSlowdown: return "HwSlowdown";
//        case nvmlClocksThrottleReasonHwThermalSlowdown: return "HwThermalSlowdown";
//        case nvmlClocksThrottleReasonNone: return "None";
//        case nvmlClocksThrottleReasonSwPowerCap: return "SwPowerCap";
//        case nvmlClocksThrottleReasonSwThermalSlowdown: return "SwThermalSlowdown";
//        case nvmlClocksThrottleReasonSyncBoost: return "SyncBoost";
//        default: return "Unknown";
//    }
//}

// prototypes
void cleanup(int signal);
void cleanup_sig_handler(void);
int pci_detect_dev(void);


// cleanup
void cleanup(int signal)
{
    if (signal == SIGHUP || signal == SIGINT || signal == SIGTERM)
    {
        if (map_base != (void *) -1)
            munmap(map_base, PG_SZ);
        if (fd != -1)
            close(fd);
        exit(0);
    }
}


// cleanup signal handler
void cleanup_sig_handler(void)
{
    struct sigaction sa;
    sa.sa_handler = &cleanup;
    sa.sa_flags = 0;
    sigfillset(&sa.sa_mask);

    if (sigaction(SIGINT, &sa, NULL) < 0)
        perror("Cannot handle SIGINT");

    if (sigaction(SIGHUP, &sa, NULL) < 0)
        perror("Cannot handle SIGHUP");

    if (sigaction(SIGTERM, &sa, NULL) < 0)
        perror("Cannot handle SIGTERM");
}


// pci device detection
int pci_detect_dev(void)
{
    struct pci_access *pacc = NULL;
    struct pci_dev *pci_dev = NULL;
    int num_devs = 0;
    ssize_t dev_table_size = (sizeof(dev_table)/sizeof(struct device));

    pacc = pci_alloc();
    pci_init(pacc);
    pci_scan_bus(pacc);

for (pci_dev = pacc->devices; pci_dev; pci_dev = pci_dev->next)
{
    pci_fill_info(pci_dev, PCI_FILL_IDENT | PCI_FILL_BASES | PCI_FILL_CLASS);

    // Print out each NVIDIA device's ID for debugging
   if (debug_flag) {
	    if (pci_dev->vendor_id == 0x10DE) { // 0x10DE is NVIDIA's vendor ID
       		printf("Found NVIDIA Device - Vendor ID: %04x, Device ID: %04x\n", pci_dev->vendor_id, pci_dev->device_id);
    		}
	}

    for (uint32_t i = 0; i < dev_table_size; i++)
    {
        if (pci_dev->device_id == dev_table[i].dev_id)
        {
            devices[num_devs] = dev_table[i];
            devices[num_devs].bar0 = (pci_dev->base_addr[0] & 0xFFFFFFFF);
            devices[num_devs].bus = pci_dev->bus;
            devices[num_devs].dev = pci_dev->dev;
            devices[num_devs].func = pci_dev->func;

            // Format and store the PCI Bus ID
            sprintf(devices[num_devs].pciBusId, "%04x:%02x:%02x.0", pci_dev->domain, pci_dev->bus, pci_dev->dev);

            num_devs++;
        }
    }
}


    pci_cleanup(pacc);
    return num_devs;
}


int main(int argc, char **argv)
{
    // Parse command-line arguments
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-d") == 0) {
            debug_flag = 1;
        }
    }



    (void) argc;
    (void) argv;
    void *virt_addr;
    uint32_t temp;
    uint32_t phys_addr;
    uint32_t read_result;
    uint32_t base_offset;

    nvmlReturn_t result;
    result = nvmlInit();
    if (NVML_SUCCESS != result)      
    {
          fprintf(stderr, "Failed to initialize NVML: %s\n", nvmlErrorString(result));
          return 1;
    }

    int num_devs;
    char *MEM = "\x2f\x64\x65\x76\x2f\x6d\x65\x6d";

    num_devs = pci_detect_dev();

    if (num_devs == 0)
    {
        printf("No compatible GPU found\n.");
        exit(-1);
    }

    for (int i = 0; i < num_devs; i++) {
        struct device *device = &devices[i];
   	if (debug_flag) {
        	printf("Device: %s %s (%s / 0x%04x) pci=%x:%x:%x\n", device->name, device->vram,
            	device->arch, device->dev_id, device->bus, device->dev, device->func);
    	}
    }

    if ((fd = open(MEM, O_RDWR | O_SYNC)) == -1)
    {
        printf("Can't read memory. If you are root, enable kernel parameter iomem=relaxed\n");
        PRINT_ERROR();
    }

    cleanup_sig_handler();


while (1)
{
        // Open the metrics file in write mode to overwrite the existing content
        FILE *metrics_file = fopen("./metrics.txt", "w");
        if (metrics_file == NULL) {
            perror("Error opening metrics file");
            return 1;
        }

    // Write the header
    fprintf(metrics_file, "# HELP DCGM_FI_DEV_VRAM_TEMP VRAM temperature (in C).\n");
    fprintf(metrics_file, "# TYPE DCGM_FI_DEV_VRAM_TEMP gauge\n");


    for (int i = 0; i < num_devs; i++) {
        nvmlDevice_t nvml_device;
        char uuid[NVML_DEVICE_UUID_BUFFER_SIZE];
        struct device *device = &devices[i];

        result = nvmlDeviceGetHandleByPciBusId_v2(devices[i].pciBusId, &nvml_device);
        if (NVML_SUCCESS != result)
        {
            fprintf(stderr, "Failed to get handle for device %d: %s\n", i, nvmlErrorString(result));
            continue;
        }

        result = nvmlDeviceGetUUID(nvml_device, uuid, NVML_DEVICE_UUID_BUFFER_SIZE);
        if (NVML_SUCCESS != result)
        {
            fprintf(stderr, "Failed to get UUID for device %d: %s\n", i, nvmlErrorString(result));
            continue;
        }

        phys_addr = (device->bar0 + device->offset);
        base_offset = phys_addr & ~(PG_SZ-1);
        map_base = mmap(0, PG_SZ, PROT_READ | PROT_WRITE, MAP_SHARED, fd, base_offset);

        if(map_base == (void *) -1)
        {
            if (fd != -1)
                close(fd);
            printf("Can't read memory. If you are root, enable kernel parameter iomem=relaxed\n");
            PRINT_ERROR();
        }
        virt_addr = (uint8_t *) map_base + (phys_addr - base_offset);
        read_result = *((uint32_t *) virt_addr);
        temp = ((read_result & 0x00000fff) / 0x20);
        printf("DCGM_FI_DEV_VRAM_TEMP{gpu=\"%d\", UUID=\"%s\"} %u\n", i, uuid, temp);
        // Write to file instead of printing to stdout
        fprintf(metrics_file, "DCGM_FI_DEV_VRAM_TEMP{gpu=\"%d\", UUID=\"%s\"} %u\n", i, uuid, temp);
    }

   fprintf(metrics_file, "# HELP DCGM_FI_DEV_HOT_SPOT_TEMP Hot Spot temperature (in C).\n");
   fprintf(metrics_file, "# TYPE DCGM_FI_DEV_HOT_SPOT_TEMP gauge\n");
    for (int i = 0; i < num_devs; i++) {
        nvmlDevice_t nvml_device;
        char uuid[NVML_DEVICE_UUID_BUFFER_SIZE];
        struct device *device = &devices[i];

        result = nvmlDeviceGetHandleByPciBusId_v2(devices[i].pciBusId, &nvml_device);
        if (NVML_SUCCESS != result)
        {
            fprintf(stderr, "Failed to get handle for device %d: %s\n", i, nvmlErrorString(result));
            continue;
        }

        result = nvmlDeviceGetUUID(nvml_device, uuid, NVML_DEVICE_UUID_BUFFER_SIZE);
        if (NVML_SUCCESS != result)
        {
            fprintf(stderr, "Failed to get UUID for device %d: %s\n", i, nvmlErrorString(result));
            continue;
        }

        phys_addr = (device->bar0 + device->offset);
        base_offset = phys_addr & ~(PG_SZ-1);
        map_base = mmap(0, PG_SZ, PROT_READ | PROT_WRITE, MAP_SHARED, fd, base_offset);

        if(map_base == (void *) -1)
        {
            if (fd != -1)
                close(fd);
            printf("Can't read memory. If you are root, enable kernel parameter iomem=relaxed\n");
            PRINT_ERROR();
        }
	// Code to read and write the hot spot temperature for the current device
    	uint32_t hotSpotRegAddr = device->bar0 + HOTSPOT_REGISTER_OFFSET;
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
        	// Write hot spot temperature metric for this device
		fprintf(stderr,"DCGM_FI_DEV_HOT_SPOT_TEMP{gpu=\"%d\", UUID=\"%s\"} %u\n", i, uuid, hotSpotTemp); 
        	fprintf(metrics_file, "DCGM_FI_DEV_HOT_SPOT_TEMP{gpu=\"%d\", UUID=\"%s\"} %u\n", i, uuid, hotSpotTemp);
    	} else {
        // Optionally handle invalid readings
    }
   }// end of for loop for 


    // Write the header for clocks throttle reasons
//    fprintf(metrics_file, "# TYPE DCGM_FI_DEV_CLOCKS_THROTTLE_REASONS gauge\n");
//    fprintf(metrics_file, "# HELP DCGM_FI_DEV_CLOCKS_THROTTLE_REASONS The current throttle reasons for GPU clocks. Each bit in the value represents a specific throttle reason as defined: None (0), GpuIdle (1), ApplicationsClocksSetting (2), SwPowerCap (4), HwSlowdown (8), SyncBoost (16), SwThermalSlowdown (32), HwThermalSlowdown (64), HwPowerBrakeSlowdown (128), DisplayClockSetting (256).\n");
    // Write the header for individual clocks throttle reasons
    fprintf(metrics_file, "# HELP DCGM_FI_DEV_CLOCKS_THROTTLE_REASON Individual throttle reason for GPU clocks.\n");
    fprintf(metrics_file, "# TYPE DCGM_FI_DEV_CLOCKS_THROTTLE_REASON gauge\n");


    for (int i = 0; i < num_devs; i++) {
        nvmlDevice_t nvml_device;
        char uuid[NVML_DEVICE_UUID_BUFFER_SIZE];
        unsigned long long clocksThrottleReasons;

        result = nvmlDeviceGetHandleByPciBusId_v2(devices[i].pciBusId, &nvml_device);
        if (NVML_SUCCESS != result)
        {
            fprintf(stderr, "Failed to get handle for device %d: %s\n", i, nvmlErrorString(result));
            continue;
        }

        result = nvmlDeviceGetUUID(nvml_device, uuid, NVML_DEVICE_UUID_BUFFER_SIZE);
        if (NVML_SUCCESS != result)
        {
            fprintf(stderr, "Failed to get UUID for device %d: %s\n", i, nvmlErrorString(result));
            continue;
        }
        result = nvmlDeviceGetCurrentClocksThrottleReasons(nvml_device, &clocksThrottleReasons);
        if (NVML_SUCCESS != result) {
            fprintf(stderr, "Failed to get clocks throttle reasons for device %d: %s\n", i, nvmlErrorString(result));
            continue;
        }
	// Inside the loop where you print out DCGM_FI_DEV_CLOCKS_THROTTLE_REASONS
	for (size_t j = 0; j < sizeof(throttleReasons)/sizeof(throttleReasons[0]); j++) {
	    if (clocksThrottleReasons & throttleReasons[j].reasonBit) {
	        fprintf(stderr, "DCGM_FI_DEV_CLOCKS_THROTTLE_REASON{reason=\"%s\",gpu=\"%d\", UUID=\"%s\"} 1\n",
	                throttleReasons[j].reasonString, i, uuid);
                fprintf(metrics_file, "DCGM_FI_DEV_CLOCKS_THROTTLE_REASON{reason=\"%s\",gpu=\"%d\", UUID=\"%s\"} 1\n",
                        throttleReasons[j].reasonString, i, uuid);

	    } else {
        	fprintf(stderr, "DCGM_FI_DEV_CLOCKS_THROTTLE_REASON{reason=\"%s\",gpu=\"%d\", UUID=\"%s\"} 0\n",
                	throttleReasons[j].reasonString, i, uuid);
                fprintf(metrics_file, "DCGM_FI_DEV_CLOCKS_THROTTLE_REASON{reason=\"%s\",gpu=\"%d\", UUID=\"%s\"} 0\n",
                        throttleReasons[j].reasonString, i, uuid);

	    }
	}

        // Write throttle reasons to metrics file
//        printf( "DCGM_FI_DEV_CLOCKS_THROTTLE_REASONS{gpu=\"%d\", UUID=\"%s\", reason=\"%s\"} %llu\n",
//                i, uuid, throttle_reason_to_string(clocksThrottleReasons), clocksThrottleReasons);
//        fprintf(metrics_file, "DCGM_FI_DEV_CLOCKS_THROTTLE_REASONS{gpu=\"%d\", UUID=\"%s\", reason=\"%s\"} %llu\n",
//                i, uuid, throttle_reason_to_string(clocksThrottleReasons), clocksThrottleReasons);
     }


    fflush(stdout);
    // Make sure to flush the stream to write to the file immediately
    fflush(metrics_file);
    fclose(metrics_file);

    sleep(5);
}

close(fd);
nvmlShutdown();
return 0;

}
