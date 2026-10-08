#pragma once
#include <simforge/platform/window.hpp>
#include <vulkan/vulkan_core.h>
#include <vector>

namespace simforge::platform
{
// A lease on a native window; the renderer owns the VkSurfaceKHR it creates.
class VulkanWindow
{
public:
    explicit VulkanWindow(const Window& window);
    [[nodiscard]] PixelSize pixel_size() const;
    [[nodiscard]] std::vector<const char*> instance_extensions() const;
    [[nodiscard]] VkSurfaceKHR create_surface(VkInstance instance) const;
private:
    std::shared_ptr<WindowState> state_;
};
}
