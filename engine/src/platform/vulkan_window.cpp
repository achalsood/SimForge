#include "window_state.hpp"
#include <simforge/platform/vulkan_window.hpp>
#include <SDL3/SDL_vulkan.h>

namespace simforge::platform
{
VulkanWindow::VulkanWindow(const Window& window) : state_{window.state_}
{
    if (!state_ || !state_->open || !state_->vulkan)
        throw std::invalid_argument("Renderer requires an open SDL Vulkan window");
}
PixelSize VulkanWindow::pixel_size() const { return state_->pixel_size(); }
std::vector<const char*> VulkanWindow::instance_extensions() const
{
    Uint32 count = 0; const auto extensions = SDL_Vulkan_GetInstanceExtensions(&count);
    if (!extensions) throw std::runtime_error(std::string{"SDL Vulkan extension query failed: "} + SDL_GetError());
    return {extensions,extensions+count};
}
VkSurfaceKHR VulkanWindow::create_surface(VkInstance instance) const
{
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    if (!SDL_Vulkan_CreateSurface(state_->window,instance,nullptr,&surface))
        throw std::runtime_error(std::string{"SDL Vulkan surface creation failed: "} + SDL_GetError());
    return surface;
}
}
