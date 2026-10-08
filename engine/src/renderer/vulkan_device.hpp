#pragma once
#include "vulkan_context.hpp"
#include <simforge/renderer/render_policy.hpp>

namespace simforge::renderer::detail
{
class VulkanDevice
{
public:
    explicit VulkanDevice(const VulkanContext& context);
    ~VulkanDevice();
    VulkanDevice(const VulkanDevice&) = delete;
    VulkanDevice& operator=(const VulkanDevice&) = delete;
    [[nodiscard]] VkDevice handle() const noexcept { return device_; }
    [[nodiscard]] VkPhysicalDevice physical() const noexcept { return physical_; }
    [[nodiscard]] VkQueue graphics() const noexcept { return graphics_; }
    [[nodiscard]] VkQueue present() const noexcept { return present_; }
    [[nodiscard]] QueueFamilies families() const noexcept { return families_; }
    [[nodiscard]] const VkPhysicalDeviceProperties& properties() const noexcept { return properties_; }
    [[nodiscard]] VkFormat depth_format() const noexcept { return depth_format_; }
private:
    VkPhysicalDevice physical_{VK_NULL_HANDLE};
    VkPhysicalDeviceProperties properties_{};
    VkDevice device_{VK_NULL_HANDLE};
    VkQueue graphics_{VK_NULL_HANDLE}, present_{VK_NULL_HANDLE};
    QueueFamilies families_{};
    VkFormat depth_format_{VK_FORMAT_UNDEFINED};
};
}
