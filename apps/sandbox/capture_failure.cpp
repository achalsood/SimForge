#include "capture.hpp"
#include <cstdlib>
#include <exception>
#include <iostream>
#include <stdexcept>
FailureCapture::FailureCapture(simforge::renderer::Renderer& renderer) : renderer_{renderer}, exceptions_{std::uncaught_exceptions()}
{ if(const auto* path=std::getenv("SIMFORGE_FAILURE_CAPTURE")) path_=std::filesystem::u8path(path); }
FailureCapture::~FailureCapture() noexcept
{
    if(path_.empty() || std::uncaught_exceptions()<=exceptions_ || !renderer_.initialized()) return;
    try { write_capture(renderer_.capture_frame(),path_); std::cerr<<"Failure framebuffer saved: "<<path_.string()<<'\n'; }
    catch(const std::exception& e) { std::cerr<<"Failure framebuffer unavailable: "<<e.what()<<'\n'; }
}
void diagnostic_failure_probe()
{ if(std::getenv("SIMFORGE_TEST_FAIL_AFTER_PRESENT")) throw std::runtime_error("Intentional graphics artifact-retention test failure"); }
