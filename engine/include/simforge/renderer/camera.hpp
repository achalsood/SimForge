#pragma once
#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

namespace simforge::renderer
{
// Right handed, Y up, local camera forward -Z. Radians; column vectors.
struct Camera
{
    glm::vec3 position{10.0f,11.0f,14.0f};
    glm::vec3 target{0.0f,0.2f,0.0f};
    glm::vec3 up{0.0f,1.0f,0.0f};
    float vertical_fov{0.785398163f};
    float aspect_ratio{16.0f/9.0f};
    float near_plane{0.1f}, far_plane{100.0f};
    void validate() const;
    [[nodiscard]] glm::mat4 view() const;
    // Explicit RH zero-to-one depth; projection flips Y for a positive viewport.
    [[nodiscard]] glm::mat4 projection() const;
    [[nodiscard]] glm::mat4 view_projection() const;
};
struct RenderTransform
{
    glm::vec3 position{0.0f};
    glm::vec3 rotation{0.0f}; // Euler XYZ radians, applied X then Y then Z.
    glm::vec3 scale{1.0f}; // Positive scales preserve outward winding.
    [[nodiscard]] glm::mat4 matrix() const; // T * Rz * Ry * Rx * S.
};
[[nodiscard]] bool finite(const glm::vec3& vector) noexcept;
[[nodiscard]] bool finite(const glm::mat4& matrix) noexcept;
}
