#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#define VK_USE_PLATFORM_WIN32_KHR
#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>

#include "aurora/vulkan_presenter.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <utility>

namespace aurora {
namespace {
template <class T> T structure(VkStructureType type) {
    T value{};
    value.sType = type;
    return value;
}
void check(VkResult result, const char* operation) {
    if (result != VK_SUCCESS) {
        throw std::runtime_error(std::string(operation) + " falhou (VkResult " +
                                 std::to_string(result) + ").");
    }
}
}

struct VulkanPresenter::Impl {
    HWND window{};
    HMODULE loader{};
    VkInstance instance{};
    VkSurfaceKHR surface{};
    VkPhysicalDevice physical{};
    VkDevice device{};
    VkQueue queue{};
    std::uint32_t queueFamily{};
    VkCommandPool commandPool{};
    VkCommandBuffer commandBuffer{};
    VkSwapchainKHR swapchain{};
    VkFormat format{};
    VkExtent2D extent{};
    std::uint32_t clientWidth{}, clientHeight{};
    std::vector<VkImage> images;
    VkSemaphore acquired{};
    std::vector<VkSemaphore> ready;
    VkBuffer upload{};
    VkDeviceMemory uploadMemory{};
    void* mapped{};
    std::string name;

    PFN_vkGetInstanceProcAddr getInstanceProcAddr{};
    PFN_vkGetDeviceProcAddr getDeviceProcAddr{};
    PFN_vkDestroyInstance destroyInstance{};
    PFN_vkDestroySurfaceKHR destroySurface{};
    PFN_vkCreateWin32SurfaceKHR createSurface{};
    PFN_vkEnumeratePhysicalDevices enumeratePhysicalDevices{};
    PFN_vkGetPhysicalDeviceProperties getPhysicalDeviceProperties{};
    PFN_vkGetPhysicalDeviceQueueFamilyProperties getQueueFamilyProperties{};
    PFN_vkGetPhysicalDeviceSurfaceSupportKHR getSurfaceSupport{};
    PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR getSurfaceCapabilities{};
    PFN_vkGetPhysicalDeviceSurfaceFormatsKHR getSurfaceFormats{};
    PFN_vkGetPhysicalDeviceMemoryProperties getMemoryProperties{};
    PFN_vkEnumerateDeviceExtensionProperties enumerateDeviceExtensions{};
    PFN_vkCreateDevice createDevice{};
    PFN_vkDestroyDevice destroyDevice{};
    PFN_vkGetDeviceQueue getDeviceQueue{};
    PFN_vkDeviceWaitIdle deviceWaitIdle{};
    PFN_vkQueueWaitIdle queueWaitIdle{};
    PFN_vkCreateCommandPool createCommandPool{};
    PFN_vkDestroyCommandPool destroyCommandPool{};
    PFN_vkAllocateCommandBuffers allocateCommandBuffers{};
    PFN_vkResetCommandBuffer resetCommandBuffer{};
    PFN_vkBeginCommandBuffer beginCommandBuffer{};
    PFN_vkEndCommandBuffer endCommandBuffer{};
    PFN_vkCreateSwapchainKHR createSwapchain{};
    PFN_vkDestroySwapchainKHR destroySwapchain{};
    PFN_vkGetSwapchainImagesKHR getSwapchainImages{};
    PFN_vkAcquireNextImageKHR acquireNextImage{};
    PFN_vkQueuePresentKHR queuePresent{};
    PFN_vkCreateSemaphore createSemaphore{};
    PFN_vkDestroySemaphore destroySemaphore{};
    PFN_vkCreateBuffer createBuffer{};
    PFN_vkDestroyBuffer destroyBuffer{};
    PFN_vkGetBufferMemoryRequirements getBufferMemoryRequirements{};
    PFN_vkAllocateMemory allocateMemory{};
    PFN_vkFreeMemory freeMemory{};
    PFN_vkBindBufferMemory bindBufferMemory{};
    PFN_vkMapMemory mapMemory{};
    PFN_vkUnmapMemory unmapMemory{};
    PFN_vkCmdPipelineBarrier cmdPipelineBarrier{};
    PFN_vkCmdCopyBufferToImage cmdCopyBufferToImage{};
    PFN_vkQueueSubmit queueSubmit{};

    template <class T> void instanceFunction(T& target, const char* symbol) {
        target = reinterpret_cast<T>(getInstanceProcAddr(instance, symbol));
        if (!target) throw std::runtime_error(std::string("Vulkan: funcao ausente: ") + symbol);
    }

