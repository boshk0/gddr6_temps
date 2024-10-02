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
#include <stdbool.h>
#include <ctype.h>

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
    bool vram_temp;
    bool hotspot_temp;
    bool clocks_throttle_reason;
    bool gpu_aer_total_errors;
    bool gpu_aer_error_state;
    bool sm_clock;
    bool mem_clock;
    bool gpu_temp;
    bool power_usage;
    bool fan_speed;
    bool gpu_util;
    bool mem_util;
    bool fb_free;
    bool fb_used;
    bool nvlink_bandwidth_total;
} MetricsConfig;

typedef struct {
    char uuid[NVML_DEVICE_UUID_BUFFER_SIZE];
    unsigned int vram_temp;
    unsigned int hotspot_temp;
    unsigned int clock_throttle_reasons;
    unsigned int sm_clock;
    unsigned int mem_clock;
    unsigned int gpu_temp;
    unsigned int power_usage;
    unsigned int fan_speed;
    unsigned int gpu_util;
    unsigned int mem_util;
    unsigned long long fb_free;
    unsigned long long fb_used;
    unsigned int nvlink_bandwidth_total;
    char device_name[NVML_DEVICE_NAME_BUFFER_SIZE];
} DeviceData;

DeviceData devices[MAX_DEVICES];

void printPciInfo(const nvmlPciInfo_t *pciInfo);
void printPciDev(const struct pci_dev *dev);
void cleanup(int signal);
void cleanup_sig_handler(void);
void createMetricFile(int device_count, MetricsConfig* metricsConfig);
int getGpuPciBusId(unsigned int index, char *pciBusId, unsigned int length);
unsigned int getTotalAerErrorsForDevice(unsigned int gpuIndex);
unsigned int checkGpuErrorState(unsigned int gpuIndex);
bool initializeNvml(void);
unsigned int countUpgradablePackages(void);
void loadMetricsConfig(MetricsConfig* config);
void printHelpMessage(void);
void printConsoleOutput(MetricsConfig* metricsConfig);

unsigned int countUpgradablePackages(void) {
    FILE *fp;
    unsigned int count = 0;
    char buffer[1024];

    // Attempt to read the package count from /var/log/package-count.txt
    fp = fopen("/var/log/package-count.txt", "r");
    if (fp != NULL) {
        if (fgets(buffer, sizeof(buffer), fp) != NULL) {
            sscanf(buffer, "Upgradable packages: %u", &count);
        }
        fclose(fp);
        return count;
    } else {
        // Fallback to counting using apt list --upgradable
        fp = popen("apt list --upgradable 2>/dev/null", "r");
        if (fp == NULL) {
            fprintf(stderr, "Failed to run command\n");
            return 0;
        }

        // Read the output a line at a time - count each package line
        while (fgets(buffer, sizeof(buffer), fp) != NULL) {
            if (strstr(buffer, "upgradable from")) {
                count++;
            }
        }

        // Close the pipe
        pclose(fp);
        return count;
    }
}

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

