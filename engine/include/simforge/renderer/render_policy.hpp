#pragma once
#include <vulkan/vulkan_core.h>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace simforge::renderer
{
struct QueueSupport { std::uint32_t count{0}; bool graphics{false}, present{false}; };
struct QueueFamilies
{
    std::uint32_t graphics, present;
    [[nodiscard]] bool separate() const noexcept { return graphics != present; }
    bool operator==(const QueueFamilies&) const = default;
};
struct DeviceSupport
{
    VkPhysicalDeviceType type{VK_PHYSICAL_DEVICE_TYPE_OTHER};
    std::uint32_t api_version{VK_API_VERSION_1_1};
    bool swapchain_extension{false}, present_fences{false}, color_attachment_usage{false};
    std::vector<QueueSupport> queues;
    std::vector<VkSurfaceFormatKHR> formats;
    std::vector<VkPresentModeKHR> present_modes;
};
[[nodiscard]] std::optional<QueueFamilies> choose_queues(std::span<const QueueSupport> queues);
[[nodiscard]] std::string device_rejection(const DeviceSupport& support);
[[nodiscard]] std::optional<std::size_t> choose_device(std::span<const DeviceSupport> devices);
[[nodiscard]] std::uint32_t choose_image_count(const VkSurfaceCapabilitiesKHR& capabilities);
[[nodiscard]] std::optional<VkExtent2D> choose_extent(const VkSurfaceCapabilitiesKHR& capabilities, std::uint32_t width, std::uint32_t height);
[[nodiscard]] VkSurfaceFormatKHR choose_format(std::span<const VkSurfaceFormatKHR> formats);
[[nodiscard]] VkPresentModeKHR choose_present_mode(std::span<const VkPresentModeKHR> modes);
[[nodiscard]] VkCompositeAlphaFlagBitsKHR choose_composite_alpha(VkCompositeAlphaFlagsKHR supported);
}
