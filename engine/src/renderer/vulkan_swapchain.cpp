#include "vulkan_swapchain.hpp"
#include <imgui_impl_vulkan.h>
#include "vulkan_support.hpp"
#include <cstdio>
#include <array>
#include <limits>

namespace simforge::renderer::detail
{
VulkanSwapchain::VulkanSwapchain(const VulkanContext& context, const VulkanDevice& device, VkExtent2D extent, VkSwapchainKHR old, bool capture)
    : device_{device}, extent_{extent}
{
    try
    {
        VkSurfaceCapabilitiesKHR capabilities{};
        check(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device.physical(),context.surface(),&capabilities),"Query swapchain capabilities");
        const auto formats = color_formats(device.physical(),context.surface());
        const auto modes = enumerate<VkPresentModeKHR>([&](auto n, auto values) { return vkGetPhysicalDeviceSurfacePresentModesKHR(device.physical(),context.surface(),n,values); },"Query swapchain modes");
        format_ = choose_format(formats); mode_ = choose_present_mode(modes);
        if (capture)
        {
            const bool rgba8 = format_.format == VK_FORMAT_R8G8B8A8_SRGB || format_.format == VK_FORMAT_R8G8B8A8_UNORM;
            const bool bgra8 = format_.format == VK_FORMAT_B8G8R8A8_SRGB || format_.format == VK_FORMAT_B8G8R8A8_UNORM;
            if ((!rgba8 && !bgra8) || format_.colorSpace != VK_COLOR_SPACE_SRGB_NONLINEAR_KHR ||
                (capabilities.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_SRC_BIT) == 0)
                throw std::runtime_error("Diagnostic capture requires RGBA/BGRA8 sRGB color space and surface TRANSFER_SRC support");
        }
        if ((capabilities.supportedUsageFlags & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT) == 0) throw std::runtime_error("Surface no longer supports color attachments");
        const auto queues = device.families(); const std::uint32_t indices[]{queues.graphics,queues.present};
        VkSwapchainCreateInfoKHR info{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR}; info.surface = context.surface();
        info.minImageCount = choose_image_count(capabilities); info.imageFormat = format_.format; info.imageColorSpace = format_.colorSpace;
        info.imageExtent = extent_; info.imageArrayLayers = 1; info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        if (capture) info.imageUsage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        info.imageSharingMode = queues.separate() ? VK_SHARING_MODE_CONCURRENT : VK_SHARING_MODE_EXCLUSIVE;
        if (queues.separate()) { info.queueFamilyIndexCount = 2; info.pQueueFamilyIndices = indices; }
        info.preTransform = capabilities.currentTransform; info.compositeAlpha = choose_composite_alpha(capabilities.supportedCompositeAlpha);
        info.presentMode = mode_; info.clipped = VK_TRUE; info.oldSwapchain = old;
        check(vkCreateSwapchainKHR(device.handle(),&info,nullptr,&swapchain_),"Create swapchain");
        images_ = enumerate<VkImage>([&](auto n, auto values) { return vkGetSwapchainImagesKHR(device.handle(),swapchain_,n,values); },"Get swapchain images");
        views_.resize(images_.size(),VK_NULL_HANDLE); framebuffers_.resize(images_.size(),VK_NULL_HANDLE);
        rendered_.resize(images_.size(),VK_NULL_HANDLE); present_fences_.resize(images_.size(),VK_NULL_HANDLE);
        presentation_pending_.resize(images_.size(),false);
        VkAttachmentDescription attachment{}; attachment.format = format_.format; attachment.samples = VK_SAMPLE_COUNT_1_BIT;
        attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR; attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE; attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED; attachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        VkAttachmentDescription depth{}; depth.format = device.depth_format(); depth.samples = VK_SAMPLE_COUNT_1_BIT;
        depth.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR; depth.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        depth.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE; depth.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        depth.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED; depth.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        const std::array attachments{attachment,depth};
        const VkAttachmentReference color{0,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
        const VkAttachmentReference depth_reference{1,VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
        VkSubpassDescription subpass{}; subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS; subpass.colorAttachmentCount = 1; subpass.pColorAttachments = &color;
        subpass.pDepthStencilAttachment = &depth_reference;
        VkSubpassDependency dependency{}; dependency.srcSubpass = VK_SUBPASS_EXTERNAL; dependency.dstSubpass = 0;
        dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
        dependency.dstStageMask = dependency.srcStageMask;
        dependency.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        VkRenderPassCreateInfo render_pass{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO}; render_pass.attachmentCount = 2; render_pass.pAttachments = attachments.data();
        render_pass.subpassCount = 1; render_pass.pSubpasses = &subpass; render_pass.dependencyCount = 1; render_pass.pDependencies = &dependency;
        check(vkCreateRenderPass(device.handle(),&render_pass,nullptr,&pass_),"Create clear render pass");
        VkImageCreateInfo depth_image{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO}; depth_image.imageType = VK_IMAGE_TYPE_2D;
        depth_image.format = device.depth_format(); depth_image.extent = {extent.width,extent.height,1};
        depth_image.mipLevels = depth_image.arrayLayers = 1; depth_image.samples = VK_SAMPLE_COUNT_1_BIT;
        depth_image.tiling = VK_IMAGE_TILING_OPTIMAL; depth_image.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
        depth_image.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        check(vkCreateImage(device.handle(),&depth_image,nullptr,&depth_image_),"Create depth attachment image");
        VkMemoryRequirements requirements{}; vkGetImageMemoryRequirements(device.handle(),depth_image_,&requirements);
        VkPhysicalDeviceMemoryProperties memory{}; vkGetPhysicalDeviceMemoryProperties(device.physical(),&memory);
        const auto memory_type = choose_memory_type(memory,requirements.memoryTypeBits,0,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        if (!memory_type) throw std::runtime_error("No compatible memory type for depth image");
        VkMemoryAllocateInfo allocate{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        allocate.allocationSize = requirements.size; allocate.memoryTypeIndex = *memory_type;
        check(vkAllocateMemory(device.handle(),&allocate,nullptr,&depth_memory_),"Allocate depth image memory");
        check(vkBindImageMemory(device.handle(),depth_image_,depth_memory_,0),"Bind depth image memory");
        VkImageViewCreateInfo depth_view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        depth_view.image = depth_image_; depth_view.viewType = VK_IMAGE_VIEW_TYPE_2D; depth_view.format = device.depth_format();
        depth_view.subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT,0,1,0,1};
        check(vkCreateImageView(device.handle(),&depth_view,nullptr,&depth_view_),"Create depth image view");
        if (capture)
        {
            const auto pixels = static_cast<std::uint64_t>(extent.width)*extent.height;
            if (pixels > std::numeric_limits<std::size_t>::max()/4) throw std::runtime_error("Diagnostic image size overflow");
            readback_ = std::make_unique<VulkanBuffer>(device,pixels*4,VK_BUFFER_USAGE_TRANSFER_DST_BIT);
        }
        for (std::size_t i = 0; i < images_.size(); ++i)
        {
            VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO}; view.image = images_[i]; view.viewType = VK_IMAGE_VIEW_TYPE_2D; view.format = format_.format;
            view.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
            check(vkCreateImageView(device.handle(),&view,nullptr,&views_[i]),"Create swapchain image view");
            VkFramebufferCreateInfo framebuffer{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO}; framebuffer.renderPass = pass_;
            const std::array framebuffer_views{views_[i],depth_view_};
            framebuffer.attachmentCount = 2; framebuffer.pAttachments = framebuffer_views.data(); framebuffer.width = extent_.width; framebuffer.height = extent_.height; framebuffer.layers = 1;
            check(vkCreateFramebuffer(device.handle(),&framebuffer,nullptr,&framebuffers_[i]),"Create clear framebuffer");
            VkSemaphoreCreateInfo semaphore{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
            check(vkCreateSemaphore(device.handle(),&semaphore,nullptr,&rendered_[i]),"Create image presentation semaphore");
            VkFenceCreateInfo fence{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
            check(vkCreateFence(device.handle(),&fence,nullptr,&present_fences_[i]),"Create presentation fence");
        }
    }
    catch (...) { destroy(); throw; }
}
VulkanSwapchain::~VulkanSwapchain() { destroy(); }
void VulkanSwapchain::wait_presentations()
{
    for (std::size_t i = 0; i < present_fences_.size(); ++i)
        if (present_fences_[i] && i < presentation_pending_.size() && presentation_pending_[i])
        {
            check(vkWaitForFences(device_.handle(),1,&present_fences_[i],VK_TRUE,UINT64_MAX),"Wait for swapchain presentation retirement");
            presentation_pending_[i] = false;
        }
}
VkResult VulkanSwapchain::acquire(VkSemaphore acquired, VkFence fence, std::uint32_t& image) const
{ return vkAcquireNextImageKHR(device_.handle(),swapchain_,1'000'000'000ULL,acquired,fence,&image); }
DrawCounts VulkanSwapchain::record(VkCommandBuffer command, std::uint32_t image, const VulkanScene& scene, const RenderFrameData& frame, ImDrawData* ui) const
{
    VkClearValue color{};
    const auto clear = scene_clear_color(format_.format);
    for (std::size_t i = 0; i < clear.size(); ++i) color.color.float32[i] = clear[i];
    VkRenderPassBeginInfo info{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO}; info.renderPass = pass_; info.framebuffer = framebuffers_.at(image);
    VkClearValue depth{}; depth.depthStencil = {1.0f,0};
    const std::array clears{color,depth};
    info.renderArea = {{0,0},extent_}; info.clearValueCount = 2; info.pClearValues = clears.data();
    vkCmdBeginRenderPass(command,&info,VK_SUBPASS_CONTENTS_INLINE);
    const auto counts = scene.record(command,extent_,frame);
    if (ui) ImGui_ImplVulkan_RenderDrawData(ui,command);
    vkCmdEndRenderPass(command);
    if (readback_)
    {
        VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT; barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        barrier.oldLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR; barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = images_.at(image); barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
        vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,nullptr,0,nullptr,1,&barrier);
        VkBufferImageCopy copy{}; copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT,0,0,1}; copy.imageExtent = {extent_.width,extent_.height,1};
        vkCmdCopyImageToBuffer(command,images_[image],VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,readback_->handle(),1,&copy);
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT; barrier.dstAccessMask = 0;
        barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL; barrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        VkBufferMemoryBarrier host{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};
        host.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT; host.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
        host.srcQueueFamilyIndex = host.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        host.buffer = readback_->handle(); host.size = VK_WHOLE_SIZE;
        vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT | VK_PIPELINE_STAGE_HOST_BIT,
            0,0,nullptr,1,&host,1,&barrier);
    }
    return counts;
}
CapturedFrame VulkanSwapchain::read_pixels()
{
    if (!readback_) throw std::logic_error("Frame capture was not enabled");
    CapturedFrame frame{extent_.width,extent_.height,{}};
    const auto bytes = readback_->read(); frame.rgba.resize(bytes.size());
    const bool bgra = format_.format == VK_FORMAT_B8G8R8A8_SRGB || format_.format == VK_FORMAT_B8G8R8A8_UNORM;
    for (std::size_t i = 0; i < bytes.size(); i += 4)
    {
        frame.rgba[i] = std::to_integer<std::uint8_t>(bytes[i+(bgra ? 2 : 0)]);
        frame.rgba[i+1] = std::to_integer<std::uint8_t>(bytes[i+1]);
        frame.rgba[i+2] = std::to_integer<std::uint8_t>(bytes[i+(bgra ? 0 : 2)]);
        frame.rgba[i+3] = std::to_integer<std::uint8_t>(bytes[i+3]);
    }
    return frame;
}
VkResult VulkanSwapchain::present(std::uint32_t image)
{
    if (presentation_pending_.at(image))
    {
        check(vkWaitForFences(device_.handle(),1,&present_fences_[image],VK_TRUE,UINT64_MAX),"Wait before reusing presentation fence");
        presentation_pending_[image] = false;
    }
    check(vkResetFences(device_.handle(),1,&present_fences_[image]),"Reset presentation fence");
    VkSwapchainPresentFenceInfoKHR fences{VK_STRUCTURE_TYPE_SWAPCHAIN_PRESENT_FENCE_INFO_KHR}; fences.swapchainCount = 1; fences.pFences = &present_fences_[image];
    VkPresentInfoKHR info{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR}; info.pNext = &fences; info.waitSemaphoreCount = 1;
    info.pWaitSemaphores = &rendered_[image]; info.swapchainCount = 1; info.pSwapchains = &swapchain_; info.pImageIndices = &image;
    const auto result = vkQueuePresentKHR(device_.present(),&info);
    // These recoverable results still enqueue the presentation fence/waits.
    if (result == VK_SUCCESS || result == VK_SUBOPTIMAL_KHR || result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_ERROR_SURFACE_LOST_KHR)
        presentation_pending_[image] = true;
    else lost_ = true;
    return result;
}
void VulkanSwapchain::destroy() noexcept
{
    if (!lost_)
    {
        try { wait_presentations(); }
        catch (const std::exception& error) { std::fprintf(stderr,"Vulkan cleanup: %s\n",error.what()); }
    }
    for (auto framebuffer : framebuffers_) if (framebuffer) vkDestroyFramebuffer(device_.handle(),framebuffer,nullptr);
    readback_.reset();
    if (depth_view_) vkDestroyImageView(device_.handle(),depth_view_,nullptr);
    if (depth_image_) vkDestroyImage(device_.handle(),depth_image_,nullptr);
    if (depth_memory_) vkFreeMemory(device_.handle(),depth_memory_,nullptr);
    if (pass_) vkDestroyRenderPass(device_.handle(),pass_,nullptr);
    for (auto view : views_) if (view) vkDestroyImageView(device_.handle(),view,nullptr);
    for (auto semaphore : rendered_) if (semaphore) vkDestroySemaphore(device_.handle(),semaphore,nullptr);
    for (auto fence : present_fences_) if (fence) vkDestroyFence(device_.handle(),fence,nullptr);
    if (swapchain_) vkDestroySwapchainKHR(device_.handle(),swapchain_,nullptr);
    swapchain_ = VK_NULL_HANDLE; pass_ = VK_NULL_HANDLE;
    depth_view_ = VK_NULL_HANDLE; depth_image_ = VK_NULL_HANDLE; depth_memory_ = VK_NULL_HANDLE;
}
}