// Function to load metrics configuration from metrics.ini
void loadMetricsConfig(MetricsConfig* config) {
    // Initialize all metrics to false
    memset(config, 0, sizeof(MetricsConfig));

    FILE* fp = fopen("metrics.ini", "r");
    if (fp == NULL) {
        // metrics.ini does not exist, create it with default metrics
        fp = fopen("metrics.ini", "w");
        if (fp == NULL) {
            fprintf(stderr, "Failed to create metrics.ini\n");
            return;
        }
        fprintf(fp, "DCGM_FI_DEV_VRAM_TEMP\n");
        fprintf(fp, "DCGM_FI_DEV_HOT_SPOT_TEMP\n");
        fprintf(fp, "DCGM_FI_DEV_CLOCKS_THROTTLE_REASON\n");
        fprintf(fp, "GPU_AER_TOTAL_ERRORS\n");
        fprintf(fp, "GPU_AER_ERROR_STATE\n");
        fclose(fp);
        // Reopen for reading
        fp = fopen("metrics.ini", "r");
        if (fp == NULL) {
            fprintf(stderr, "Failed to open metrics.ini for reading\n");
            return;
        }
    }

    char line[256];
    while (fgets(line, sizeof(line), fp) != NULL) {
        // Remove newline and whitespace
        char* pos;
        if ((pos = strchr(line, '\n')) != NULL) *pos = '\0';

        // Trim leading and trailing whitespace
        char* start = line;
        while (*start && isspace(*start)) start++;
        char* end = start + strlen(start) - 1;
        while (end > start && isspace(*end)) *end-- = '\0';

        // Set the corresponding metric to true
        if (strcmp(start, "DCGM_FI_DEV_VRAM_TEMP") == 0) {
            config->vram_temp = true;
        } else if (strcmp(start, "DCGM_FI_DEV_HOT_SPOT_TEMP") == 0) {
            config->hotspot_temp = true;
        } else if (strcmp(start, "DCGM_FI_DEV_CLOCKS_THROTTLE_REASON") == 0) {
            config->clocks_throttle_reason = true;
        } else if (strcmp(start, "GPU_AER_TOTAL_ERRORS") == 0) {
            config->gpu_aer_total_errors = true;
        } else if (strcmp(start, "GPU_AER_ERROR_STATE") == 0) {
            config->gpu_aer_error_state = true;
        } else if (strcmp(start, "DCGM_FI_DEV_SM_CLOCK") == 0) {
            config->sm_clock = true;
        } else if (strcmp(start, "DCGM_FI_DEV_MEM_CLOCK") == 0) {
            config->mem_clock = true;
        } else if (strcmp(start, "DCGM_FI_DEV_GPU_TEMP") == 0) {
            config->gpu_temp = true;
        } else if (strcmp(start, "DCGM_FI_DEV_POWER_USAGE") == 0) {
            config->power_usage = true;
        } else if (strcmp(start, "DCGM_FI_DEV_FAN_SPEED") == 0) {
            config->fan_speed = true;
        } else if (strcmp(start, "DCGM_FI_DEV_GPU_UTIL") == 0) {
            config->gpu_util = true;
        } else if (strcmp(start, "DCGM_FI_DEV_MEM_COPY_UTIL") == 0) {
            config->mem_util = true;
        } else if (strcmp(start, "DCGM_FI_DEV_FB_FREE") == 0) {
            config->fb_free = true;
        } else if (strcmp(start, "DCGM_FI_DEV_FB_USED") == 0) {
            config->fb_used = true;
        } else if (strcmp(start, "DCGM_FI_DEV_NVLINK_BANDWIDTH_TOTAL") == 0) {
            config->nvlink_bandwidth_total = true;
        }
        // Ignore unknown metrics
    }
    fclose(fp);
}

// Function to create the metrics.txt file
#define UUID_MAX_LEN (NVML_DEVICE_UUID_BUFFER_SIZE - 1) // 80 - 1 = 79
#define NAME_MAX_LEN (NVML_DEVICE_NAME_BUFFER_SIZE - 1) // 64 - 1 = 63
#define HOSTNAME_MAX_LEN 255
#define DRIVER_VERSION_MAX_LEN (NVML_SYSTEM_DRIVER_VERSION_BUFFER_SIZE - 1) // 81 - 1 = 80

