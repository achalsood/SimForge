#pragma once
#include <simforge/platform/vulkan_window.hpp>
#include <simforge/renderer/renderer.hpp>
#include <atomic>

namespace simforge::renderer::detail
{
struct Diagnostics
{
    std::atomic<std::uint32_t> errors{0}, warnings{0};
};
class VulkanContext
{
public:
    VulkanContext(const platform::Window& window, RendererOptions options, Diagnostics& diagnostics);
    ~VulkanContext();
    VulkanContext(const VulkanContext&) = delete;
    VulkanContext& operator=(const VulkanContext&) = delete;
    [[nodiscard]] VkInstance instance() const noexcept { return instance_; }
    [[nodiscard]] VkSurfaceKHR surface() const noexcept { return surface_; }
    [[nodiscard]] platform::PixelSize pixels() const { return window_.pixel_size(); }
    [[nodiscard]] bool validation() const noexcept { return validation_; }
    [[nodiscard]] bool synchronization_validation() const noexcept { return sync_validation_; }
    [[nodiscard]] bool khr_maintenance() const noexcept { return khr_maintenance_; }
    [[nodiscard]] bool ext_maintenance() const noexcept { return ext_maintenance_; }
    [[nodiscard]] std::uint32_t loader_version() const noexcept { return loader_version_; }
private:
    void destroy() noexcept;
    platform::VulkanWindow window_; // Released after instance/surface destruction.
    VkInstance instance_{VK_NULL_HANDLE};
    VkSurfaceKHR surface_{VK_NULL_HANDLE};
    VkDebugUtilsMessengerEXT messenger_{VK_NULL_HANDLE};
    PFN_vkDestroyDebugUtilsMessengerEXT destroy_messenger_{nullptr};
    std::uint32_t loader_version_{VK_API_VERSION_1_0};
    bool validation_{false}, sync_validation_{false}, khr_maintenance_{false}, ext_maintenance_{false};
};
}
