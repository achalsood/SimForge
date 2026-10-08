#pragma once
#include "vulkan_buffer.hpp"
#include <simforge/renderer/render_frame_data.hpp>
#include <array>
#include <memory>

namespace simforge::renderer::detail
{
struct DrawCounts { std::uint32_t calls{0}, vertices{0}, indices{0}; };
class VulkanScene
{
public:
    explicit VulkanScene(const VulkanDevice& device);
    ~VulkanScene();
    VulkanScene(const VulkanScene&) = delete;
    VulkanScene& operator=(const VulkanScene&) = delete;
    void ensure_pipeline(VkRenderPass pass, PipelineSignature signature);
    [[nodiscard]] DrawCounts record(VkCommandBuffer command, VkExtent2D extent, const RenderFrameData& frame) const;
    [[nodiscard]] std::uint64_t pipeline_generations() const noexcept { return pipeline_generations_; }
    [[nodiscard]] double pipeline_creation_ms() const noexcept { return pipeline_creation_ms_; }
private:
    void destroy() noexcept;
    struct Range { std::uint32_t count, first_index; std::int32_t vertex_offset; std::uint32_t vertices; };
    const VulkanDevice& device_;
    std::unique_ptr<VulkanBuffer> vertices_, indices_;
    std::array<Range,3> ranges_{};
    VkShaderModule vertex_shader_{VK_NULL_HANDLE}, fragment_shader_{VK_NULL_HANDLE};
    VkPipelineLayout layout_{VK_NULL_HANDLE};
    VkPipeline pipeline_{VK_NULL_HANDLE};
    std::optional<PipelineSignature> signature_;
    std::uint64_t pipeline_generations_{0};
    double pipeline_creation_ms_{0};
};
}