void createMetricFile(int device_count, MetricsConfig* metricsConfig){
    metrics_file = fopen("metrics.tmp", "w");
    if (!metrics_file) {
        fprintf(stderr, "Failed to open metrics.tmp for writing\n");
        return;
    }

    // Get hostname and driver version
    char hostname[256];
    gethostname(hostname, sizeof(hostname));
    hostname[sizeof(hostname) - 1] = '\0'; // Ensure null termination

    char driver_version[NVML_SYSTEM_DRIVER_VERSION_BUFFER_SIZE];
    nvmlReturn_t result = nvmlSystemGetDriverVersion(driver_version, sizeof(driver_version));
    if (result != NVML_SUCCESS) {
        fprintf(stderr, "Failed to get driver version: %s\n", nvmlErrorString(result));
        driver_version[0] = '\0';
    }
    driver_version[sizeof(driver_version) -1 ] = '\0'; // Ensure null termination

    // Iterate through devices and write metrics to the file
    for (int i = 0; i < device_count; i++) {
        // Include labels
        char device_label[1024];
        snprintf(device_label, sizeof(device_label),
                    "{gpu=\"%d\",UUID=\"%.*s\",device=\"nvidia%d\",modelName=\"%.*s\",Hostname=\"%.*s\",DCGM_FI_DRIVER_VERSION=\"%.*s\"}",
                    i,
                    UUID_MAX_LEN, devices[i].uuid,
                    i,
                    NAME_MAX_LEN, devices[i].device_name,
                    HOSTNAME_MAX_LEN, hostname,
                    DRIVER_VERSION_MAX_LEN, driver_version);

        // Write VRAM temperature
        if (metricsConfig->vram_temp) {
            fprintf(metrics_file, "# HELP DCGM_FI_DEV_VRAM_TEMP VRAM temperature (in C).\n");
            fprintf(metrics_file, "# TYPE DCGM_FI_DEV_VRAM_TEMP gauge\n");
            fprintf(metrics_file, "DCGM_FI_DEV_VRAM_TEMP%s %u\n", device_label, devices[i].vram_temp);
        }

        // Write Hot Spot temperature
        if (metricsConfig->hotspot_temp) {
            fprintf(metrics_file, "# HELP DCGM_FI_DEV_HOT_SPOT_TEMP Hot Spot temperature (in C).\n");
            fprintf(metrics_file, "# TYPE DCGM_FI_DEV_HOT_SPOT_TEMP gauge\n");
            fprintf(metrics_file, "DCGM_FI_DEV_HOT_SPOT_TEMP%s %u\n", device_label, devices[i].hotspot_temp);
        }

        // Write Clocks Throttle Reason
        if (metricsConfig->clocks_throttle_reason) {
            fprintf(metrics_file, "# HELP DCGM_FI_DEV_CLOCKS_THROTTLE_REASON Individual throttle reason for GPU clocks.\n");
            fprintf(metrics_file, "# TYPE DCGM_FI_DEV_CLOCKS_THROTTLE_REASON gauge\n");

            // Iterate through throttle reasons and write them to the file
            for (size_t j = 0; j < sizeof(throttleReasons) / sizeof(throttleReasons[0]); j++) {
                int isThrottling = (devices[i].clock_throttle_reasons & throttleReasons[j].reasonBit) ? 1 : 0;
                fprintf(metrics_file, "DCGM_FI_DEV_CLOCKS_THROTTLE_REASON{reason=\"%s\", gpu=\"%d\", UUID=\"%s\"} %d\n",
                    throttleReasons[j].reasonString, i, devices[i].uuid, isThrottling);
            }
        }

        // Write additional metrics as per metricsConfig
        if (metricsConfig->sm_clock) {
            fprintf(metrics_file, "# HELP DCGM_FI_DEV_SM_CLOCK SM clock frequency (in MHz).\n");
            fprintf(metrics_file, "# TYPE DCGM_FI_DEV_SM_CLOCK gauge\n");
            fprintf(metrics_file, "DCGM_FI_DEV_SM_CLOCK%s %u\n", device_label, devices[i].sm_clock);
        }

        if (metricsConfig->mem_clock) {
            fprintf(metrics_file, "# HELP DCGM_FI_DEV_MEM_CLOCK Memory clock frequency (in MHz).\n");
            fprintf(metrics_file, "# TYPE DCGM_FI_DEV_MEM_CLOCK gauge\n");
            fprintf(metrics_file, "DCGM_FI_DEV_MEM_CLOCK%s %u\n", device_label, devices[i].mem_clock);
        }

        if (metricsConfig->gpu_temp) {
            fprintf(metrics_file, "# HELP DCGM_FI_DEV_GPU_TEMP GPU temperature (in C).\n");
            fprintf(metrics_file, "# TYPE DCGM_FI_DEV_GPU_TEMP gauge\n");
            fprintf(metrics_file, "DCGM_FI_DEV_GPU_TEMP%s %u\n", device_label, devices[i].gpu_temp);
        }

        if (metricsConfig->power_usage) {
            fprintf(metrics_file, "# HELP DCGM_FI_DEV_POWER_USAGE Power draw (in W).\n");
            fprintf(metrics_file, "# TYPE DCGM_FI_DEV_POWER_USAGE gauge\n");
            fprintf(metrics_file, "DCGM_FI_DEV_POWER_USAGE%s %.6f\n", device_label, devices[i].power_usage / 1000.0);
        }

        if (metricsConfig->fan_speed) {
            fprintf(metrics_file, "# HELP DCGM_FI_DEV_FAN_SPEED Fan speed for the device.\n");
            fprintf(metrics_file, "# TYPE DCGM_FI_DEV_FAN_SPEED gauge\n");
            fprintf(metrics_file, "DCGM_FI_DEV_FAN_SPEED%s %u\n", device_label, devices[i].fan_speed);
        }

        if (metricsConfig->gpu_util) {
            fprintf(metrics_file, "# HELP DCGM_FI_DEV_GPU_UTIL GPU utilization (in %%).\n");
            fprintf(metrics_file, "# TYPE DCGM_FI_DEV_GPU_UTIL gauge\n");
            fprintf(metrics_file, "DCGM_FI_DEV_GPU_UTIL%s %u\n", device_label, devices[i].gpu_util);
        }

        if (metricsConfig->mem_util) {
            fprintf(metrics_file, "# HELP DCGM_FI_DEV_MEM_COPY_UTIL Memory utilization (in %%).\n");
            fprintf(metrics_file, "# TYPE DCGM_FI_DEV_MEM_COPY_UTIL gauge\n");
            fprintf(metrics_file, "DCGM_FI_DEV_MEM_COPY_UTIL%s %u\n", device_label, devices[i].mem_util);
        }

        if (metricsConfig->fb_free) {
            fprintf(metrics_file, "# HELP DCGM_FI_DEV_FB_FREE Frame buffer memory free (in MB).\n");
            fprintf(metrics_file, "# TYPE DCGM_FI_DEV_FB_FREE gauge\n");
            fprintf(metrics_file, "DCGM_FI_DEV_FB_FREE%s %llu\n", device_label, devices[i].fb_free);
        }

        if (metricsConfig->fb_used) {
            fprintf(metrics_file, "# HELP DCGM_FI_DEV_FB_USED Frame buffer memory used (in MB).\n");
            fprintf(metrics_file, "# TYPE DCGM_FI_DEV_FB_USED gauge\n");
            fprintf(metrics_file, "DCGM_FI_DEV_FB_USED%s %llu\n", device_label, devices[i].fb_used);
        }

        if (metricsConfig->gpu_aer_total_errors) {
            unsigned int total_aer_errors = getTotalAerErrorsForDevice(i);
            // Write metrics to file
            fprintf(metrics_file, "# HELP GPU_AER_TOTAL_ERRORS Total AER errors for GPU.\n");
            fprintf(metrics_file, "# TYPE GPU_AER_TOTAL_ERRORS counter\n");
            fprintf(metrics_file, "GPU_AER_TOTAL_ERRORS{gpu=\"%d\", UUID=\"%s\"} %u\n", i, devices[i].uuid, total_aer_errors);
        }

        if (metricsConfig->gpu_aer_error_state) {
            unsigned int error_state = checkGpuErrorState(i);
            fprintf(metrics_file, "# HELP GPU_AER_ERROR_STATE Current error state for GPU (1 for error, 0 for no error).\n");
            fprintf(metrics_file, "# TYPE GPU_AER_ERROR_STATE gauge\n");
            fprintf(metrics_file, "GPU_ERROR_STATE{gpu=\"%d\", UUID=\"%s\"} %d\n", i, devices[i].uuid, error_state);
        }

        // Implement other metrics as needed
    }

    // Inside createMetricFile or main loop
    unsigned int upgradablePackages = countUpgradablePackages();
    fprintf(metrics_file, "# HELP APT_UPGRADABLE_PACKAGES Number of APT packages that can be upgraded.\n");
    fprintf(metrics_file, "# TYPE APT_UPGRADABLE_PACKAGES gauge\n");
    fprintf(metrics_file, "APT_UPGRADABLE_PACKAGES %u\n", upgradablePackages);

    fflush(metrics_file);
    fclose(metrics_file);
    metrics_file = NULL;
    rename("metrics.tmp", "metrics.txt");
}

