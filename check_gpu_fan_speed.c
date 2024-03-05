#include <nvml.h>
#include <stdio.h>

void printAdditionalInfo(nvmlDevice_t device) {
    nvmlReturn_t result;
    nvmlUtilization_t utilization;
    unsigned int temperature;
    unsigned int power;
    nvmlMemory_t memory;

    // GPU Utilization
    result = nvmlDeviceGetUtilizationRates(device, &utilization);
    if (result == NVML_SUCCESS) {
        printf("\tGPU Utilization: %u%%, Memory Utilization: %u%%\n", utilization.gpu, utilization.memory);
    } else {
        printf("\tFailed to get GPU utilization: %s\n", nvmlErrorString(result));
    }

    // Temperature
    result = nvmlDeviceGetTemperature(device, NVML_TEMPERATURE_GPU, &temperature);
    if (result == NVML_SUCCESS) {
        printf("\tTemperature: %u C\n", temperature);
    } else {
        printf("\tFailed to get temperature: %s\n", nvmlErrorString(result));
    }

    // Power Usage
    result = nvmlDeviceGetPowerUsage(device, &power);
    if (result == NVML_SUCCESS) {
        printf("\tPower Usage: %.2f W\n", power / 1000.0);
    } else {
        printf("\tFailed to get power usage: %s\n", nvmlErrorString(result));
    }

    // Memory Information
    result = nvmlDeviceGetMemoryInfo(device, &memory);
    if (result == NVML_SUCCESS) {
        printf("\tMemory Total: %llu MB, Used: %llu MB, Free: %llu MB\n", memory.total / 1024 / 1024, memory.used / 1024 / 1024, memory.free / 1024 / 1024);
    } else {
        printf("\tFailed to get memory information: %s\n", nvmlErrorString(result));
    }
}

void checkFanSpeed(unsigned int device_count) {
    nvmlReturn_t result;
    nvmlDevice_t device;
    unsigned int fanSpeed;

    nvmlPciInfo_t pciInfo;


    for (unsigned int i = 0; i < device_count; i++) {
        result = nvmlDeviceGetHandleByIndex(i, &device);
        if (NVML_SUCCESS != result) {
            fprintf(stderr, "Failed to get handle for device %u: %s\n", i, nvmlErrorString(result));
            continue;
        }

        result = nvmlDeviceGetPciInfo(device, &pciInfo);
        if (result != NVML_SUCCESS) {
            fprintf(stderr, "Failed to get PCI info for device %u: %s\n", i, nvmlErrorString(result));
           continue;
        }

    
        result = nvmlDeviceGetFanSpeed(device, &fanSpeed);
        if (NVML_SUCCESS == result) {
            
            printf("Device %u: Fan Speed: %u%% pciInfo.domain:%u pciDeviceId:%u \n", i, fanSpeed, pciInfo.domain, pciInfo.bus );
        } else {
            fprintf(stderr, "Failed to get fan speed for device %u: %s. This could indicate a problem.\n", i, nvmlErrorString(result));
            printAdditionalInfo(device);  // Print additional GPU information
        }
    }
}

int main() {
    nvmlReturn_t result = nvmlInit();
    if (NVML_SUCCESS != result) {
        fprintf(stderr, "Failed to initialize NVML: %s\n", nvmlErrorString(result));
        return 1;
    }

    unsigned int device_count;
    result = nvmlDeviceGetCount(&device_count);
    if (NVML_SUCCESS != result) {
        fprintf(stderr, "Failed to get device count: %s\n", nvmlErrorString(result));
        nvmlShutdown();
        return 1;
    }

    checkFanSpeed(device_count);



    nvmlShutdown();
    return 0;
}
