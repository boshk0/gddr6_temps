#include <iostream>
#include <nvml.h>

int main() {
    // Initialize NVML
    nvmlReturn_t result = nvmlInit();
    if (result != NVML_SUCCESS) {
        std::cerr << "Failed to initialize NVML: " << nvmlErrorString(result) << std::endl;
        return 1;
    }

    // Get the number of GPUs
    unsigned int deviceCount;
    result = nvmlDeviceGetCount(&deviceCount);
    if (result != NVML_SUCCESS) {
        std::cerr << "Failed to get device count: " << nvmlErrorString(result) << std::endl;
        nvmlShutdown();
        return 1;
    }

    // Iterate over each GPU
    for (unsigned int i = 0; i < deviceCount; i++) {
        nvmlDevice_t device;
        result = nvmlDeviceGetHandleByIndex(i, &device);
        if (result == NVML_SUCCESS) {
            // Get device name
            char name[NVML_DEVICE_NAME_BUFFER_SIZE];
            result = nvmlDeviceGetName(device, name, NVML_DEVICE_NAME_BUFFER_SIZE);
            if (result != NVML_SUCCESS) {
                std::cerr << "Failed to get name for device " << i << ": " << nvmlErrorString(result) << std::endl;
                continue;
            }

            // Get PCI info to retrieve device ID
            nvmlPciInfo_t pci;
            result = nvmlDeviceGetPciInfo(device, &pci);
            if (result != NVML_SUCCESS) {
                std::cerr << "Failed to get PCI info for device " << i << ": " << nvmlErrorString(result) << std::endl;
                continue;
            }

            std::cout << ".dev_id = 0x" << std::hex << pci.deviceId << " .name = \"" << name << "\"" << std::endl;
        } else {
            std::cerr << "Failed to get handle for device " << i << ": " << nvmlErrorString(result) << std::endl;
        }
    }

    // Shutdown NVML
    nvmlShutdown();

    return 0;
}