// Utility function to get the PCI bus ID as a string for a given GPU index
int getGpuPciBusId(unsigned int index, char *pciBusId, unsigned int length) {
    nvmlDevice_t device;
    nvmlReturn_t result = nvmlDeviceGetHandleByIndex(index, &device);
    if (result != NVML_SUCCESS) return -1;

    nvmlPciInfo_t pci;
    result = nvmlDeviceGetPciInfo(device, &pci);
    if (result != NVML_SUCCESS) return -1;

    snprintf(pciBusId, length, "%04x:%02x:%02x.0", pci.domain, pci.bus, pci.device);
    return 0;
}

// Function to count AER errors from /var/log/syslog for a specific GPU
unsigned int getTotalAerErrorsForDevice(unsigned int gpuIndex) {
    char pciBusId[20];
    if (getGpuPciBusId(gpuIndex, pciBusId, sizeof(pciBusId)) != 0) {
        fprintf(stderr, "Failed to get PCI bus ID for GPU %u\n", gpuIndex);
        return 0;
    }

    FILE *logFile = fopen("/var/log/syslog", "r");
    if (!logFile) {
        perror("Failed to open /var/log/syslog");
        return 0;
    }

    char *line = NULL;
    size_t len = 0;
    ssize_t read;
    unsigned int aerErrorCount = 0;

    while ((read = getline(&line, &len, logFile)) != -1) {
        if (strstr(line, "AER") && strstr(line, pciBusId)) {
            aerErrorCount++;
        }
    }

    free(line);
    fclose(logFile);

    return aerErrorCount;
}

// Function to initialize NVML, to be called before any other NVML operations
bool initializeNvml(void) {
    nvmlReturn_t result = nvmlInit();
    if (result == NVML_SUCCESS) {
        return true; // Initialization successful
    } else {
        fprintf(stderr, "Failed to initialize NVML: %s\n", nvmlErrorString(result));
        return false; // Initialization failed
    }
}

