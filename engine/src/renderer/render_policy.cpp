#include <simforge/renderer/render_policy.hpp>
#include <algorithm>
#include <limits>
#include <stdexcept>
#include <cmath>

namespace simforge::renderer
{
std::optional<QueueFamilies> choose_queues(std::span<const QueueSupport> queues)
{
    std::optional<std::uint32_t> graphics, present;
    for (std::uint32_t i = 0; i < queues.size(); ++i)
    {
        const auto& queue = queues[i]; if (queue.count == 0) continue;
        if (queue.graphics && queue.present) return QueueFamilies{i,i};
        if (queue.graphics && !graphics) graphics = i;
        if (queue.present && !present) present = i;
    }
    if (!graphics || !present) return {};
    return QueueFamilies{*graphics,*present};
}
std::string device_rejection(const DeviceSupport& support)
{
    if (support.api_version < VK_API_VERSION_1_1) return "Vulkan 1.1 is required";
    if (!choose_queues(support.queues)) return "graphics/presentation queue families are unavailable";
    if (!support.swapchain_extension) return "VK_KHR_swapchain is missing";
    if (!support.present_fences) return "swapchain maintenance1 present fences are required for safe retirement; update the driver";
    if (support.formats.empty()) return "surface formats are unavailable";
    if (support.present_modes.empty()) return "presentation modes are unavailable";
    if (!support.color_attachment_usage) return "swapchain color attachments are unsupported";
    if (!choose_depth_format(support.depth_formats)) return "supported depth attachments are unavailable";
    return {};
}
std::optional<std::size_t> choose_device(std::span<const DeviceSupport> devices)
{
    std::optional<std::size_t> best; int score = -1;
    for (std::size_t i = 0; i < devices.size(); ++i)
    {
        if (!device_rejection(devices[i]).empty()) continue;
        const auto type = devices[i].type;
        const int candidate = type == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU ? 3 : type == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU ? 2 : 1;
        if (candidate > score) { best = i; score = candidate; }
    }
    return best;
}
std::uint32_t choose_image_count(const VkSurfaceCapabilitiesKHR& c)
{
    if (c.minImageCount == 0 || (c.maxImageCount != 0 && c.maxImageCount < c.minImageCount))
        throw std::invalid_argument("Invalid swapchain image count limits");
    auto count = c.minImageCount;
    if (count != std::numeric_limits<std::uint32_t>::max()) ++count;
    if (c.maxImageCount != 0) count = std::min(count,c.maxImageCount);
    return count;
}
std::optional<VkExtent2D> choose_extent(const VkSurfaceCapabilitiesKHR& c, std::uint32_t width, std::uint32_t height)
{
    if (width == 0 || height == 0) return {};
    if (c.minImageExtent.width > c.maxImageExtent.width || c.minImageExtent.height > c.maxImageExtent.height)
        throw std::invalid_argument("Invalid surface extent limits");
    if ((c.currentExtent.width == UINT32_MAX) != (c.currentExtent.height == UINT32_MAX))
        throw std::invalid_argument("Inconsistent fixed/variable surface extent");
    if (c.currentExtent.width != UINT32_MAX)
    {
        if (c.currentExtent.width == 0 || c.currentExtent.height == 0) return {};
        return c.currentExtent;
    }
    const VkExtent2D extent{std::clamp(width,c.minImageExtent.width,c.maxImageExtent.width),
        std::clamp(height,c.minImageExtent.height,c.maxImageExtent.height)};
    if (extent.width == 0 || extent.height == 0) return {};
    return extent;
}
VkSurfaceFormatKHR choose_format(std::span<const VkSurfaceFormatKHR> formats)
{
    if (formats.empty()) throw std::invalid_argument("No swapchain surface formats");
    if (formats.size() == 1 && formats[0].format == VK_FORMAT_UNDEFINED)
        return {VK_FORMAT_B8G8R8A8_SRGB,formats[0].colorSpace};
    for (auto desired : {VK_FORMAT_B8G8R8A8_SRGB,VK_FORMAT_R8G8B8A8_SRGB,VK_FORMAT_B8G8R8A8_UNORM})
        for (const auto& format : formats)
            if (format.format == desired && format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) return format;
    return formats[0];
}
VkPresentModeKHR choose_present_mode(std::span<const VkPresentModeKHR> modes)
{
    if (modes.empty()) throw std::invalid_argument("No swapchain presentation modes");
    if (std::find(modes.begin(),modes.end(),VK_PRESENT_MODE_FIFO_KHR) != modes.end()) return VK_PRESENT_MODE_FIFO_KHR;
    return modes[0];
}
VkCompositeAlphaFlagBitsKHR choose_composite_alpha(VkCompositeAlphaFlagsKHR supported)
{
    for (auto mode : {VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR,
        VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR,VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR})
        if ((supported & mode) != 0) return mode;
    throw std::invalid_argument("No supported swapchain composite alpha mode");
}
std::optional<VkFormat> choose_depth_format(std::span<const DepthFormatSupport> formats)
{
    for (auto desired : {VK_FORMAT_D32_SFLOAT,VK_FORMAT_D32_SFLOAT_S8_UINT,VK_FORMAT_D24_UNORM_S8_UINT})
        for (const auto& support : formats)
            if (support.format == desired && (support.optimal_features & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) != 0)
                return desired;
    return {};
}
bool pipeline_recreation_required(std::optional<PipelineSignature> current, PipelineSignature next) noexcept
{ return !current || *current != next; } // Viewport/scissor are dynamic; extent does not belong in the signature.
std::optional<std::uint32_t> choose_memory_type(const VkPhysicalDeviceMemoryProperties& memory,
    std::uint32_t bits, VkMemoryPropertyFlags required, VkMemoryPropertyFlags preferred)
{
    if (memory.memoryTypeCount > VK_MAX_MEMORY_TYPES) throw std::invalid_argument("Invalid memory type count");
    std::optional<std::uint32_t> fallback;
    for (std::uint32_t index = 0; index < memory.memoryTypeCount; ++index)
    {
        const auto flags = memory.memoryTypes[index].propertyFlags;
        if ((bits & (1u << index)) == 0 || (flags & required) != required) continue;
        if ((flags & preferred) == preferred) return index;
        if (!fallback) fallback = index;
    }
    return fallback;
}
bool requires_srgb_encoding(VkFormat format) noexcept
{ return format == VK_FORMAT_B8G8R8A8_UNORM || format == VK_FORMAT_R8G8B8A8_UNORM; }
std::array<float,4> scene_clear_color(VkFormat format)
{
    std::array color{0.025f,0.045f,0.075f,1.0f};
    if (requires_srgb_encoding(format))
        for (std::size_t i = 0; i < 3; ++i)
            color[i] = 1.055f*std::pow(color[i],1.0f/2.4f)-0.055f;
    return color;
}
}
