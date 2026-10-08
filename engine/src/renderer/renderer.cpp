#include <simforge/renderer/renderer.hpp>
#include "vulkan_swapchain.hpp"
#include "vulkan_support.hpp"
#include <cstdio>
#include <chrono>

namespace simforge::renderer
{
namespace
{
std::string version_text(std::uint32_t version)
{ return std::to_string(VK_API_VERSION_MAJOR(version))+"."+std::to_string(VK_API_VERSION_MINOR(version))+"."+std::to_string(VK_API_VERSION_PATCH(version)); }
std::string type_text(VkPhysicalDeviceType type)
{
    switch (type)
    {
    case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU: return "discrete";
    case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU: return "integrated";
    case VK_PHYSICAL_DEVICE_TYPE_CPU: return "CPU";
    case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU: return "virtual";
    default: return "other";
    }
}
std::string format_text(VkSurfaceFormatKHR format)
{
    switch (format.format)
    {
    case VK_FORMAT_B8G8R8A8_SRGB: return "B8G8R8A8_SRGB";
    case VK_FORMAT_R8G8B8A8_SRGB: return "R8G8B8A8_SRGB";
    case VK_FORMAT_B8G8R8A8_UNORM: return "B8G8R8A8_UNORM";
    case VK_FORMAT_D32_SFLOAT: return "D32_SFLOAT";
    case VK_FORMAT_D32_SFLOAT_S8_UINT: return "D32_SFLOAT_S8_UINT";
    case VK_FORMAT_D24_UNORM_S8_UINT: return "D24_UNORM_S8_UINT";
    default: return "VkFormat("+std::to_string(format.format)+")";
    }
}
std::string mode_text(VkPresentModeKHR mode)
{
    switch (mode)
    {
    case VK_PRESENT_MODE_FIFO_KHR: return "FIFO";
    case VK_PRESENT_MODE_MAILBOX_KHR: return "MAILBOX";
    case VK_PRESENT_MODE_IMMEDIATE_KHR: return "IMMEDIATE";
    default: return "VkPresentMode("+std::to_string(mode)+")";
    }
}
}
struct Renderer::Impl
{
    detail::Diagnostics diagnostics; // Outlives the context/debug callback.
    RendererInfo status;
    std::unique_ptr<detail::VulkanContext> context;
    std::unique_ptr<detail::VulkanDevice> device;
    std::unique_ptr<detail::VulkanScene> scene;
    std::unique_ptr<detail::VulkanSwapchain> swapchain;
    VkCommandPool pool{VK_NULL_HANDLE};
    VkCommandBuffer command{VK_NULL_HANDLE};
    VkSemaphore acquired{VK_NULL_HANDLE};
    VkFence submitted{VK_NULL_HANDLE}, acquisition{VK_NULL_HANDLE};
    platform::PixelSize last_pixels{};
    bool dirty{true}, failed{false}, acquisition_pending{false};
    bool capture_enabled{false}, capture_available{false};