// Function to check if there's an error state for a given GPU
unsigned int checkGpuErrorState(unsigned int gpuIndex) {
    nvmlDevice_t device;
    unsigned int fanSpeed;
    nvmlReturn_t result;

    // Attempt to get handle for the specified GPU
    result = nvmlDeviceGetHandleByIndex(gpuIndex, &device);
    if (result != NVML_SUCCESS) {
        fprintf(stderr, "Failed to get handle for GPU %u: %s\n", gpuIndex, nvmlErrorString(result));
        return 2; // Error state
    }

    // Attempt to get the fan speed for the GPU
    result = nvmlDeviceGetFanSpeed(device, &fanSpeed);
    if (result != NVML_SUCCESS) {
        fprintf(stderr, "Failed to get fan speed for GPU %u: %s\n", gpuIndex, nvmlErrorString(result));
        return 2; // Error state
    }

    return 1; // No error state
}

// Function to print the help message
void printHelpMessage(void) {
    printf("Usage: nvml_direct_access [options]\n");
    printf("Options:\n");
    printf("  --help, -h      Show this help message and exit\n");
    printf("  --no-console    Disable console output of GPU metrics\n");
    printf("\n");
    printf("Available metrics that can be added to metrics.ini:\n");
    printf("  DCGM_FI_DEV_VRAM_TEMP\n");
    printf("  DCGM_FI_DEV_HOT_SPOT_TEMP\n");
    printf("  DCGM_FI_DEV_CLOCKS_THROTTLE_REASON\n");
    printf("  GPU_AER_TOTAL_ERRORS\n");
    printf("  GPU_AER_ERROR_STATE\n");
    printf("  DCGM_FI_DEV_SM_CLOCK\n");
    printf("  DCGM_FI_DEV_MEM_CLOCK\n");
    printf("  DCGM_FI_DEV_GPU_TEMP\n");
    printf("  DCGM_FI_DEV_POWER_USAGE\n");
    printf("  DCGM_FI_DEV_FAN_SPEED\n");
    printf("  DCGM_FI_DEV_GPU_UTIL\n");
    printf("  DCGM_FI_DEV_MEM_COPY_UTIL\n");
    printf("  DCGM_FI_DEV_FB_FREE\n");
    printf("  DCGM_FI_DEV_FB_USED\n");
    printf("  DCGM_FI_DEV_NVLINK_BANDWIDTH_TOTAL\n");
    printf("\n");
    printf("Add any of the above metrics to the metrics.ini file to enable them.\n");
    printf("\n");
    printf("Example of console output when not disabled:\n");
    printf("GPU Name: NVIDIA RTX A6000 GPU 0: Temperature: 30 C Power Usage: 28.38 W VRAM Temp: 54 C HotSpotTemp: 38 C Fan: 10%% Core Utilization: 1%%\n");
}

