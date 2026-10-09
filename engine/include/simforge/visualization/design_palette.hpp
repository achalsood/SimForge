#pragma once
#include <glm/vec3.hpp>

namespace simforge::visualization::design
{
// Linear RGB equivalents of the display palette. Vulkan's sRGB attachment
// encodes both scene and ImGui colors once; never pass display hex values raw.
inline constexpr glm::vec3 navy{0.00335f,0.00605f,0.01444f}; // #0B1220
inline constexpr glm::vec3 panel{0.00605f,0.01161f,0.02416f}; // #121C2B
inline constexpr glm::vec3 elevated{0.01096f,0.02217f,0.04374f}; // #1B293B
inline constexpr glm::vec3 border{0.02416f,0.04374f,0.08022f}; // #2B3B50
inline constexpr glm::vec3 text{0.87962f,0.91310f,0.94731f}; // #F1F5F9
inline constexpr glm::vec3 secondary{0.29614f,0.36625f,0.47932f}; // #94A3B8
inline constexpr glm::vec3 processing{0.03955f,0.50888f,0.93869f}; // #38BDF8
inline constexpr glm::vec3 waiting{0.96469f,0.52099f,0.01764f}; // #FBBF24
inline constexpr glm::vec3 completed{0.03434f,0.65141f,0.33716f}; // #34D399
inline constexpr glm::vec3 error{0.96469f,0.16513f,0.23455f}; // #FB7185
inline constexpr glm::vec3 idle{0.23f,0.29f,0.36f}, selected{1.0f,0.78f,0.20f};
}
