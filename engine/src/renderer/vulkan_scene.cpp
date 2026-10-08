#include "vulkan_scene.hpp"
#include "vulkan_support.hpp"
#include "scene_vert.hpp"
#include "scene_frag.hpp"
#include <simforge/renderer/geometry.hpp>
#include <array>
#include <chrono>

namespace simforge::renderer::detail
{
namespace
{
struct alignas(16) DrawConstants { std::array<float,16> mvp; std::array<float,4> tint; };
static_assert(sizeof(DrawConstants) == 80 && offsetof(DrawConstants,tint) == 64);
VkShaderModule shader(VkDevice device, std::span<const std::uint32_t> words)
{
    VkShaderModuleCreateInfo info{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    info.codeSize = words.size_bytes(); info.pCode = words.data();
    VkShaderModule module = VK_NULL_HANDLE;
    check(vkCreateShaderModule(device,&info,nullptr,&module),"Create embedded scene shader module");
    return module;
}
}
VulkanScene::VulkanScene(const VulkanDevice& device) : device_{device}
{
    try
    {
        std::vector<Vertex> vertices; std::vector<std::uint32_t> indices;
        for (std::size_t i = 0; i < builtin_mesh_count; ++i)
        {
            const auto mesh = make_builtin_mesh(static_cast<MeshId>(i+1));
            ranges_[i] = {static_cast<std::uint32_t>(mesh.indices.size()),static_cast<std::uint32_t>(indices.size()),
                static_cast<std::int32_t>(vertices.size()),static_cast<std::uint32_t>(mesh.vertices.size())};
            vertices.insert(vertices.end(),mesh.vertices.begin(),mesh.vertices.end());
            indices.insert(indices.end(),mesh.indices.begin(),mesh.indices.end());
        }
        const auto vertex_bytes = std::as_bytes(std::span{vertices});
        const auto index_bytes = std::as_bytes(std::span{indices});
        vertices_ = std::make_unique<VulkanBuffer>(device,vertex_bytes.size(),VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
        indices_ = std::make_unique<VulkanBuffer>(device,index_bytes.size(),VK_BUFFER_USAGE_INDEX_BUFFER_BIT);
        vertices_->upload(vertex_bytes); indices_->upload(index_bytes);
        vertex_shader_ = shader(device.handle(),shaders::scene_vert);
        fragment_shader_ = shader(device.handle(),shaders::scene_frag);
        const VkPushConstantRange push{VK_SHADER_STAGE_VERTEX_BIT,0,sizeof(DrawConstants)};
        VkPipelineLayoutCreateInfo layout{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        layout.pushConstantRangeCount = 1; layout.pPushConstantRanges = &push;
        check(vkCreatePipelineLayout(device.handle(),&layout,nullptr,&layout_),"Create 80-byte scene push-constant layout");
    }
    catch (...) { destroy(); throw; }
}
VulkanScene::~VulkanScene() { destroy(); }
void VulkanScene::ensure_pipeline(VkRenderPass pass, PipelineSignature signature)
{
    if (!pipeline_recreation_required(signature_,signature)) return;
    std::array<VkPipelineShaderStageCreateInfo,2> stages{};
    for (auto& stage : stages) { stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO; stage.pName = "main"; }
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT; stages[0].module = vertex_shader_;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT; stages[1].module = fragment_shader_;
    const VkBool32 encode_srgb = requires_srgb_encoding(signature.color);
    const VkSpecializationMapEntry entry{0,0,sizeof(encode_srgb)};
    const VkSpecializationInfo specialization{1,&entry,sizeof(encode_srgb),&encode_srgb};
    stages[1].pSpecializationInfo = &specialization;
    const VkVertexInputBindingDescription binding{0,sizeof(Vertex),VK_VERTEX_INPUT_RATE_VERTEX};
    const std::array attributes{VkVertexInputAttributeDescription{0,0,VK_FORMAT_R32G32B32_SFLOAT,offsetof(Vertex,position)},
        VkVertexInputAttributeDescription{1,0,VK_FORMAT_R32G32B32_SFLOAT,offsetof(Vertex,color)}};
    VkPipelineVertexInputStateCreateInfo vertex{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    vertex.vertexBindingDescriptionCount = 1; vertex.pVertexBindingDescriptions = &binding;
    vertex.vertexAttributeDescriptionCount = static_cast<std::uint32_t>(attributes.size()); vertex.pVertexAttributeDescriptions = attributes.data();
    VkPipelineInputAssemblyStateCreateInfo assembly{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineViewportStateCreateInfo viewport{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO}; viewport.viewportCount = viewport.scissorCount = 1;
    VkPipelineRasterizationStateCreateInfo raster{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    raster.polygonMode = VK_POLYGON_MODE_FILL; raster.cullMode = VK_CULL_MODE_BACK_BIT;
    raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE; raster.lineWidth = 1;
    VkPipelineMultisampleStateCreateInfo samples{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO}; samples.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineDepthStencilStateCreateInfo depth{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    depth.depthTestEnable = depth.depthWriteEnable = VK_TRUE; depth.depthCompareOp = VK_COMPARE_OP_LESS;
    VkPipelineColorBlendAttachmentState color{}; color.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    VkPipelineColorBlendStateCreateInfo blend{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO}; blend.attachmentCount = 1; blend.pAttachments = &color;
    const std::array states{VK_DYNAMIC_STATE_VIEWPORT,VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamic{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dynamic.dynamicStateCount = static_cast<std::uint32_t>(states.size()); dynamic.pDynamicStates = states.data();
    VkGraphicsPipelineCreateInfo info{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    info.stageCount = 2; info.pStages = stages.data(); info.pVertexInputState = &vertex; info.pInputAssemblyState = &assembly;
    info.pViewportState = &viewport; info.pRasterizationState = &raster; info.pMultisampleState = &samples;
    info.pDepthStencilState = &depth; info.pColorBlendState = &blend; info.pDynamicState = &dynamic; info.layout = layout_; info.renderPass = pass;
    VkPipeline replacement = VK_NULL_HANDLE;
    const auto started = std::chrono::steady_clock::now();
    const auto result = vkCreateGraphicsPipelines(device_.handle(),VK_NULL_HANDLE,1,&info,nullptr,&replacement);
    if (result != VK_SUCCESS)
    {
        if (replacement) vkDestroyPipeline(device_.handle(),replacement,nullptr);
        check(result,"Create scene graphics pipeline");
    }
    if (pipeline_) vkDestroyPipeline(device_.handle(),pipeline_,nullptr); // Caller drains GPU before rebuilding.
    pipeline_ = replacement; signature_ = signature; ++pipeline_generations_;
    pipeline_creation_ms_ += std::chrono::duration<double,std::milli>{std::chrono::steady_clock::now()-started}.count();
}
DrawCounts VulkanScene::record(VkCommandBuffer command, VkExtent2D extent, const RenderFrameData& frame) const
{
    if (!pipeline_) throw std::logic_error("Scene pipeline has not been created");
    const auto camera = frame_camera(frame,static_cast<float>(extent.width)/static_cast<float>(extent.height));
    const auto view_projection = camera.view_projection();
    const VkViewport viewport{0,0,static_cast<float>(extent.width),static_cast<float>(extent.height),0,1};
    const VkRect2D scissor{{0,0},extent};
    vkCmdSetViewport(command,0,1,&viewport); vkCmdSetScissor(command,0,1,&scissor);
    vkCmdBindPipeline(command,VK_PIPELINE_BIND_POINT_GRAPHICS,pipeline_);
    const auto vertex = vertices_->handle(); const VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(command,0,1,&vertex,&offset); vkCmdBindIndexBuffer(command,indices_->handle(),0,VK_INDEX_TYPE_UINT32);
    DrawCounts counts;
    for (const auto& object : frame.objects)
    {
        const auto& range = ranges_.at(static_cast<std::size_t>(object.mesh)-1);
        const auto matrix = view_projection*object.transform.matrix();
        if (!finite(matrix)) throw std::invalid_argument("Drawable object matrix overflow");
        DrawConstants push{};
        for (int col = 0; col < 4; ++col) for (int row = 0; row < 4; ++row) push.mvp[col*4+row] = matrix[col][row];
        push.tint = {object.color.x,object.color.y,object.color.z,1};
        vkCmdPushConstants(command,layout_,VK_SHADER_STAGE_VERTEX_BIT,0,sizeof(push),&push);
        vkCmdDrawIndexed(command,range.count,1,range.first_index,range.vertex_offset,0);
        ++counts.calls; counts.vertices += range.vertices; counts.indices += range.count;
    }
    return counts;
}
void VulkanScene::destroy() noexcept
{
    if (pipeline_) vkDestroyPipeline(device_.handle(),pipeline_,nullptr);
    if (layout_) vkDestroyPipelineLayout(device_.handle(),layout_,nullptr);
    if (vertex_shader_) vkDestroyShaderModule(device_.handle(),vertex_shader_,nullptr);
    if (fragment_shader_) vkDestroyShaderModule(device_.handle(),fragment_shader_,nullptr);
    pipeline_ = VK_NULL_HANDLE; layout_ = VK_NULL_HANDLE; vertex_shader_ = fragment_shader_ = VK_NULL_HANDLE;
    indices_.reset(); vertices_.reset();
}
}