    template <class T> void deviceFunction(T& target, const char* symbol) {
        target = reinterpret_cast<T>(getDeviceProcAddr(device, symbol));
        if (!target) throw std::runtime_error(std::string("Vulkan: funcao ausente: ") + symbol);
    }

    ~Impl() {
        if (device && deviceWaitIdle) deviceWaitIdle(device);
        releaseSwapchain();
        if (device && acquired && destroySemaphore) destroySemaphore(device, acquired, nullptr);
        if (device && commandPool && destroyCommandPool) destroyCommandPool(device, commandPool, nullptr);
        if (device && destroyDevice) destroyDevice(device, nullptr);
        if (surface && destroySurface) destroySurface(instance, surface, nullptr);
        if (instance && destroyInstance) destroyInstance(instance, nullptr);
        if (loader) FreeLibrary(loader);
    }

    void releaseSwapchain() {
        if (mapped && unmapMemory) unmapMemory(device, uploadMemory);
        mapped = nullptr;
        if (upload && destroyBuffer) destroyBuffer(device, upload, nullptr);
        upload = VK_NULL_HANDLE;
        if (uploadMemory && freeMemory) freeMemory(device, uploadMemory, nullptr);
        uploadMemory = VK_NULL_HANDLE;
        for (auto semaphore : ready) {
            if (semaphore && destroySemaphore) destroySemaphore(device, semaphore, nullptr);
        }
        ready.clear();
        images.clear();
        if (swapchain && destroySwapchain) destroySwapchain(device, swapchain, nullptr);
        swapchain = VK_NULL_HANDLE;
    }

    bool clientSize(std::uint32_t& width, std::uint32_t& height) const {
        RECT rect{};
        if (!GetClientRect(window, &rect)) throw std::runtime_error("Vulkan: janela invalida.");
        width = static_cast<std::uint32_t>(std::max<LONG>(0, rect.right - rect.left));
        height = static_cast<std::uint32_t>(std::max<LONG>(0, rect.bottom - rect.top));
        return width && height;
    }

