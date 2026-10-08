#include "vulkan_device.hpp"
#include "vulkan_support.hpp"
#include <algorithm>
#include <cstring>
#include <iostream>

namespace simforge::renderer::detail
{
VulkanDevice::VulkanDevice(const VulkanContext& context)
{
    const auto devices = enumerate<VkPhysicalDevice>([&](auto count, auto values) { return vkEnumeratePhysicalDevices(context.instance(),count,values); },"Enumerate physical GPUs");
    std::vector<DeviceSupport> supports;
    std::vector<bool> khr_maintenance;
    for (auto physical : devices)
    {
        DeviceSupport support; VkPhysicalDeviceProperties properties{}; vkGetPhysicalDeviceProperties(physical,&properties);
        support.type = properties.deviceType; support.api_version = properties.apiVersion;
        const auto extensions = enumerate<VkExtensionProperties>([&](auto count, auto values) { return vkEnumerateDeviceExtensionProperties(physical,nullptr,count,values); },"Enumerate GPU extensions");
        const auto has = [&](const char* name) { return std::any_of(extensions.begin(),extensions.end(),[&](const auto& e) { return std::strcmp(e.extensionName,name) == 0; }); };
        support.swapchain_extension = has(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
        const bool device_khr = has(VK_KHR_SWAPCHAIN_MAINTENANCE_1_EXTENSION_NAME);
        const bool device_ext = has(VK_EXT_SWAPCHAIN_MAINTENANCE_1_EXTENSION_NAME);
        const bool khr = context.khr_maintenance() && has(VK_KHR_SWAPCHAIN_MAINTENANCE_1_EXTENSION_NAME);
        const bool ext = context.ext_maintenance() && has(VK_EXT_SWAPCHAIN_MAINTENANCE_1_EXTENSION_NAME);
        khr_maintenance.push_back(khr);
        if (properties.apiVersion >= VK_API_VERSION_1_1 && (khr || ext))
        {
            VkPhysicalDeviceSwapchainMaintenance1FeaturesKHR maintenance{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SWAPCHAIN_MAINTENANCE_1_FEATURES_KHR};
            VkPhysicalDeviceFeatures2 features{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2}; features.pNext = &maintenance;
            vkGetPhysicalDeviceFeatures2(physical,&features); support.present_fences = maintenance.swapchainMaintenance1 == VK_TRUE;
        }
        for (auto format : {VK_FORMAT_D32_SFLOAT,VK_FORMAT_D32_SFLOAT_S8_UINT,VK_FORMAT_D24_UNORM_S8_UINT})
        {
            VkFormatProperties depth{}; vkGetPhysicalDeviceFormatProperties(physical,format,&depth);
            support.depth_formats.push_back({format,depth.optimalTilingFeatures});
        }
        std::uint32_t count = 0; vkGetPhysicalDeviceQueueFamilyProperties(physical,&count,nullptr);
        std::vector<VkQueueFamilyProperties> queues(count); vkGetPhysicalDeviceQueueFamilyProperties(physical,&count,queues.data());
        for (std::uint32_t i = 0; i < count; ++i)
        {
            VkBool32 presentation = VK_FALSE;
            check(vkGetPhysicalDeviceSurfaceSupportKHR(physical,i,context.surface(),&presentation),"Query surface presentation support");
            support.queues.push_back({queues[i].queueCount,(queues[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0,presentation == VK_TRUE});
        }
        // Surface capability queries require a physical device that supports
        // this surface. Reject missing queue support without making those calls.
        if (choose_queues(support.queues))
        {
            support.formats = color_formats(physical,context.surface());
            support.present_modes = enumerate<VkPresentModeKHR>([&](auto n, auto values) { return vkGetPhysicalDeviceSurfacePresentModesKHR(physical,context.surface(),n,values); },"Query presentation modes");
            VkSurfaceCapabilitiesKHR capabilities{};
            check(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical,context.surface(),&capabilities),"Query surface capabilities");
            support.color_attachment_usage = (capabilities.supportedUsageFlags & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT) != 0;
        }
        const auto rejection = device_rejection(support);
        if (properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU)
            std::cout << "Vulkan integrated GPU visible: " << properties.deviceName
                << " | Graphics/present queues: " << (choose_queues(support.queues) ? "available" : "unavailable")
                << " | Swapchain: " << (support.swapchain_extension ? "yes" : "no")
                << " | Maintenance1 KHR/EXT: " << device_khr << '/' << device_ext
                << " | Present-fence feature: " << support.present_fences << '\n';
        if (!rejection.empty()) std::cout << "Vulkan GPU rejected: " << properties.deviceName << ": " << rejection << '\n';
        supports.push_back(std::move(support));
    }
    const auto selected = choose_device(supports);
    if (!selected) throw std::runtime_error("No suitable Vulkan GPU: require Vulkan 1.1, graphics/present queues, swapchain color support and maintenance1 present fences");
    physical_ = devices[*selected]; families_ = *choose_queues(supports[*selected].queues); vkGetPhysicalDeviceProperties(physical_,&properties_);
    depth_format_ = *choose_depth_format(supports[*selected].depth_formats);
    const float priority = 1;
    std::vector<VkDeviceQueueCreateInfo> queue_infos;
    for (auto family : {families_.graphics,families_.present})
        if (queue_infos.empty() || queue_infos.front().queueFamilyIndex != family)
        {
            VkDeviceQueueCreateInfo info{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
            info.queueFamilyIndex = family; info.queueCount = 1; info.pQueuePriorities = &priority; queue_infos.push_back(info);
        }
    VkPhysicalDeviceSwapchainMaintenance1FeaturesKHR maintenance{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SWAPCHAIN_MAINTENANCE_1_FEATURES_KHR};
    maintenance.swapchainMaintenance1 = VK_TRUE;
    const auto maintenance_name = khr_maintenance[*selected] ? VK_KHR_SWAPCHAIN_MAINTENANCE_1_EXTENSION_NAME : VK_EXT_SWAPCHAIN_MAINTENANCE_1_EXTENSION_NAME;
    const char* extensions[]{VK_KHR_SWAPCHAIN_EXTENSION_NAME,maintenance_name};
    VkDeviceCreateInfo info{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO}; info.pNext = &maintenance;
    info.queueCreateInfoCount = static_cast<std::uint32_t>(queue_infos.size()); info.pQueueCreateInfos = queue_infos.data();
    info.enabledExtensionCount = 2; info.ppEnabledExtensionNames = extensions;
    check(vkCreateDevice(physical_,&info,nullptr,&device_),"Create logical Vulkan device");
    vkGetDeviceQueue(device_,families_.graphics,0,&graphics_); vkGetDeviceQueue(device_,families_.present,0,&present_);
}
VulkanDevice::~VulkanDevice() { if (device_) vkDestroyDevice(device_,nullptr); }
}