int main(int argc, char* argv[]) {
    // Default is to print to console
    bool console_output = true;

    // Check for command-line arguments
    for (int argi = 1; argi < argc; argi++) {
        if (strcmp(argv[argi], "--help") == 0 || strcmp(argv[argi], "-h") == 0) {
            printHelpMessage();
            return 0;
        } else if (strcmp(argv[argi], "--no-console") == 0) {
            console_output = false;
        } else {
            fprintf(stderr, "Unknown option: %s\n", argv[argi]);
            fprintf(stderr, "Use --help or -h for usage information.\n");
            return 1;
        }
    }


    // Open the metrics file in write mode to overwrite the existing content
    cleanup_sig_handler();

    MetricsConfig metricsConfig;
    loadMetricsConfig(&metricsConfig);

    while(1){
        // Initialize PCI library
        struct pci_access *pacc = pci_alloc();
        nvmlReturn_t result = nvmlInit();
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

        for (unsigned int i = 0; i < device_count; i++) {
            nvmlDevice_t nvml_device;
            unsigned long long clocksThrottleReasons;
            char device_name[NVML_DEVICE_NAME_BUFFER_SIZE];

            // Store data in the devices array

            result = nvmlDeviceGetHandleByIndex(i, &nvml_device);
            if (result != NVML_SUCCESS) {
                fprintf(stderr, "Failed to get handle for device %u: %s\n", i, nvmlErrorString(result));
                continue;
            }

            result = nvmlDeviceGetName(nvml_device, device_name, NVML_DEVICE_NAME_BUFFER_SIZE);
            if (result == NVML_SUCCESS) {
                // Ensure null termination of device_name
                device_name[NVML_DEVICE_NAME_BUFFER_SIZE - 1] = '\0';
                strncpy(devices[i].device_name, device_name, sizeof(devices[i].device_name));
                devices[i].device_name[sizeof(devices[i].device_name) - 1] = '\0'; // Ensure null termination
            } else {
                fprintf(stderr, "Failed to get name for device: %s\n", nvmlErrorString(result));
                devices[i].device_name[0] = '\0'; // Ensure the string is empty in case of failure
            }

            nvmlPciInfo_t pciInfo;
            result = nvmlDeviceGetPciInfo(nvml_device, &pciInfo);
            if (result != NVML_SUCCESS) {
                fprintf(stderr, "Failed to get PCI info for device %u: %s\n", i, nvmlErrorString(result));
                continue;
            }

            // Collect metrics
            if (metricsConfig.gpu_temp) {
                unsigned int temp;
                result = nvmlDeviceGetTemperature(nvml_device, NVML_TEMPERATURE_GPU, &temp);
                if (result == NVML_SUCCESS) {
                    devices[i].gpu_temp = temp;
                } else {
                    fprintf(stderr, "Failed to get temperature for device %u: %s\n", i, nvmlErrorString(result));
                    devices[i].gpu_temp = 0;
                }
            }

            if (metricsConfig.power_usage) {
                unsigned int power;
                result = nvmlDeviceGetPowerUsage(nvml_device, &power);
                if (result == NVML_SUCCESS) {
                    devices[i].power_usage = power;
                } else {
                    fprintf(stderr, "Failed to get power usage for device %u: %s\n", i, nvmlErrorString(result));
                    devices[i].power_usage = 0;
                }
            }

            // Additional metrics
            if (metricsConfig.sm_clock) {
                unsigned int sm_clock;
                result = nvmlDeviceGetClockInfo(nvml_device, NVML_CLOCK_SM, &sm_clock);
                if (result == NVML_SUCCESS) {
                    devices[i].sm_clock = sm_clock;
                } else {
                    fprintf(stderr, "Failed to get SM clock for device %u: %s\n", i, nvmlErrorString(result));
                    devices[i].sm_clock = 0;
                }
            }

            if (metricsConfig.mem_clock) {
                unsigned int mem_clock;
                result = nvmlDeviceGetClockInfo(nvml_device, NVML_CLOCK_MEM, &mem_clock);
                if (result == NVML_SUCCESS) {
                    devices[i].mem_clock = mem_clock;
                } else {
                    fprintf(stderr, "Failed to get Memory clock for device %u: %s\n", i, nvmlErrorString(result));
                    devices[i].mem_clock = 0;
                }
            }

            if (metricsConfig.fan_speed) {
                unsigned int fan_speed;
                result = nvmlDeviceGetFanSpeed(nvml_device, &fan_speed);
                if (result == NVML_SUCCESS) {
                    devices[i].fan_speed = fan_speed;
                } else {
                    fprintf(stderr, "Failed to get fan speed for device %u: %s\n", i, nvmlErrorString(result));
                    devices[i].fan_speed = 0;
                }
            }

            if (metricsConfig.gpu_util || metricsConfig.mem_util) {
                nvmlUtilization_t utilization;
                result = nvmlDeviceGetUtilizationRates(nvml_device, &utilization);
                if (result == NVML_SUCCESS) {
                    if (metricsConfig.gpu_util) {
                        devices[i].gpu_util = utilization.gpu;
                    }
                    if (metricsConfig.mem_util) {
                        devices[i].mem_util = utilization.memory;
                    }
                } else {
                    fprintf(stderr, "Failed to get utilization rates for device %u: %s\n", i, nvmlErrorString(result));
                    if (metricsConfig.gpu_util) {
                        devices[i].gpu_util = 0;
                    }
                    if (metricsConfig.mem_util) {
                        devices[i].mem_util = 0;
                    }
                }
            }

            if (metricsConfig.fb_free || metricsConfig.fb_used) {
                nvmlMemory_t memory;
                result = nvmlDeviceGetMemoryInfo(nvml_device, &memory);
                if (result == NVML_SUCCESS) {
                    if (metricsConfig.fb_free) {
                        devices[i].fb_free = memory.free / (1024 * 1024); // Convert to MB
                    }
                    if (metricsConfig.fb_used) {
                        devices[i].fb_used = memory.used / (1024 * 1024); // Convert to MB
                    }
                } else {
                    fprintf(stderr, "Failed to get memory info for device %u: %s\n", i, nvmlErrorString(result));
                    if (metricsConfig.fb_free) {
                        devices[i].fb_free = 0;
                    }
                    if (metricsConfig.fb_used) {
                        devices[i].fb_used = 0;
                    }
                }
            }

            for (struct pci_dev *pci_dev = pacc->devices; pci_dev; pci_dev = pci_dev->next) {
                pci_fill_info(pci_dev, PCI_FILL_IDENT | PCI_FILL_BASES | PCI_FILL_CLASS);
                unsigned int combinedDeviceId = (pci_dev->device_id << 16) | pci_dev->vendor_id;

                if (combinedDeviceId == pciInfo.pciDeviceId &&
                    (unsigned int)pci_dev->domain == pciInfo.domain &&
                    pci_dev->bus == pciInfo.bus &&
                    pci_dev->dev == pciInfo.device) {

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
                    if (i < MAX_DEVICES) {
                        devices[i].vram_temp = vram_temp_value;
                    } else {
                        fprintf(stderr, "Exceeded maximum number of devices\n");
                    }

                    char uuid[NVML_DEVICE_UUID_BUFFER_SIZE];
                    result = nvmlDeviceGetUUID(nvml_device, uuid, sizeof(uuid));
                    if (result == NVML_SUCCESS) {
                        // Ensure null termination of uuid
                        uuid[NVML_DEVICE_UUID_BUFFER_SIZE - 1] = '\0';
                        strncpy(devices[i].uuid, uuid, sizeof(devices[i].uuid));
                        devices[i].uuid[sizeof(devices[i].uuid) - 1] = '\0'; // Ensure null termination 
                    } else {
                        fprintf(stderr, "Failed to get UUID for device %d: %s\n", i, nvmlErrorString(result));
                        devices[i].uuid[0] = '\0'; // Ensure the string is empty in case of failure
                    }

                    if (i < MAX_DEVICES) {
                        strncpy(devices[i].uuid, uuid, sizeof(devices[i].uuid));
                        devices[i].uuid[sizeof(devices[i].uuid) - 1] = '\0'; // Ensure null termination
                    } else {
                        fprintf(stderr, "Exceeded maximum number of devices\n");
                    }

                    // Code to read the hot spot temperature for the current device
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
                        if (i < MAX_DEVICES) {
                            devices[i].hotspot_temp = hotSpotTemp;
                        } else {
                            fprintf(stderr, "Exceeded maximum number of devices\n");
                        }
                    }

                    if (metricsConfig.clocks_throttle_reason) {
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
                    }

                    // Flush the stream to write to the file immediately
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
        createMetricFile(device_count, &metricsConfig);
        // If console output is enabled, print the metrics to console
        if (console_output) {
            printConsoleOutput(&metricsConfig);
        }
        pci_cleanup(pacc);
        nvmlShutdown();
        sleep(5);
    }

    return 0;
}

void printConsoleOutput(MetricsConfig* metricsConfig) {
    FILE* fp = fopen("metrics.txt", "r");
    if (fp == NULL) {
        fprintf(stderr, "Failed to open metrics.txt for reading\n");
        return;
    }

    // Define a structure to hold the metrics per GPU
    typedef struct {
        char gpu_name[NVML_DEVICE_NAME_BUFFER_SIZE];
        char uuid[NVML_DEVICE_UUID_BUFFER_SIZE];
        int gpu_index;
        char model_name[NVML_DEVICE_NAME_BUFFER_SIZE];
        unsigned int gpu_temp;
        float power_usage;
        unsigned int vram_temp;
        unsigned int hotspot_temp;
        unsigned int fan_speed;
        unsigned int gpu_util;
        // Add other metrics as needed
        bool has_data; // Indicates if data for this GPU is available
    } ConsoleDeviceData;

    ConsoleDeviceData console_devices[MAX_DEVICES];
    memset(console_devices, 0, sizeof(console_devices));

    char line[1024];
    while (fgets(line, sizeof(line), fp) != NULL) {
        // Skip comments and empty lines
        if (line[0] == '#' || line[0] == '\n') {
            continue;
        }

        // Parse the metric line
        char metric_name[128];
        char labels[512];
        char value_str[64];

        char* ptr = strchr(line, '{');
        if (ptr) {
            // Metric with labels
            size_t name_len = ptr - line;
            if (name_len >= sizeof(metric_name)) {
                name_len = sizeof(metric_name) - 1;
            }
            strncpy(metric_name, line, name_len);
            metric_name[name_len] = '\0';

            char* end_labels = strchr(ptr, '}');
            if (!end_labels) continue; // Malformed line, skip
            size_t labels_len = end_labels - ptr - 1;
            if (labels_len >= sizeof(labels)) {
                labels_len = sizeof(labels) - 1;
            }
            strncpy(labels, ptr + 1, labels_len);
            labels[labels_len] = '\0';

            // Get the value after the labels
            char* value_ptr = end_labels + 1;
            while (*value_ptr && isspace(*value_ptr)) value_ptr++;
            strncpy(value_str, value_ptr, sizeof(value_str) - 1);
            value_str[sizeof(value_str) - 1] = '\0';

        } else {
            // Metric without labels (unlikely in this case)
            continue; // Skip
        }

        // Extract GPU index from labels
        int gpu_index = -1;
        char uuid[NVML_DEVICE_UUID_BUFFER_SIZE] = "";
        char model_name[NVML_DEVICE_NAME_BUFFER_SIZE] = "";
        //char hostname[256] = "";
        //char driver_version[NVML_SYSTEM_DRIVER_VERSION_BUFFER_SIZE] = "";
        // Extract other labels if needed

        // Tokenize labels
        char* label_ptr = strtok(labels, ",");
        while (label_ptr != NULL) {
            char* equal_sign = strchr(label_ptr, '=');
            if (equal_sign) {
                *equal_sign = '\0';
                char* key = label_ptr;
                char* value = equal_sign + 1;

                // Remove quotes from value
                if (value[0] == '\"') value++;
                size_t len = strlen(value);
                if (len > 0 && value[len - 1] == '\"') value[len - 1] = '\0';

                if (strcmp(key, "gpu") == 0) {
                    gpu_index = atoi(value);
                } else if (strcmp(key, "UUID") == 0) {
                    snprintf(uuid, sizeof(uuid), "%s", value);
                } else if (strcmp(key, "modelName") == 0) {
                    snprintf(model_name, sizeof(model_name), "%s", value);
                } else if (strcmp(key, "Hostname") == 0) {
                    // Optionally store hostname if needed
                } else if (strcmp(key, "DCGM_FI_DRIVER_VERSION") == 0) {
                    // Optionally store driver version if needed
                }
                // Handle other labels if needed
            }
            label_ptr = strtok(NULL, ",");
        }

        if (gpu_index >= 0 && gpu_index < MAX_DEVICES) {
            console_devices[gpu_index].gpu_index = gpu_index;
            console_devices[gpu_index].has_data = true;

            // Store common labels
            if (uuid[0] != '\0') {
                snprintf(console_devices[gpu_index].uuid, sizeof(console_devices[gpu_index].uuid), "%s", uuid);
            }
            if (model_name[0] != '\0') {
                snprintf(console_devices[gpu_index].model_name, sizeof(console_devices[gpu_index].model_name), "%s", model_name);
            }

            // Store metric values based on metric name
            if (strcmp(metric_name, "DCGM_FI_DEV_GPU_TEMP") == 0 && metricsConfig->gpu_temp) {
                console_devices[gpu_index].gpu_temp = atoi(value_str);
            } else if (strcmp(metric_name, "DCGM_FI_DEV_POWER_USAGE") == 0 && metricsConfig->power_usage) {
                console_devices[gpu_index].power_usage = atof(value_str);
            } else if (strcmp(metric_name, "DCGM_FI_DEV_VRAM_TEMP") == 0 && metricsConfig->vram_temp) {
                console_devices[gpu_index].vram_temp = atoi(value_str);
            } else if (strcmp(metric_name, "DCGM_FI_DEV_HOT_SPOT_TEMP") == 0 && metricsConfig->hotspot_temp) {
                console_devices[gpu_index].hotspot_temp = atoi(value_str);
            } else if (strcmp(metric_name, "DCGM_FI_DEV_FAN_SPEED") == 0 && metricsConfig->fan_speed) {
                console_devices[gpu_index].fan_speed = atoi(value_str);
            } else if (strcmp(metric_name, "DCGM_FI_DEV_GPU_UTIL") == 0 && metricsConfig->gpu_util) {
                console_devices[gpu_index].gpu_util = atoi(value_str);
            }
            // Add other metrics as needed
        }
    }

    fclose(fp);

    // Print the console output
    for (int i = 0; i < MAX_DEVICES; i++) {
        if (console_devices[i].has_data) {
            printf("GPU Name: %s GPU %d:", console_devices[i].model_name, console_devices[i].gpu_index);
            if (metricsConfig->gpu_temp) {
                printf(" Temperature: %u C", console_devices[i].gpu_temp);
            }
            if (metricsConfig->power_usage) {
                printf(" Power Usage: %.2f W", console_devices[i].power_usage);
            }
            if (metricsConfig->vram_temp) {
                printf(" VRAM Temp: %u C", console_devices[i].vram_temp);
            }
            if (metricsConfig->hotspot_temp) {
                printf(" HotSpotTemp: %u C", console_devices[i].hotspot_temp);
            }
            if (metricsConfig->fan_speed) {
                printf(" Fan: %u %%", console_devices[i].fan_speed);
            }
            if (metricsConfig->gpu_util) {
                printf(" Core Utilization: %u %%", console_devices[i].gpu_util);
            }
            // Add other metrics as needed

            printf("\n");
        }
    }
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