    void initialize(void* hwnd) {
        window = static_cast<HWND>(hwnd);
        if (!IsWindow(window)) throw std::runtime_error("Vulkan: HWND invalido.");
        loader = LoadLibraryExW(L"vulkan-1.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (!loader) throw std::runtime_error("Vulkan indisponivel. Instale o driver oficial da sua GPU.");
        const auto loaderEntry = GetProcAddress(loader, "vkGetInstanceProcAddr");
        static_assert(sizeof(getInstanceProcAddr) == sizeof(loaderEntry));
        std::memcpy(&getInstanceProcAddr, &loaderEntry, sizeof(loaderEntry));
        if (!getInstanceProcAddr) throw std::runtime_error("Loader Vulkan invalido.");
        auto enumerateVersion = reinterpret_cast<PFN_vkEnumerateInstanceVersion>(
            getInstanceProcAddr(nullptr, "vkEnumerateInstanceVersion"));
        std::uint32_t version = VK_API_VERSION_1_0;
        if (enumerateVersion) check(enumerateVersion(&version), "vkEnumerateInstanceVersion");
        if (version < VK_API_VERSION_1_1) throw std::runtime_error("Este prototipo requer Vulkan 1.1.");
        auto createInstance = reinterpret_cast<PFN_vkCreateInstance>(
            getInstanceProcAddr(nullptr, "vkCreateInstance"));
        if (!createInstance) throw std::runtime_error("vkCreateInstance indisponivel.");
        const char* extensions[]{VK_KHR_SURFACE_EXTENSION_NAME, VK_KHR_WIN32_SURFACE_EXTENSION_NAME};
        auto app = structure<VkApplicationInfo>(VK_STRUCTURE_TYPE_APPLICATION_INFO);
        app.pApplicationName = "Aurora Core Lab";
        app.applicationVersion = VK_MAKE_VERSION(0, 3, 0);
        app.pEngineName = "Aurora PS1 experimental";
        app.apiVersion = VK_API_VERSION_1_1;
        auto info = structure<VkInstanceCreateInfo>(VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO);
        info.pApplicationInfo = &app;
        info.enabledExtensionCount = 2;
        info.ppEnabledExtensionNames = extensions;
        check(createInstance(&info, nullptr, &instance), "vkCreateInstance");
        instanceFunction(destroyInstance, "vkDestroyInstance");
        instanceFunction(destroySurface, "vkDestroySurfaceKHR");
        instanceFunction(createSurface, "vkCreateWin32SurfaceKHR");
        instanceFunction(enumeratePhysicalDevices, "vkEnumeratePhysicalDevices");
        instanceFunction(getPhysicalDeviceProperties, "vkGetPhysicalDeviceProperties");
        instanceFunction(getQueueFamilyProperties, "vkGetPhysicalDeviceQueueFamilyProperties");
        instanceFunction(getSurfaceSupport, "vkGetPhysicalDeviceSurfaceSupportKHR");
        instanceFunction(getSurfaceCapabilities, "vkGetPhysicalDeviceSurfaceCapabilitiesKHR");
        instanceFunction(getSurfaceFormats, "vkGetPhysicalDeviceSurfaceFormatsKHR");
        instanceFunction(getMemoryProperties, "vkGetPhysicalDeviceMemoryProperties");
        instanceFunction(enumerateDeviceExtensions, "vkEnumerateDeviceExtensionProperties");
        instanceFunction(createDevice, "vkCreateDevice");
        instanceFunction(getDeviceProcAddr, "vkGetDeviceProcAddr");
        auto surfaceInfo = structure<VkWin32SurfaceCreateInfoKHR>(VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR);
        surfaceInfo.hinstance = GetModuleHandleW(nullptr);
        surfaceInfo.hwnd = window;
        check(createSurface(instance, &surfaceInfo, nullptr, &surface), "vkCreateWin32SurfaceKHR");
        selectDevice();
        const float priority = 1.0f;
        auto queueInfo = structure<VkDeviceQueueCreateInfo>(VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO);
        queueInfo.queueFamilyIndex = queueFamily;
        queueInfo.queueCount = 1;
        queueInfo.pQueuePriorities = &priority;
        const char* deviceExtensions[]{VK_KHR_SWAPCHAIN_EXTENSION_NAME};
        auto deviceInfo = structure<VkDeviceCreateInfo>(VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO);
        deviceInfo.queueCreateInfoCount = 1;
        deviceInfo.pQueueCreateInfos = &queueInfo;
        deviceInfo.enabledExtensionCount = 1;
        deviceInfo.ppEnabledExtensionNames = deviceExtensions;
        check(createDevice(physical, &deviceInfo, nullptr, &device), "vkCreateDevice");
        deviceFunction(destroyDevice, "vkDestroyDevice");
        deviceFunction(deviceWaitIdle, "vkDeviceWaitIdle");
        deviceFunction(getDeviceQueue, "vkGetDeviceQueue");
        deviceFunction(queueWaitIdle, "vkQueueWaitIdle");
        deviceFunction(createCommandPool, "vkCreateCommandPool");
        deviceFunction(destroyCommandPool, "vkDestroyCommandPool");
        deviceFunction(allocateCommandBuffers, "vkAllocateCommandBuffers");
        deviceFunction(resetCommandBuffer, "vkResetCommandBuffer");
        deviceFunction(beginCommandBuffer, "vkBeginCommandBuffer");
        deviceFunction(endCommandBuffer, "vkEndCommandBuffer");
        deviceFunction(createSwapchain, "vkCreateSwapchainKHR");
        deviceFunction(destroySwapchain, "vkDestroySwapchainKHR");
        deviceFunction(getSwapchainImages, "vkGetSwapchainImagesKHR");
        deviceFunction(acquireNextImage, "vkAcquireNextImageKHR");
        deviceFunction(queuePresent, "vkQueuePresentKHR");
        deviceFunction(createSemaphore, "vkCreateSemaphore");
        deviceFunction(destroySemaphore, "vkDestroySemaphore");
        deviceFunction(createBuffer, "vkCreateBuffer");
        deviceFunction(destroyBuffer, "vkDestroyBuffer");
        deviceFunction(getBufferMemoryRequirements, "vkGetBufferMemoryRequirements");
        deviceFunction(allocateMemory, "vkAllocateMemory");
        deviceFunction(freeMemory, "vkFreeMemory");
        deviceFunction(bindBufferMemory, "vkBindBufferMemory");
        deviceFunction(mapMemory, "vkMapMemory");
        deviceFunction(unmapMemory, "vkUnmapMemory");
        deviceFunction(cmdPipelineBarrier, "vkCmdPipelineBarrier");
        deviceFunction(cmdCopyBufferToImage, "vkCmdCopyBufferToImage");
        deviceFunction(queueSubmit, "vkQueueSubmit");
        getDeviceQueue(device, queueFamily, 0, &queue);
        auto pool = structure<VkCommandPoolCreateInfo>(VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO);
        pool.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        pool.queueFamilyIndex = queueFamily;
        check(createCommandPool(device, &pool, nullptr, &commandPool), "vkCreateCommandPool");
        auto command = structure<VkCommandBufferAllocateInfo>(VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO);
        command.commandPool = commandPool;
        command.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        command.commandBufferCount = 1;
        check(allocateCommandBuffers(device, &command, &commandBuffer), "vkAllocateCommandBuffers");
        auto semaphore = structure<VkSemaphoreCreateInfo>(VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO);
        check(createSemaphore(device, &semaphore, nullptr, &acquired), "vkCreateSemaphore");
        recreateSwapchain();
    }

    void selectDevice() {
        std::uint32_t count = 0;
        check(enumeratePhysicalDevices(instance, &count, nullptr), "vkEnumeratePhysicalDevices");
        std::vector<VkPhysicalDevice> devices(count);
        check(enumeratePhysicalDevices(instance, &count, devices.data()), "vkEnumeratePhysicalDevices");
        for (auto candidate : devices) {
            VkPhysicalDeviceProperties properties{};
            getPhysicalDeviceProperties(candidate, &properties);
            if (properties.apiVersion < VK_API_VERSION_1_1) continue;
            std::uint32_t extensionCount = 0;
            check(enumerateDeviceExtensions(candidate, nullptr, &extensionCount, nullptr),
                  "vkEnumerateDeviceExtensionProperties");
            std::vector<VkExtensionProperties> extensions(extensionCount);
            check(enumerateDeviceExtensions(candidate, nullptr, &extensionCount, extensions.data()),
                  "vkEnumerateDeviceExtensionProperties");
            if (std::none_of(extensions.begin(), extensions.end(), [](const auto& extension) {
                    return std::strcmp(extension.extensionName, VK_KHR_SWAPCHAIN_EXTENSION_NAME) == 0;
                })) continue;
            std::uint32_t familyCount = 0;
            getQueueFamilyProperties(candidate, &familyCount, nullptr);
            std::vector<VkQueueFamilyProperties> families(familyCount);
            getQueueFamilyProperties(candidate, &familyCount, families.data());
            for (std::uint32_t i = 0; i < familyCount; ++i) {
                VkBool32 supported = VK_FALSE;
                check(getSurfaceSupport(candidate, i, surface, &supported),
                      "vkGetPhysicalDeviceSurfaceSupportKHR");
                if (families[i].queueCount && (families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) && supported) {
                    physical = candidate;
                    queueFamily = i;
                    name = properties.deviceName;
                    return;
                }
            }
        }
        throw std::runtime_error("Nenhuma GPU Vulkan 1.1 com apresentacao Win32 disponivel.");
    }

    void recreateSwapchain() {
        std::uint32_t width = 0, height = 0;
        if (!clientSize(width, height)) return;
        check(deviceWaitIdle(device), "vkDeviceWaitIdle");
        releaseSwapchain();
        VkSurfaceCapabilitiesKHR capabilities{};
        check(getSurfaceCapabilities(physical, surface, &capabilities),
              "vkGetPhysicalDeviceSurfaceCapabilitiesKHR");
        if (!(capabilities.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_DST_BIT)) {
            throw std::runtime_error("Esta superficie Vulkan nao permite copiar o framebuffer.");
        }
        extent = capabilities.currentExtent;
        if (extent.width == std::numeric_limits<std::uint32_t>::max()) {
            extent.width = std::clamp(width, capabilities.minImageExtent.width, capabilities.maxImageExtent.width);
            extent.height = std::clamp(height, capabilities.minImageExtent.height, capabilities.maxImageExtent.height);
        }
        if (!extent.width || !extent.height) return;
        if (std::uint64_t(extent.width) * extent.height > 32ull * 1024 * 1024) {
            throw std::runtime_error("Janela excede o limite de framebuffer do prototipo.");
        }
        std::uint32_t count = 0;
        check(getSurfaceFormats(physical, surface, &count, nullptr), "vkGetPhysicalDeviceSurfaceFormatsKHR");
        if (!count) throw std::runtime_error("Superficie Vulkan sem formatos.");
        std::vector<VkSurfaceFormatKHR> formats(count);
        check(getSurfaceFormats(physical, surface, &count, formats.data()), "vkGetPhysicalDeviceSurfaceFormatsKHR");
        VkSurfaceFormatKHR selected{};
        bool found = false;
        const std::array<VkFormat, 4> preferred{VK_FORMAT_B8G8R8A8_UNORM, VK_FORMAT_R8G8B8A8_UNORM,
                                              VK_FORMAT_B8G8R8A8_SRGB, VK_FORMAT_R8G8B8A8_SRGB};
        if (formats.size() == 1 && formats.front().format == VK_FORMAT_UNDEFINED) {
            selected = {preferred.front(), formats.front().colorSpace};
            found = true;
        }
        for (auto preference : preferred) {
            if (found) break;
            const auto match = std::find_if(formats.begin(), formats.end(), [&](const auto& candidate) {
                return candidate.format == preference;
            });
            if (match != formats.end()) { selected = *match; found = true; }
        }
        if (!found) throw std::runtime_error("Vulkan: nenhum formato RGBA/BGRA de 8 bits suportado.");
        format = selected.format;
        std::uint32_t imageCount = capabilities.minImageCount + 1;
        if (capabilities.maxImageCount && imageCount > capabilities.maxImageCount) imageCount = capabilities.maxImageCount;
        VkCompositeAlphaFlagBitsKHR alpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
        for (const auto candidate : {VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR, VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR,
                                     VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR, VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR}) {
            if (capabilities.supportedCompositeAlpha & candidate) { alpha = candidate; break; }
        }
        auto info = structure<VkSwapchainCreateInfoKHR>(VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR);
        info.surface = surface;
        info.minImageCount = imageCount;
        info.imageFormat = selected.format;
        info.imageColorSpace = selected.colorSpace;
        info.imageExtent = extent;
        info.imageArrayLayers = 1;
        info.imageUsage = VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        info.preTransform = capabilities.currentTransform;
        info.compositeAlpha = alpha;
        info.presentMode = VK_PRESENT_MODE_FIFO_KHR;
        info.clipped = VK_TRUE;
        check(createSwapchain(device, &info, nullptr, &swapchain), "vkCreateSwapchainKHR");
        check(getSwapchainImages(device, swapchain, &count, nullptr), "vkGetSwapchainImagesKHR");
        images.resize(count);
        check(getSwapchainImages(device, swapchain, &count, images.data()), "vkGetSwapchainImagesKHR");
        ready.resize(count, VK_NULL_HANDLE);
        auto semaphore = structure<VkSemaphoreCreateInfo>(VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO);
        for (auto& entry : ready) check(createSemaphore(device, &semaphore, nullptr, &entry), "vkCreateSemaphore");
        auto bufferInfo = structure<VkBufferCreateInfo>(VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO);
        bufferInfo.size = VkDeviceSize(extent.width) * extent.height * sizeof(std::uint32_t);
        bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        check(createBuffer(device, &bufferInfo, nullptr, &upload), "vkCreateBuffer");
        VkMemoryRequirements requirements{};
        getBufferMemoryRequirements(device, upload, &requirements);
        VkPhysicalDeviceMemoryProperties memory{};
        getMemoryProperties(physical, &memory);
        std::uint32_t memoryType = memory.memoryTypeCount;
        const VkMemoryPropertyFlags required = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
        for (std::uint32_t i = 0; i < memory.memoryTypeCount; ++i) {
            if ((requirements.memoryTypeBits & (1u << i)) &&
                (memory.memoryTypes[i].propertyFlags & required) == required) { memoryType = i; break; }
        }
        if (memoryType == memory.memoryTypeCount) throw std::runtime_error("Vulkan: memoria de upload coerente indisponivel.");
        auto allocation = structure<VkMemoryAllocateInfo>(VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO);
        allocation.allocationSize = requirements.size;
        allocation.memoryTypeIndex = memoryType;
        check(allocateMemory(device, &allocation, nullptr, &uploadMemory), "vkAllocateMemory");
        check(bindBufferMemory(device, upload, uploadMemory, 0), "vkBindBufferMemory");
        check(mapMemory(device, uploadMemory, 0, bufferInfo.size, 0, &mapped), "vkMapMemory");
        clientWidth = width;
        clientHeight = height;
    }

    void present(const std::vector<std::uint32_t>& rgba, std::uint32_t width, std::uint32_t height) {
        if (!width || !height || std::uint64_t(width) * height != rgba.size()) {
            throw std::runtime_error("Framebuffer invalido para apresentacao Vulkan.");
        }
        std::uint32_t currentWidth = 0, currentHeight = 0;
        if (!clientSize(currentWidth, currentHeight)) return;
        if (!swapchain || currentWidth != clientWidth || currentHeight != clientHeight) recreateSwapchain();
        if (!swapchain) return;
        std::uint32_t imageIndex = 0;
        const auto acquire = acquireNextImage(device, swapchain, std::numeric_limits<std::uint64_t>::max(),
                                             acquired, VK_NULL_HANDLE, &imageIndex);
        if (acquire == VK_ERROR_OUT_OF_DATE_KHR) { recreateSwapchain(); return; }
        if (acquire != VK_SUBOPTIMAL_KHR) check(acquire, "vkAcquireNextImageKHR");
        auto* destination = static_cast<std::uint32_t*>(mapped);
        const bool bgra = format == VK_FORMAT_B8G8R8A8_UNORM || format == VK_FORMAT_B8G8R8A8_SRGB;
        for (std::uint32_t y = 0; y < extent.height; ++y) {
            const std::size_t sourceRow = std::size_t(std::uint64_t(y) * height / extent.height) * width;
            for (std::uint32_t x = 0; x < extent.width; ++x) {
                auto pixel = rgba[sourceRow + std::size_t(std::uint64_t(x) * width / extent.width)];
                if (bgra) pixel = (pixel & 0xff00ff00u) | ((pixel & 0x000000ffu) << 16) | ((pixel & 0x00ff0000u) >> 16);
                destination[std::size_t(y) * extent.width + x] = pixel;
            }
        }
        check(resetCommandBuffer(commandBuffer, 0), "vkResetCommandBuffer");
        auto begin = structure<VkCommandBufferBeginInfo>(VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO);
        begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        check(beginCommandBuffer(commandBuffer, &begin), "vkBeginCommandBuffer");
        auto barrier = structure<VkImageMemoryBarrier>(VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER);
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = images.at(imageIndex);
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.levelCount = 1;
        barrier.subresourceRange.layerCount = 1;
        cmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                           0, 0, nullptr, 0, nullptr, 1, &barrier);
        VkBufferImageCopy copy{};
        copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        copy.imageSubresource.layerCount = 1;
        copy.imageExtent = {extent.width, extent.height, 1};
        cmdCopyBufferToImage(commandBuffer, upload, images[imageIndex], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = 0;
        barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        cmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                           0, 0, nullptr, 0, nullptr, 1, &barrier);
        check(endCommandBuffer(commandBuffer), "vkEndCommandBuffer");
        const VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        auto submit = structure<VkSubmitInfo>(VK_STRUCTURE_TYPE_SUBMIT_INFO);
        submit.waitSemaphoreCount = 1;
        submit.pWaitSemaphores = &acquired;
        submit.pWaitDstStageMask = &waitStage;
        submit.commandBufferCount = 1;
        submit.pCommandBuffers = &commandBuffer;
        submit.signalSemaphoreCount = 1;
        submit.pSignalSemaphores = &ready[imageIndex];
        check(queueSubmit(queue, 1, &submit, VK_NULL_HANDLE), "vkQueueSubmit");
        auto presentInfo = structure<VkPresentInfoKHR>(VK_STRUCTURE_TYPE_PRESENT_INFO_KHR);
        presentInfo.waitSemaphoreCount = 1;
        presentInfo.pWaitSemaphores = &ready[imageIndex];
        presentInfo.swapchainCount = 1;
        presentInfo.pSwapchains = &swapchain;
        presentInfo.pImageIndices = &imageIndex;
        const auto result = queuePresent(queue, &presentInfo);
        check(queueWaitIdle(queue), "vkQueueWaitIdle");
        if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR || acquire == VK_SUBOPTIMAL_KHR) {
            if (result != VK_ERROR_OUT_OF_DATE_KHR && result != VK_SUBOPTIMAL_KHR) check(result, "vkQueuePresentKHR");
            recreateSwapchain();
        } else {
            check(result, "vkQueuePresentKHR");
        }
    }
};

VulkanPresenter::VulkanPresenter(void* hwnd) : impl_(std::make_unique<Impl>()) {
    impl_->initialize(hwnd);
}
VulkanPresenter::~VulkanPresenter() = default;
void VulkanPresenter::present(const std::vector<std::uint32_t>& rgba, std::uint32_t width, std::uint32_t height) {
    impl_->present(rgba, width, height);
}
std::string VulkanPresenter::deviceName() const { return impl_->name; }
}
#endif
