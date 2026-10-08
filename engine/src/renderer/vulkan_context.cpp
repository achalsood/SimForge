#include "vulkan_context.hpp"
#include "vulkan_support.hpp"
#include <algorithm>
#include <cstring>
#include <iostream>

namespace simforge::renderer::detail
{
namespace
{
VKAPI_ATTR VkBool32 VKAPI_CALL message(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
    VkDebugUtilsMessageTypeFlagsEXT, const VkDebugUtilsMessengerCallbackDataEXT* data, void* user) noexcept
{
    auto& diagnostics = *static_cast<Diagnostics*>(user);
    const bool error = (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) != 0;
    const bool warning = (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) != 0;
    if (error) ++diagnostics.errors; else if (warning) ++diagnostics.warnings;
    if (error || warning)
    {
        try { std::cerr << (error ? "Vulkan validation error: " : "Vulkan validation warning: ") << data->pMessage << '\n'; }
        catch (...) {} // C ABI callbacks must never propagate an exception.
    }
    return VK_FALSE;
}
}
VulkanContext::VulkanContext(const platform::Window& window, RendererOptions options, Diagnostics& diagnostics) : window_{window}
{
    try
    {
        std::uint32_t version = VK_API_VERSION_1_0;
        const auto enumerate_version = reinterpret_cast<PFN_vkEnumerateInstanceVersion>(vkGetInstanceProcAddr(nullptr,"vkEnumerateInstanceVersion"));
        if (enumerate_version) check(enumerate_version(&version),"Query Vulkan loader version");
        if (version < VK_API_VERSION_1_1) throw std::runtime_error("Vulkan 1.1 loader required; update the GPU driver");
        loader_version_ = version;
        const auto available = enumerate<VkExtensionProperties>([](auto count, auto values) { return vkEnumerateInstanceExtensionProperties(nullptr,count,values); },"Enumerate instance extensions");
        const auto has = [&](const char* name) { return std::any_of(available.begin(),available.end(),[&](const auto& p) { return std::strcmp(p.extensionName,name) == 0; }); };
        auto extensions = window_.instance_extensions();
        extensions.push_back(VK_KHR_GET_SURFACE_CAPABILITIES_2_EXTENSION_NAME);
        khr_maintenance_ = has(VK_KHR_SURFACE_MAINTENANCE_1_EXTENSION_NAME);
        ext_maintenance_ = has(VK_EXT_SURFACE_MAINTENANCE_1_EXTENSION_NAME);
        if (!khr_maintenance_ && !ext_maintenance_)
            throw std::runtime_error("Vulkan surface maintenance1 unavailable; update the Vulkan loader/GPU driver");
        if (khr_maintenance_) extensions.push_back(VK_KHR_SURFACE_MAINTENANCE_1_EXTENSION_NAME);
        if (ext_maintenance_) extensions.push_back(VK_EXT_SURFACE_MAINTENANCE_1_EXTENSION_NAME);
        for (auto name : extensions)
            if (!has(name)) throw std::runtime_error(std::string{"Required Vulkan instance extension missing: "}+name);
        const auto layers = enumerate<VkLayerProperties>([](auto count, auto values) { return vkEnumerateInstanceLayerProperties(count,values); },"Enumerate Vulkan layers");
        const char* validation_layer = "VK_LAYER_KHRONOS_validation";
        validation_ = (options.validation || options.require_validation) && std::any_of(layers.begin(),layers.end(),[&](const auto& p) { return std::strcmp(p.layerName,validation_layer) == 0; });
        if (options.require_validation && (!validation_ || !has(VK_EXT_DEBUG_UTILS_EXTENSION_NAME)))
            throw std::runtime_error("Required Khronos validation/debug-utils support missing; install/enable Vulkan SDK layers");
        if (options.validation && !validation_) std::cerr << "Vulkan diagnostic: Khronos validation unavailable; Debug validation is disabled.\n";
        if (validation_ && has(VK_EXT_DEBUG_UTILS_EXTENSION_NAME)) extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
        const auto layer_extensions = validation_ ? enumerate<VkExtensionProperties>([&](auto count, auto values) { return vkEnumerateInstanceExtensionProperties(validation_layer,count,values); },"Enumerate validation layer extensions") : std::vector<VkExtensionProperties>{};
        sync_validation_ = validation_ && std::any_of(layer_extensions.begin(),layer_extensions.end(),[](const auto& p) { return std::strcmp(p.extensionName,VK_EXT_VALIDATION_FEATURES_EXTENSION_NAME) == 0; });
        if (sync_validation_) extensions.push_back(VK_EXT_VALIDATION_FEATURES_EXTENSION_NAME);
        VkDebugUtilsMessengerCreateInfoEXT debug{VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT};
        debug.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
        debug.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
        debug.pfnUserCallback = message; debug.pUserData = &diagnostics;
        const VkValidationFeatureEnableEXT feature = VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT;
        VkValidationFeaturesEXT validation_features{VK_STRUCTURE_TYPE_VALIDATION_FEATURES_EXT};
        validation_features.enabledValidationFeatureCount = 1; validation_features.pEnabledValidationFeatures = &feature;
        if (sync_validation_) debug.pNext = &validation_features;
        VkApplicationInfo application{VK_STRUCTURE_TYPE_APPLICATION_INFO}; application.pApplicationName = "SimForge";
        application.pEngineName = "SimForge"; application.apiVersion = VK_API_VERSION_1_1;
        VkInstanceCreateInfo info{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO}; info.pApplicationInfo = &application;
        info.enabledExtensionCount = static_cast<std::uint32_t>(extensions.size()); info.ppEnabledExtensionNames = extensions.data();
        if (validation_) { info.enabledLayerCount = 1; info.ppEnabledLayerNames = &validation_layer; }
        if (validation_ && has(VK_EXT_DEBUG_UTILS_EXTENSION_NAME)) info.pNext = &debug;
        else if (sync_validation_) info.pNext = &validation_features;
        check(vkCreateInstance(&info,nullptr,&instance_),"Create Vulkan instance");
        if (validation_ && has(VK_EXT_DEBUG_UTILS_EXTENSION_NAME))
        {
            const auto create = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(vkGetInstanceProcAddr(instance_,"vkCreateDebugUtilsMessengerEXT"));
            destroy_messenger_ = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(vkGetInstanceProcAddr(instance_,"vkDestroyDebugUtilsMessengerEXT"));
            if (!create || !destroy_messenger_) throw std::runtime_error("Vulkan debug messenger functions unavailable");
            debug.pNext = nullptr;
            check(create(instance_,&debug,nullptr,&messenger_),"Create Vulkan debug messenger");
        }
        surface_ = window_.create_surface(instance_);
        std::cout << "Vulkan validation: " << (validation_ ? validation_layer : "disabled") << " | Synchronization validation: " << (sync_validation_ ? "enabled" : "disabled") << '\n';
    }
    catch (...) { destroy(); throw; }
}
VulkanContext::~VulkanContext() { destroy(); }
void VulkanContext::destroy() noexcept
{
    if (surface_) vkDestroySurfaceKHR(instance_,surface_,nullptr);
    if (messenger_ && destroy_messenger_) destroy_messenger_(instance_,messenger_,nullptr);
    if (instance_) vkDestroyInstance(instance_,nullptr);
    surface_ = VK_NULL_HANDLE; messenger_ = VK_NULL_HANDLE; instance_ = VK_NULL_HANDLE;
}
}