    Impl(const platform::Window& window, RendererOptions options)
    {
        try
        {
            capture_enabled = options.capture_frames;
            context = std::make_unique<detail::VulkanContext>(window,options,diagnostics);
            device = std::make_unique<detail::VulkanDevice>(*context);
            const auto assets_started = std::chrono::steady_clock::now();
            scene = std::make_unique<detail::VulkanScene>(*device);
            status.asset_initialization_ms = std::chrono::duration<double,std::milli>{std::chrono::steady_clock::now()-assets_started}.count();
            status.buffer_allocations = 2;
            status.depth_format = format_text({device->depth_format(),VK_COLOR_SPACE_SRGB_NONLINEAR_KHR});
            const auto& properties = device->properties(); const auto queues = device->families();
            status.gpu_name = properties.deviceName; status.gpu_type = type_text(properties.deviceType);
            status.api_version = version_text(properties.apiVersion); status.graphics_family = queues.graphics; status.present_family = queues.present;
            status.loader_version = version_text(context->loader_version());
            status.validation_enabled = context->validation(); status.synchronization_validation = context->synchronization_validation();
            VkCommandPoolCreateInfo command_pool{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
            command_pool.queueFamilyIndex = queues.graphics; command_pool.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
            detail::check(vkCreateCommandPool(device->handle(),&command_pool,nullptr,&pool),"Create command pool");
            VkCommandBufferAllocateInfo allocate{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO}; allocate.commandPool = pool;
            allocate.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY; allocate.commandBufferCount = 1;
            detail::check(vkAllocateCommandBuffers(device->handle(),&allocate,&command),"Allocate clear command buffer");
            VkSemaphoreCreateInfo semaphore{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
            detail::check(vkCreateSemaphore(device->handle(),&semaphore,nullptr,&acquired),"Create acquisition semaphore");
            VkFenceCreateInfo fence{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO}; fence.flags = VK_FENCE_CREATE_SIGNALED_BIT;
            detail::check(vkCreateFence(device->handle(),&fence,nullptr,&submitted),"Create submission fence");
            fence.flags = 0;
            detail::check(vkCreateFence(device->handle(),&fence,nullptr,&acquisition),"Create acquisition fence");
            (void)recreate(context->pixels());
        }
        catch (...) { destroy(); throw; }
    }
    ~Impl() { destroy(); }
    RendererInfo info() const
    {
        auto result = status; result.validation_errors = diagnostics.errors.load(); result.validation_warnings = diagnostics.warnings.load(); return result;
    }
    bool recreate(platform::PixelSize pixels)
    {
        if (pixels.width == 0 || pixels.height == 0) return false;
        detail::check(vkDeviceWaitIdle(device->handle()),"Wait before swapchain recreation");
        if (swapchain) swapchain->wait_presentations();
        try
        {
            VkSurfaceCapabilitiesKHR capabilities{};
            detail::check(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device->physical(),context->surface(),&capabilities),"Query resize capabilities");
            const auto extent = choose_extent(capabilities,pixels.width,pixels.height);
            if (!extent) return false;
            auto replacement = std::make_unique<detail::VulkanSwapchain>(*context,*device,*extent,swapchain ? swapchain->handle() : VK_NULL_HANDLE,capture_enabled);
            scene->ensure_pipeline(replacement->render_pass(),{replacement->format().format,device->depth_format()});
            swapchain.swap(replacement); // oldSwapchain is retired; never render through it again.
            status.width = extent->width; status.height = extent->height; status.image_count = swapchain->image_count();
            status.surface_format = format_text(swapchain->format()); status.present_mode = mode_text(swapchain->mode());
            ++status.swapchain_generations; last_pixels = pixels; dirty = false;
            ++status.depth_images_created;
            if (capture_enabled) ++status.buffer_allocations;
            status.pipeline_generations = scene->pipeline_generations(); status.pipeline_creation_ms = scene->pipeline_creation_ms();
            capture_available = false;
            return true;
        }
        catch (const detail::VulkanError& error)
        {
            if (error.result() != VK_ERROR_OUT_OF_DATE_KHR) throw;
            // Even a failed creation using oldSwapchain can retire it. Discard
            // the drained old generation and retry from no swapchain next frame.
            swapchain.reset(); dirty = true; return false;
        }
    }
    bool draw(const RenderFrameData& frame)
    {
        if (failed) throw std::logic_error("Renderer is in a failed state; shut down before reinitialization");
        try
        {
            const auto pixels = context->pixels();
            if (pixels.width == 0 || pixels.height == 0) { ++status.skipped_frames; return false; }
            if ((dirty || pixels != last_pixels || !swapchain) && !recreate(pixels)) { ++status.skipped_frames; return false; }
            detail::check(vkWaitForFences(device->handle(),1,&submitted,VK_TRUE,UINT64_MAX),"Wait before command buffer reuse");
            detail::check(vkResetFences(device->handle(),1,&acquisition),"Reset acquisition fence");
            std::uint32_t image = 0;
            const auto result = swapchain->acquire(acquired,acquisition,image);
            if (result == VK_ERROR_OUT_OF_DATE_KHR) { dirty = true; ++status.skipped_frames; return false; }
            if (result == VK_TIMEOUT || result == VK_NOT_READY) { ++status.skipped_frames; return false; }
            if (result != VK_SUBOPTIMAL_KHR) detail::check(result,"Acquire swapchain image");
            acquisition_pending = true;
            detail::check(vkWaitForFences(device->handle(),1,&acquisition,VK_TRUE,UINT64_MAX),"Wait for acquired image ownership");
            acquisition_pending = false;
            detail::check(vkResetCommandBuffer(command,0),"Reset clear command buffer");
            VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO}; begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
            detail::check(vkBeginCommandBuffer(command,&begin),"Begin clear command buffer");
            const auto counts = swapchain->record(command,image,*scene,frame);
            detail::check(vkEndCommandBuffer(command),"End clear command buffer");
            // Reset only after a successful acquire/record, so out-of-date paths
            // leave the submission fence signaled and cannot deadlock next frame.
            detail::check(vkResetFences(device->handle(),1,&submitted),"Reset submission fence");
            const auto rendered = swapchain->rendered(image);
            const VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
            VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO}; submit.waitSemaphoreCount = 1; submit.pWaitSemaphores = &acquired;
            submit.pWaitDstStageMask = &wait_stage; submit.commandBufferCount = 1; submit.pCommandBuffers = &command;
            submit.signalSemaphoreCount = 1; submit.pSignalSemaphores = &rendered;
            detail::check(vkQueueSubmit(device->graphics(),1,&submit,submitted),"Submit clear frame");
            capture_available = capture_enabled;
            status.draw_calls = counts.calls; status.submitted_vertices = counts.vertices; status.submitted_indices = counts.indices;
            status.aspect_ratio = static_cast<float>(swapchain->extent().width)/static_cast<float>(swapchain->extent().height);
            const auto presented = swapchain->present(image);
            if (presented == VK_ERROR_OUT_OF_DATE_KHR || presented == VK_SUBOPTIMAL_KHR || result == VK_SUBOPTIMAL_KHR) dirty = true;
            if (presented != VK_SUBOPTIMAL_KHR && presented != VK_ERROR_OUT_OF_DATE_KHR) detail::check(presented,"Present clear frame");
            if (presented == VK_ERROR_OUT_OF_DATE_KHR) { ++status.skipped_frames; return false; }
            ++status.presented_frames; return true;
        }
        catch (...) { failed = true; throw; }
    }
    CapturedFrame capture()
    {
        if (failed || !capture_available || !swapchain) throw std::logic_error("No submitted frame available for capture");
        detail::check(vkWaitForFences(device->handle(),1,&submitted,VK_TRUE,UINT64_MAX),"Wait for diagnostic frame copy");
        return swapchain->read_pixels();
    }
    void destroy() noexcept
    {
        if (device)
        {
            const auto idle = vkDeviceWaitIdle(device->handle());
            if (idle != VK_SUCCESS)
            {
                if (swapchain) swapchain->abandon_waits();
                std::fprintf(stderr,"Vulkan cleanup: device idle failed (VkResult %d).\n",static_cast<int>(idle));
            }
            if (idle == VK_SUCCESS && acquisition_pending)
                (void)vkWaitForFences(device->handle(),1,&acquisition,VK_TRUE,UINT64_MAX);
            if (pool) vkDestroyCommandPool(device->handle(),pool,nullptr);
            pool = VK_NULL_HANDLE; command = VK_NULL_HANDLE;
            swapchain.reset();
            scene.reset();
            if (submitted) vkDestroyFence(device->handle(),submitted,nullptr);
            if (acquisition) vkDestroyFence(device->handle(),acquisition,nullptr);
            if (acquired) vkDestroySemaphore(device->handle(),acquired,nullptr);
            submitted = acquisition = VK_NULL_HANDLE; acquired = VK_NULL_HANDLE;
            device.reset();
        }
        context.reset();
    }
};
Renderer::Renderer() = default;
Renderer::~Renderer() { shutdown(); }
void Renderer::initialize(const platform::Window& window, RendererOptions options)
{
    if (impl_) throw std::logic_error("Renderer is already initialized");
    auto replacement = std::make_unique<Impl>(window,options);
    last_info_ = {}; impl_ = std::move(replacement);
}
bool Renderer::render_frame(const RenderFrameData& frame)
{
    if (!impl_) throw std::logic_error("Cannot render before initialization");
    validate_frame(frame); // Invalid client data does not poison the GPU lifecycle.
    return impl_->draw(frame);
}
CapturedFrame Renderer::capture_frame()
{
    if (!impl_) throw std::logic_error("Cannot capture before initialization");
    try { return impl_->capture(); }
    catch (const detail::VulkanError&) { impl_->failed = true; throw; }
}
void Renderer::shutdown() noexcept
{
    if (!impl_) return;
    impl_->destroy();
    // Keep diagnostics from destruction as well as ordinary frames.
    last_info_ = std::move(impl_->status);
    last_info_.validation_errors = impl_->diagnostics.errors.load();
    last_info_.validation_warnings = impl_->diagnostics.warnings.load();
    impl_.reset();
}
bool Renderer::initialized() const noexcept { return impl_ != nullptr; }
RendererInfo Renderer::info() const { return impl_ ? impl_->info() : last_info_; }
}
