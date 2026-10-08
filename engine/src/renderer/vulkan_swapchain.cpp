#include "vulkan_swapchain.hpp"
#include "vulkan_support.hpp"
#include <cstdio>

namespace simforge::renderer::detail
{
VulkanSwapchain::VulkanSwapchain(const VulkanContext& context, const VulkanDevice& device, VkExtent2D extent, VkSwapchainKHR old)
    : device_{device}, extent_{extent}
{
    try
    {
        VkSurfaceCapabilitiesKHR capabilities{};
        check(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device.physical(),context.surface(),&capabilities),"Query swapchain capabilities");
        const auto formats = color_formats(device.physical(),context.surface());
        const auto modes = enumerate<VkPresentModeKHR>([&](auto n, auto values) { return vkGetPhysicalDeviceSurfacePresentModesKHR(device.physical(),context.surface(),n,values); },"Query swapchain modes");
        format_ = choose_format(formats); mode_ = choose_present_mode(modes);
        if ((capabilities.supportedUsageFlags & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT) == 0) throw std::runtime_error("Surface no longer supports color attachments");
        const auto queues = device.families(); const std::uint32_t indices[]{queues.graphics,queues.present};
        VkSwapchainCreateInfoKHR info{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR}; info.surface = context.surface();
        info.minImageCount = choose_image_count(capabilities); info.imageFormat = format_.format; info.imageColorSpace = format_.colorSpace;
        info.imageExtent = extent_; info.imageArrayLayers = 1; info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
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
        const VkAttachmentReference color{0,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
        VkSubpassDescription subpass{}; subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS; subpass.colorAttachmentCount = 1; subpass.pColorAttachments = &color;
        VkSubpassDependency dependency{}; dependency.srcSubpass = VK_SUBPASS_EXTERNAL; dependency.dstSubpass = 0;
        dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT; dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        VkRenderPassCreateInfo render_pass{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO}; render_pass.attachmentCount = 1; render_pass.pAttachments = &attachment;
        render_pass.subpassCount = 1; render_pass.pSubpasses = &subpass; render_pass.dependencyCount = 1; render_pass.pDependencies = &dependency;
        check(vkCreateRenderPass(device.handle(),&render_pass,nullptr,&pass_),"Create clear render pass");
        for (std::size_t i = 0; i < images_.size(); ++i)
        {
            VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO}; view.image = images_[i]; view.viewType = VK_IMAGE_VIEW_TYPE_2D; view.format = format_.format;
            view.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
            check(vkCreateImageView(device.handle(),&view,nullptr,&views_[i]),"Create swapchain image view");
            VkFramebufferCreateInfo framebuffer{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO}; framebuffer.renderPass = pass_;
            framebuffer.attachmentCount = 1; framebuffer.pAttachments = &views_[i]; framebuffer.width = extent_.width; framebuffer.height = extent_.height; framebuffer.layers = 1;
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
void VulkanSwapchain::record_clear(VkCommandBuffer command, std::uint32_t image) const
{
    VkClearValue color{}; color.color = {{0.025f,0.045f,0.075f,1.0f}};
    VkRenderPassBeginInfo info{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO}; info.renderPass = pass_; info.framebuffer = framebuffers_.at(image);
    info.renderArea = {{0,0},extent_}; info.clearValueCount = 1; info.pClearValues = &color;
    vkCmdBeginRenderPass(command,&info,VK_SUBPASS_CONTENTS_INLINE); vkCmdEndRenderPass(command);
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
    if (pass_) vkDestroyRenderPass(device_.handle(),pass_,nullptr);
    for (auto view : views_) if (view) vkDestroyImageView(device_.handle(),view,nullptr);
    for (auto semaphore : rendered_) if (semaphore) vkDestroySemaphore(device_.handle(),semaphore,nullptr);
    for (auto fence : present_fences_) if (fence) vkDestroyFence(device_.handle(),fence,nullptr);
    if (swapchain_) vkDestroySwapchainKHR(device_.handle(),swapchain_,nullptr);
    swapchain_ = VK_NULL_HANDLE; pass_ = VK_NULL_HANDLE;
}
}
