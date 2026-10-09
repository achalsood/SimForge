#pragma once
#include <simforge/renderer/renderer.hpp>
#include <filesystem>
// Lossless diagnostic image output. Creates a new file; never replaces a file.
void write_capture(const simforge::renderer::CapturedFrame& frame,const std::filesystem::path& path);
// Saves the last available framebuffer only when unwinding a failed smoke run.
class FailureCapture
{
public:
    explicit FailureCapture(simforge::renderer::Renderer& renderer);
    ~FailureCapture() noexcept;
private:
    simforge::renderer::Renderer& renderer_;
    std::filesystem::path path_;
    int exceptions_;
};
void diagnostic_failure_probe(); // Explicit opt-in test fault, never normal application behavior.
