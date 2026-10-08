#include <simforge/core/engine.hpp>

#include <stdexcept>

namespace simforge
{

Engine::~Engine()
{
    shutdown();
}

void Engine::initialize()
{
    if (initialized_)
    {
        throw std::logic_error("SimForge Engine is already initialized");
    }

    clock_.reset();

    last_frame_delta_ = 0.0;
    frame_count_ = 0;

    initialized_ = true;
    running_ = true;
}

void Engine::shutdown()
{
    if (!initialized_)
    {
        return;
    }

    running_ = false;
    initialized_ = false;
}

bool Engine::initialized() const noexcept
{
    return initialized_;
}

bool Engine::running() const noexcept
{
    return running_;
}

void Engine::request_exit() noexcept
{
    running_ = false;
}

double Engine::begin_frame()
{
    if (!initialized_)
    {
        throw std::logic_error(
            "Cannot begin a frame before initializing SimForge Engine"
        );
    }

    last_frame_delta_ = clock_.tick();

    return last_frame_delta_;
}

void Engine::end_frame() noexcept
{
    ++frame_count_;
}

double Engine::uptime_seconds() const
{
    return clock_.elapsed_seconds();
}

double Engine::last_frame_delta() const noexcept
{
    return last_frame_delta_;
}

std::uint64_t Engine::frame_count() const noexcept
{
    return frame_count_;
}

} // namespace simforge