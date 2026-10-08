#pragma once
#include "vulkan_device.hpp"
#include <cstddef>
#include <span>

namespace simforge::renderer::detail
{
// Small explicitly owned host-visible buffer; no allocator or staging framework.
class VulkanBuffer
{
public:
    VulkanBuffer(const VulkanDevice& device, VkDeviceSize size, VkBufferUsageFlags usage);
    ~VulkanBuffer();
    VulkanBuffer(const VulkanBuffer&) = delete;
    VulkanBuffer& operator=(const VulkanBuffer&) = delete;
    void upload(std::span<const std::byte> bytes);
    [[nodiscard]] std::span<const std::byte> read(); // Caller must wait for GPU completion first.
    [[nodiscard]] VkBuffer handle() const noexcept { return buffer_; }
private:
    void destroy() noexcept;
    const VulkanDevice& device_;
    VkBuffer buffer_{VK_NULL_HANDLE};
    VkDeviceMemory memory_{VK_NULL_HANDLE};
    void* mapped_{nullptr};
    VkDeviceSize size_;
    bool coherent_{false};
};
}
