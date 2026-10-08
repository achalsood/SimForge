#include <simforge/renderer/camera.hpp>
#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/geometric.hpp>
#include <glm/gtc/constants.hpp>
#include <cmath>
#include <stdexcept>

namespace simforge::renderer
{
bool finite(const glm::vec3& value) noexcept
{ return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z); }
bool finite(const glm::mat4& matrix) noexcept
{
    for (int col = 0; col < 4; ++col)
        for (int row = 0; row < 4; ++row)
            if (!std::isfinite(matrix[col][row])) return false;
    return true;
}
void Camera::validate() const
{
    if (!finite(position) || !finite(target) || !finite(up) ||
        !std::isfinite(vertical_fov) || vertical_fov <= 0 || vertical_fov >= glm::pi<float>() ||
        !std::isfinite(aspect_ratio) || aspect_ratio <= 0 ||
        !std::isfinite(near_plane) || !std::isfinite(far_plane) || near_plane <= 0 || far_plane <= near_plane)
        throw std::invalid_argument("Invalid camera vectors, FOV, aspect ratio or clipping planes");
    const glm::dvec3 direction = glm::dvec3{target}-glm::dvec3{position};
    const glm::dvec3 vertical{up};
    if (glm::dot(direction,direction) <= 1e-12 || glm::dot(vertical,vertical) <= 1e-12 ||
        glm::length(glm::cross(glm::normalize(direction),glm::normalize(vertical))) <= 1e-6)
        throw std::invalid_argument("Camera target/up must define a nondegenerate basis");
}
glm::mat4 Camera::view() const
{
    validate();
    const glm::mat4 result{glm::lookAtRH(glm::dvec3{position},glm::dvec3{target},glm::dvec3{up})};
    if (!finite(result)) throw std::invalid_argument("Camera view is not representable as GPU floats");
    return result;
}
glm::mat4 Camera::projection() const
{
    validate();
    glm::mat4 result{glm::perspectiveRH_ZO(static_cast<double>(vertical_fov),static_cast<double>(aspect_ratio),
        static_cast<double>(near_plane),static_cast<double>(far_plane))};
    result[1][1] = -result[1][1];
    if (!finite(result)) throw std::invalid_argument("Camera projection is not representable as GPU floats");
    return result;
}
glm::mat4 Camera::view_projection() const
{
    const auto result = projection()*view();
    if (!finite(result)) throw std::invalid_argument("Camera matrix overflow");
    return result;
}
glm::mat4 RenderTransform::matrix() const
{
    if (!finite(position) || !finite(rotation) || !finite(scale) || scale.x <= 0 || scale.y <= 0 || scale.z <= 0)
        throw std::invalid_argument("Render transforms require finite values and positive scales");
    glm::mat4 result = glm::translate(glm::mat4{1.0f},position);
    result = glm::rotate(result,rotation.z,glm::vec3{0,0,1});
    result = glm::rotate(result,rotation.y,glm::vec3{0,1,0});
    result = glm::rotate(result,rotation.x,glm::vec3{1,0,0});
    result = glm::scale(result,scale);
    if (!finite(result)) throw std::invalid_argument("Render transform matrix overflow");
    return result;
}
}
