#pragma once
#include "vulkan_device.hpp"
#include "vulkan_buffer.hpp"
#include "vulkan_scene.hpp"

namespace simforge::renderer::detail
{
class VulkanSwapchain
{
public:
    VulkanSwapchain(const VulkanContext& context, const VulkanDevice& device, VkExtent2D extent, VkSwapchainKHR old, bool capture);
    ~VulkanSwapchain();
    VulkanSwapchain(const VulkanSwapchain&) = delete;
    VulkanSwapchain& operator=(const VulkanSwapchain&) = delete;
    void wait_presentations();
    [[nodiscard]] DrawCounts record(VkCommandBuffer command, std::uint32_t image, const VulkanScene& scene, const RenderFrameData& frame) const;
    [[nodiscard]] CapturedFrame read_pixels();
    [[nodiscard]] VkResult acquire(VkSemaphore acquired, VkFence fence, std::uint32_t& image) const;
    [[nodiscard]] VkResult present(std::uint32_t image);
    [[nodiscard]] VkSemaphore rendered(std::uint32_t image) const { return rendered_.at(image); }
    [[nodiscard]] VkSwapchainKHR handle() const noexcept { return swapchain_; }
    [[nodiscard]] VkExtent2D extent() const noexcept { return extent_; }
    [[nodiscard]] VkSurfaceFormatKHR format() const noexcept { return format_; }
    [[nodiscard]] VkPresentModeKHR mode() const noexcept { return mode_; }
    [[nodiscard]] VkRenderPass render_pass() const noexcept { return pass_; }
    [[nodiscard]] std::uint32_t image_count() const noexcept { return static_cast<std::uint32_t>(images_.size()); }
    void abandon_waits() noexcept { lost_ = true; }
private:
    void destroy() noexcept;
    const VulkanDevice& device_;
    VkSwapchainKHR swapchain_{VK_NULL_HANDLE};
    VkRenderPass pass_{VK_NULL_HANDLE};
    VkExtent2D extent_{};
    VkSurfaceFormatKHR format_{};
    VkPresentModeKHR mode_{VK_PRESENT_MODE_FIFO_KHR};
    std::vector<VkImage> images_;
    std::vector<VkImageView> views_;
    std::vector<VkFramebuffer> framebuffers_;
    std::vector<VkSemaphore> rendered_;
    std::vector<VkFence> present_fences_;
    std::vector<bool> presentation_pending_;
    bool lost_{false};
    VkImage depth_image_{VK_NULL_HANDLE};
    VkDeviceMemory depth_memory_{VK_NULL_HANDLE};
    VkImageView depth_view_{VK_NULL_HANDLE};
    std::unique_ptr<VulkanBuffer> readback_;
};
}
