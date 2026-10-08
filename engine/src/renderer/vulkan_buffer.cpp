#include "vulkan_buffer.hpp"
#include "vulkan_support.hpp"
#include <cstring>
#include <limits>

namespace simforge::renderer::detail
{
VulkanBuffer::VulkanBuffer(const VulkanDevice& device, VkDeviceSize size, VkBufferUsageFlags usage)
    : device_{device}, size_{size}
{
    if (size == 0 || size > std::numeric_limits<std::size_t>::max()) throw std::invalid_argument("Invalid GPU buffer size");
    try
    {
        VkBufferCreateInfo info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        info.size = size; info.usage = usage; info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        check(vkCreateBuffer(device.handle(),&info,nullptr,&buffer_),"Create host-visible buffer");
        VkMemoryRequirements requirements{}; vkGetBufferMemoryRequirements(device.handle(),buffer_,&requirements);
        VkPhysicalDeviceMemoryProperties properties{}; vkGetPhysicalDeviceMemoryProperties(device.physical(),&properties);
        const auto type = choose_memory_type(properties,requirements.memoryTypeBits,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        if (!type) throw std::runtime_error("No host-visible memory for GPU buffer");
        coherent_ = (properties.memoryTypes[*type].propertyFlags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) != 0;
        VkMemoryAllocateInfo allocate{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        allocate.allocationSize = requirements.size; allocate.memoryTypeIndex = *type;
        check(vkAllocateMemory(device.handle(),&allocate,nullptr,&memory_),"Allocate host-visible buffer memory");
        check(vkBindBufferMemory(device.handle(),buffer_,memory_,0),"Bind GPU buffer memory");
        void* mapping = nullptr;
        check(vkMapMemory(device.handle(),memory_,0,VK_WHOLE_SIZE,0,&mapping),"Map GPU buffer memory");
        mapped_ = mapping; // Mark mapped only after a successful map operation.
    }
    catch (...) { destroy(); throw; }
}
VulkanBuffer::~VulkanBuffer() { destroy(); }
void VulkanBuffer::upload(std::span<const std::byte> bytes)
{
    if (bytes.size() > size_) throw std::invalid_argument("GPU buffer upload exceeds allocation");
    if (bytes.empty()) return;
    std::memcpy(mapped_,bytes.data(),bytes.size());
    if (!coherent_)
    {
        VkMappedMemoryRange range{VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE}; range.memory = memory_; range.size = VK_WHOLE_SIZE;
        check(vkFlushMappedMemoryRanges(device_.handle(),1,&range),"Flush noncoherent buffer upload");
    }
}
std::span<const std::byte> VulkanBuffer::read()
{
    if (!coherent_)
    {
        VkMappedMemoryRange range{VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE}; range.memory = memory_; range.size = VK_WHOLE_SIZE;
        check(vkInvalidateMappedMemoryRanges(device_.handle(),1,&range),"Invalidate noncoherent buffer readback");
    }
    return {static_cast<const std::byte*>(mapped_),static_cast<std::size_t>(size_)};
}
void VulkanBuffer::destroy() noexcept
{
    if (mapped_) vkUnmapMemory(device_.handle(),memory_);
    if (buffer_) vkDestroyBuffer(device_.handle(),buffer_,nullptr);
    if (memory_) vkFreeMemory(device_.handle(),memory_,nullptr);
    mapped_ = nullptr; buffer_ = VK_NULL_HANDLE; memory_ = VK_NULL_HANDLE;
}
}
