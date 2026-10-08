#pragma once
#include <vulkan/vulkan_core.h>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>
#include <array>

namespace simforge::renderer
{
struct QueueSupport { std::uint32_t count{0}; bool graphics{false}, present{false}; };
struct QueueFamilies
{
    std::uint32_t graphics, present;
    [[nodiscard]] bool separate() const noexcept { return graphics != present; }
    bool operator==(const QueueFamilies&) const = default;
};
struct DepthFormatSupport { VkFormat format; VkFormatFeatureFlags optimal_features; };
struct PipelineSignature
{
    VkFormat color, depth;
    bool operator==(const PipelineSignature&) const = default;
};
struct DeviceSupport
{
    VkPhysicalDeviceType type{VK_PHYSICAL_DEVICE_TYPE_OTHER};
    std::uint32_t api_version{VK_API_VERSION_1_1};
    bool swapchain_extension{false}, present_fences{false}, color_attachment_usage{false};
    std::vector<QueueSupport> queues;
    std::vector<VkSurfaceFormatKHR> formats;
    std::vector<VkPresentModeKHR> present_modes;
    std::vector<DepthFormatSupport> depth_formats;
};
[[nodiscard]] std::optional<QueueFamilies> choose_queues(std::span<const QueueSupport> queues);
[[nodiscard]] std::string device_rejection(const DeviceSupport& support);
[[nodiscard]] std::optional<std::size_t> choose_device(std::span<const DeviceSupport> devices);
[[nodiscard]] std::uint32_t choose_image_count(const VkSurfaceCapabilitiesKHR& capabilities);
[[nodiscard]] std::optional<VkExtent2D> choose_extent(const VkSurfaceCapabilitiesKHR& capabilities, std::uint32_t width, std::uint32_t height);
[[nodiscard]] VkSurfaceFormatKHR choose_format(std::span<const VkSurfaceFormatKHR> formats);
[[nodiscard]] VkPresentModeKHR choose_present_mode(std::span<const VkPresentModeKHR> modes);
[[nodiscard]] VkCompositeAlphaFlagBitsKHR choose_composite_alpha(VkCompositeAlphaFlagsKHR supported);
[[nodiscard]] std::optional<VkFormat> choose_depth_format(std::span<const DepthFormatSupport> formats);
[[nodiscard]] bool pipeline_recreation_required(std::optional<PipelineSignature> current, PipelineSignature next) noexcept;
[[nodiscard]] std::optional<std::uint32_t> choose_memory_type(const VkPhysicalDeviceMemoryProperties& memory,
    std::uint32_t bits, VkMemoryPropertyFlags required, VkMemoryPropertyFlags preferred);
[[nodiscard]] bool requires_srgb_encoding(VkFormat format) noexcept;
[[nodiscard]] std::array<float,4> scene_clear_color(VkFormat format);
}
