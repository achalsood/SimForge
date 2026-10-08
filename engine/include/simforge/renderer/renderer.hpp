#pragma once
#include <simforge/renderer/render_frame_data.hpp>
#include <cstdint>
#include <memory>
#include <string>

namespace simforge::platform { class Window; }
namespace simforge::renderer
{
struct RendererOptions
{
#ifdef NDEBUG
    bool validation{false};
#else
    bool validation{true};
#endif
    bool require_validation{false};
    bool capture_frames{false}; // Diagnostic transfer/readback; off in normal rendering.
};
struct RendererInfo
{
    std::string gpu_name, gpu_type, api_version, loader_version, surface_format, present_mode, depth_format;
    std::uint32_t graphics_family{0}, present_family{0}, width{0}, height{0}, image_count{0};
    bool validation_enabled{false}, synchronization_validation{false};
    std::uint64_t presented_frames{0}, swapchain_generations{0}, skipped_frames{0};
    std::uint32_t validation_errors{0}, validation_warnings{0};
    std::uint32_t draw_calls{0}, submitted_vertices{0}, submitted_indices{0};
    std::uint64_t pipeline_generations{0}, buffer_allocations{0}, depth_images_created{0};
    double asset_initialization_ms{0}, pipeline_creation_ms{0};
    float aspect_ratio{0};
};
struct CapturedFrame
{
    std::uint32_t width{0}, height{0};
    std::vector<std::uint8_t> rgba; // Owned tightly packed RGBA8, top row first.
};
class Renderer
{
public:
    // Window and renderer operations belong on the SDL main thread.
    Renderer();
    ~Renderer();
    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;
    Renderer(Renderer&&) = delete;
    Renderer& operator=(Renderer&&) = delete;
    void initialize(const platform::Window& window, RendererOptions options = {});
    // Returns false for a closed/minimized window, zero extent or a recoverable
    // acquire/recreation condition. Fatal errors throw and require shutdown.
    [[nodiscard]] bool render_frame(const RenderFrameData& frame);
    // Opt-in diagnostic; waits for the latest submitted frame and owns its pixels.
    [[nodiscard]] CapturedFrame capture_frame();
    void shutdown() noexcept;
    [[nodiscard]] bool initialized() const noexcept;
    // A diagnostic snapshot; includes destruction messages after shutdown.
    [[nodiscard]] RendererInfo info() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    RendererInfo last_info_;
};
}
