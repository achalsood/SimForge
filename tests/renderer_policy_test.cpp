#include <simforge/renderer/render_policy.hpp>
#include <array>
#include <iostream>
#include <source_location>
#include <stdexcept>
#include <string_view>
#include <cmath>

using namespace simforge::renderer;
namespace
{
void check(bool value, std::source_location location = std::source_location::current())
{
    if (!value) throw std::runtime_error("Renderer policy check failed at line " + std::to_string(location.line()));
}
template<class Function> void rejects(Function&& function)
{
    bool rejected = false;
    try { function(); } catch (const std::invalid_argument&) { rejected = true; }
    check(rejected);
}
DeviceSupport suitable(VkPhysicalDeviceType type = VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU)
{
    DeviceSupport support;
    support.type = type;
    support.swapchain_extension = support.present_fences = support.color_attachment_usage = true;
    support.queues = {{1,true,true}};
    support.formats = {{VK_FORMAT_B8G8R8A8_SRGB,VK_COLOR_SPACE_SRGB_NONLINEAR_KHR}};
    support.present_modes = {VK_PRESENT_MODE_FIFO_KHR};
    support.depth_formats = {{VK_FORMAT_D32_SFLOAT,VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT}};
    return support;
}
void images()
{
    VkSurfaceCapabilitiesKHR limits{};
    limits.minImageCount = 2;
    check(choose_image_count(limits) == 3); // max == 0 is unbounded.
    limits.maxImageCount = 2;
    check(choose_image_count(limits) == 2);
    limits.maxImageCount = 4;
    check(choose_image_count(limits) == 3);
    limits.minImageCount = limits.maxImageCount = UINT32_MAX;
    check(choose_image_count(limits) == UINT32_MAX); // No overflow.
    limits.minImageCount = 3; limits.maxImageCount = 2;
    rejects([&] { (void)choose_image_count(limits); });
    limits.minImageCount = 0; limits.maxImageCount = 0;
    rejects([&] { (void)choose_image_count(limits); });
}
void formats()
{
    const auto linear = scene_clear_color(VK_FORMAT_B8G8R8A8_SRGB);
    check(linear == std::array{0.025f,0.045f,0.075f,1.0f});
    const auto encoded = scene_clear_color(VK_FORMAT_B8G8R8A8_UNORM);
    check(std::lround(encoded[0]*255) == 44 && std::lround(encoded[1]*255) == 60 && std::lround(encoded[2]*255) == 77);
    check(encoded == scene_clear_color(VK_FORMAT_R8G8B8A8_UNORM) && encoded[3] == 1);
    check(!requires_srgb_encoding(VK_FORMAT_R8G8B8A8_SRGB));
    const std::array available{
        VkSurfaceFormatKHR{VK_FORMAT_R8G8B8A8_UNORM,VK_COLOR_SPACE_SRGB_NONLINEAR_KHR},
        VkSurfaceFormatKHR{VK_FORMAT_R8G8B8A8_SRGB,VK_COLOR_SPACE_SRGB_NONLINEAR_KHR},
        VkSurfaceFormatKHR{VK_FORMAT_B8G8R8A8_SRGB,VK_COLOR_SPACE_SRGB_NONLINEAR_KHR}};
    check(choose_format(available).format == VK_FORMAT_B8G8R8A8_SRGB);
    check(choose_format(std::span{available}.first(2)).format == VK_FORMAT_R8G8B8A8_SRGB);
    check(choose_format(std::span{available}.first(1)).format == VK_FORMAT_R8G8B8A8_UNORM);
    const std::array unspecified{VkSurfaceFormatKHR{VK_FORMAT_UNDEFINED,VK_COLOR_SPACE_SRGB_NONLINEAR_KHR}};
    check(choose_format(unspecified).format == VK_FORMAT_B8G8R8A8_SRGB);
    const std::array other_space{VkSurfaceFormatKHR{VK_FORMAT_R16G16B16A16_SFLOAT,VK_COLOR_SPACE_EXTENDED_SRGB_LINEAR_EXT}};
    const auto selected = choose_format(other_space);
    check(selected.format == other_space[0].format && selected.colorSpace == other_space[0].colorSpace);
    rejects([] { (void)choose_format({}); });
    const std::array modes{VK_PRESENT_MODE_MAILBOX_KHR,VK_PRESENT_MODE_FIFO_KHR,VK_PRESENT_MODE_IMMEDIATE_KHR};
    check(choose_present_mode(modes) == VK_PRESENT_MODE_FIFO_KHR);
    check(choose_present_mode(std::span{modes}.first(1)) == VK_PRESENT_MODE_MAILBOX_KHR);
    rejects([] { (void)choose_present_mode({}); });
    check(choose_composite_alpha(VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR | VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR) == VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR);
    check(choose_composite_alpha(VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR) == VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR);
    check(choose_composite_alpha(VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR) == VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR);
    check(choose_composite_alpha(VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR) == VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR);
    rejects([] { (void)choose_composite_alpha(0); });
}
void extents()
{
    VkSurfaceCapabilitiesKHR limits{};
    limits.minImageExtent = {64,32}; limits.maxImageExtent = {4096,2048};
    limits.currentExtent = {UINT32_MAX,UINT32_MAX};
    limits.currentExtent.height = 600;
    rejects([&] { (void)choose_extent(limits,100,100); });
    limits.currentExtent.height = UINT32_MAX;
    const auto bounded = choose_extent(limits,1,UINT32_MAX);
    check(bounded && bounded->width == 64 && bounded->height == 2048);
    const auto actual = choose_extent(limits,1920,1080);
    check(actual && actual->width == 1920 && actual->height == 1080);
    check(!choose_extent(limits,0,100)); check(!choose_extent(limits,100,0));
    limits.currentExtent = {800,600};
    const auto fixed = choose_extent(limits,1920,1080);
    check(fixed && fixed->width == 800 && fixed->height == 600);
    check(!choose_extent(limits,0,0)); // Minimized pixels override a stale fixed extent.
    limits.currentExtent = {0,0}; check(!choose_extent(limits,100,100));
    limits.currentExtent = {UINT32_MAX,UINT32_MAX};
    limits.minImageExtent.width = 4097;
    rejects([&] { (void)choose_extent(limits,100,100); });
}
void queues()
{
    check(!choose_queues({}));
    const std::array empty{QueueSupport{0,true,true}};
    check(!choose_queues(empty));
    const std::array separate{QueueSupport{1,true,false},QueueSupport{1,false,true}};
    const auto split = choose_queues(separate);
    check(split && *split == QueueFamilies{0,1} && split->separate());
    const std::array combined{QueueSupport{1,true,false},QueueSupport{1,false,true},QueueSupport{1,true,true}};
    const auto same = choose_queues(combined);
    check(same && *same == QueueFamilies{2,2} && !same->separate());
    check(!choose_queues(std::span{separate}.first(1)));
    check(!choose_queues(std::span{separate}.last(1)));
}
void devices()
{
    check(device_rejection(suitable()).empty());
    const auto rejected = [](auto change) {
        auto device = suitable(); change(device);
        check(!device_rejection(device).empty());
        check(!choose_device(std::span{&device,1}));
    };
    rejected([](auto& d) { d.api_version = VK_API_VERSION_1_0; });
    rejected([](auto& d) { d.queues[0].graphics = false; });
    rejected([](auto& d) { d.queues[0].present = false; });
    rejected([](auto& d) { d.queues[0].count = 0; });
    rejected([](auto& d) { d.swapchain_extension = false; });
    rejected([](auto& d) { d.present_fences = false; });
    rejected([](auto& d) { d.formats.clear(); });
    rejected([](auto& d) { d.present_modes.clear(); });
    rejected([](auto& d) { d.color_attachment_usage = false; });
    rejected([](auto& d) { d.depth_formats.clear(); });
    check(!choose_device({}));
    std::array candidates{suitable(),suitable(VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU),suitable(VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)};
    check(choose_device(candidates) == 1); // Stable first choice on ties.
    candidates[1].present_fences = false; check(choose_device(candidates) == 2);
    candidates[2].swapchain_extension = false; check(choose_device(candidates) == 0);
    candidates[0].queues = {{1,true,false},{1,false,true}};
    check(choose_device(candidates) == 0); // Integrated and split queues are supported.
}
void depth()
{
    check(!choose_depth_format({}));
    const std::array formats{
        DepthFormatSupport{VK_FORMAT_D24_UNORM_S8_UINT,VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT},
        DepthFormatSupport{VK_FORMAT_D32_SFLOAT,0},
        DepthFormatSupport{VK_FORMAT_D32_SFLOAT_S8_UINT,VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT}};
    check(choose_depth_format(formats) == VK_FORMAT_D32_SFLOAT_S8_UINT);
    check(choose_depth_format(std::span{formats}.first(2)) == VK_FORMAT_D24_UNORM_S8_UINT);
    check(!choose_depth_format(std::span{formats}.subspan(1,1)));
    const std::array preferred{DepthFormatSupport{VK_FORMAT_D32_SFLOAT,VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT}};
    check(choose_depth_format(preferred) == VK_FORMAT_D32_SFLOAT);
}
void pipeline()
{
    const PipelineSignature initial{VK_FORMAT_B8G8R8A8_SRGB,VK_FORMAT_D32_SFLOAT};
    check(pipeline_recreation_required({},initial)); check(!pipeline_recreation_required(initial,initial));
    check(pipeline_recreation_required(initial,{VK_FORMAT_R8G8B8A8_SRGB,VK_FORMAT_D32_SFLOAT}));
    check(pipeline_recreation_required(initial,{VK_FORMAT_B8G8R8A8_SRGB,VK_FORMAT_D24_UNORM_S8_UINT}));
}
void memory()
{
    VkPhysicalDeviceMemoryProperties properties{}; properties.memoryTypeCount = 3;
    properties.memoryTypes[0].propertyFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    properties.memoryTypes[1].propertyFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
    properties.memoryTypes[2].propertyFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    check(choose_memory_type(properties,7,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) == 2);
    check(choose_memory_type(properties,3,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) == 1);
    check(choose_memory_type(properties,7,0,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) == 0);
    check(!choose_memory_type(properties,1,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,0));
    check(!choose_memory_type(properties,0,0,0));
    properties.memoryTypeCount = VK_MAX_MEMORY_TYPES+1;
    rejects([&] { (void)choose_memory_type(properties,1,0,0); });
}
}
int main(int argc, char** argv)
{
    try
    {
        if (argc != 2) throw std::invalid_argument("Expected policy case");
        const std::string_view name = argv[1];
        if (name == "images") images(); else if (name == "formats") formats();
        else if (name == "extents") extents(); else if (name == "queues") queues();
        else if (name == "devices") devices(); else if (name == "depth") depth();
        else if (name == "pipeline") pipeline(); else if (name == "memory") memory();
        else throw std::invalid_argument("Unknown policy case");
        return 0;
    }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
