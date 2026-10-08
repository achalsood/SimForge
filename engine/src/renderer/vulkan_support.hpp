#pragma once
#include <vulkan/vulkan.h>
#include <stdexcept>
#include <string>
#include <vector>
#include <algorithm>
#include <utility>

namespace simforge::renderer::detail
{
class VulkanError : public std::runtime_error
{
public:
    VulkanError(VkResult result, std::string text) : std::runtime_error(std::move(text)), result_{result} {}
    [[nodiscard]] VkResult result() const noexcept { return result_; }
private:
    VkResult result_;
};
inline void check(VkResult result, const char* operation)
{
    if (result == VK_SUCCESS) return;
    const auto reason = result == VK_ERROR_DEVICE_LOST ? "device lost" :
        result == VK_ERROR_OUT_OF_HOST_MEMORY || result == VK_ERROR_OUT_OF_DEVICE_MEMORY ? "allocation failed" :
        result == VK_ERROR_SURFACE_LOST_KHR ? "surface lost" : "Vulkan operation failed";
    throw VulkanError(result,std::string{operation}+": "+reason+" (VkResult "+std::to_string(result)+")");
}
template<class T, class Function> std::vector<T> enumerate(Function function, const char* operation)
{
    for (;;)
    {
        std::uint32_t count = 0; check(function(&count,nullptr),operation);
        std::vector<T> values(count);
        if (count == 0) return values;
        const auto result = function(&count,values.data());
        if (result == VK_INCOMPLETE) continue;
        check(result,operation); values.resize(count); return values;
    }
}
inline std::vector<VkSurfaceFormatKHR> color_formats(VkPhysicalDevice physical, VkSurfaceKHR surface)
{
    auto formats = enumerate<VkSurfaceFormatKHR>([&](auto n, auto values) {
        return vkGetPhysicalDeviceSurfaceFormatsKHR(physical,surface,n,values);
    },"Query color surface formats");
    if (formats.size() == 1 && formats.front().format == VK_FORMAT_UNDEFINED)
    {
        const auto space = formats.front().colorSpace;
        formats = {{VK_FORMAT_B8G8R8A8_SRGB,space},{VK_FORMAT_R8G8B8A8_SRGB,space},{VK_FORMAT_B8G8R8A8_UNORM,space}};
    }
    std::erase_if(formats,[&](const auto& format) {
        VkFormatProperties properties{};
        vkGetPhysicalDeviceFormatProperties(physical,format.format,&properties);
        return (properties.optimalTilingFeatures & VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT) == 0;
    });
    return formats;
}
}
