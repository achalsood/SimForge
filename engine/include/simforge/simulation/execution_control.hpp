#pragma once
#include <cstdint>
#include <functional>
#include <stdexcept>
#include <stop_token>
#include <string_view>

namespace simforge::simulation
{
struct ExecutionCancelled : std::runtime_error
{ ExecutionCancelled() : std::runtime_error("Experiment cancelled") {} };
// Optional computation control. Callbacks execute on the caller's thread only.
struct ExecutionControl
{
    std::stop_token stop;
    std::function<void(std::string_view, std::string_view, std::uint64_t, std::uint64_t)> progress;
    void check() const { if (stop.stop_requested()) throw ExecutionCancelled{}; }
    void update(std::string_view phase, std::string_view name = {}, std::uint64_t tick = 0, std::uint64_t horizon = 0) const
    { check(); if (progress) progress(phase,name,tick,horizon); check(); }
};
}
