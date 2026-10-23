#include "application.hpp"

#include "torus.hpp"
#include <imgui.h>
#include <cstddef>
#include <cstring>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace application {
namespace {

VkBuffer vertex_buffer{}, index_buffer{};
VmaAllocation vertex_allocation{}, index_allocation{};
VkPipelineLayout pipeline_layout{};
VkPipeline pipeline{};
uint32_t index_count{};
float pitch = 0.65f, yaw = 0.25f;

// mvp + model, 2*mat4
struct Transform { torus::Mat4 mvp, model; };
static_assert(sizeof(Transform) == 128);

void check(VkResult r, const char* message) {
	if (r != VK_SUCCESS) throw std::runtime_error(message);
}

void createBuffer(const void* data, VkDeviceSize size, VkBufferUsageFlags usage,
                  VkBuffer& buffer, VmaAllocation& allocation) {
	auto& ctx = graphics::internal::context;
	VkBufferCreateInfo info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
	info.size = size;
	info.usage = usage;
	info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

	VmaAllocationCreateInfo alloc{};
	alloc.usage = VMA_MEMORY_USAGE_AUTO;
	alloc.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
	              VMA_ALLOCATION_CREATE_MAPPED_BIT;
	VmaAllocationInfo mapped{};
	check(vmaCreateBuffer(ctx.allocator, &info, &alloc, &buffer, &allocation, &mapped),
	      "Creating mesh buffer failed");
	std::memcpy(mapped.pMappedData, data, static_cast<size_t>(size));
	check(vmaFlushAllocation(ctx.allocator, allocation, 0, size), "Flushing buffer failed");
}

VkShaderModule loadShader(const char* path) {
	std::ifstream file(path, std::ios::binary | std::ios::ate);
	if (!file) throw std::runtime_error(std::string("Cannot open shader: ") + path);
	auto bytes = static_cast<size_t>(file.tellg());
	if (!bytes || bytes % 4) throw std::runtime_error("Invalid SPIR-V size");
	std::vector<uint32_t> code(bytes / 4);
	file.seekg(0);
	file.read(reinterpret_cast<char*>(code.data()), static_cast<std::streamsize>(bytes));
	if (!file) throw std::runtime_error("Reading SPIR-V failed");

	VkShaderModuleCreateInfo info{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
	info.codeSize = bytes;
	info.pCode = code.data();
	VkShaderModule module{};
	check(vkCreateShaderModule(graphics::internal::context.device, &info, nullptr, &module),
	      "Creating shader failed");
	return module;
}

} // namespace

bool initialize() {
	auto& ctx = graphics::internal::context;
	VkShaderModule vert{}, frag{};
	try {
		auto mesh = torus::generate();
		index_count = static_cast<uint32_t>(mesh.indices.size());
		createBuffer(mesh.vertices.data(), mesh.vertices.size() * sizeof(torus::Vertex),
		             VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, vertex_buffer, vertex_allocation);
		createBuffer(mesh.indices.data(), mesh.indices.size() * sizeof(uint32_t),
		             VK_BUFFER_USAGE_INDEX_BUFFER_BIT, index_buffer, index_allocation);

		VkPushConstantRange range{VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(Transform)};
		VkPipelineLayoutCreateInfo layout{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
		layout.pushConstantRangeCount = 1;
		layout.pPushConstantRanges = &range;
		check(vkCreatePipelineLayout(ctx.device, &layout, nullptr, &pipeline_layout),
		      "Creating pipeline layout failed");

		vert = loadShader("shaders/torus.vert.spv");
		frag = loadShader("shaders/torus.frag.spv");

		VkPipelineShaderStageCreateInfo stages[2]{};
		for (auto& s : stages) {
			s.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
			s.pName = "main";
		}
		stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
		stages[0].module = vert;
		stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
		stages[1].module = frag;

		VkVertexInputBindingDescription binding{0, sizeof(torus::Vertex), VK_VERTEX_INPUT_RATE_VERTEX};
		VkVertexInputAttributeDescription attrs[] = {
			{0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(torus::Vertex, position)},
			{1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(torus::Vertex, normal)},
		};
		VkPipelineVertexInputStateCreateInfo input{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
		input.vertexBindingDescriptionCount = 1;
		input.pVertexBindingDescriptions = &binding;
		input.vertexAttributeDescriptionCount = 2;
		input.pVertexAttributeDescriptions = attrs;

		VkPipelineInputAssemblyStateCreateInfo assembly{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
		assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

		VkPipelineViewportStateCreateInfo viewport{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
		viewport.viewportCount = viewport.scissorCount = 1;

		VkPipelineRasterizationStateCreateInfo raster{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
		raster.polygonMode = VK_POLYGON_MODE_FILL;
		raster.cullMode = VK_CULL_MODE_NONE;
		raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
		raster.lineWidth = 1;

		VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
		ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

		// depth test
		VkPipelineDepthStencilStateCreateInfo depth{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
		depth.depthTestEnable = depth.depthWriteEnable = VK_TRUE;
		depth.depthCompareOp = VK_COMPARE_OP_LESS;

		VkPipelineColorBlendAttachmentState attachment{};
		attachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
		                            VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
		VkPipelineColorBlendStateCreateInfo blend{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
		blend.attachmentCount = 1;
		blend.pAttachments = &attachment;

		// viewport/scissor задаём каждый кадр
		VkDynamicState states[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
		VkPipelineDynamicStateCreateInfo dynamic{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
		dynamic.dynamicStateCount = 2;
		dynamic.pDynamicStates = states;

		VkGraphicsPipelineCreateInfo info{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
		info.stageCount = 2;
		info.pStages = stages;
		info.pVertexInputState = &input;
		info.pInputAssemblyState = &assembly;
		info.pViewportState = &viewport;
		info.pRasterizationState = &raster;
		info.pMultisampleState = &ms;
		info.pDepthStencilState = &depth;
		info.pColorBlendState = &blend;
		info.pDynamicState = &dynamic;
		info.layout = pipeline_layout;
		info.renderPass = ctx.render_pass;
		check(vkCreateGraphicsPipelines(ctx.device, VK_NULL_HANDLE, 1, &info, nullptr, &pipeline),
		      "Creating graphics pipeline failed");

		vkDestroyShaderModule(ctx.device, frag, nullptr);
		vkDestroyShaderModule(ctx.device, vert, nullptr);
		ImGui::StyleColorsDark();
		return true;
	} catch (const std::exception& e) {
		std::cerr << e.what() << '\n';
		if (frag) vkDestroyShaderModule(ctx.device, frag, nullptr);
		if (vert) vkDestroyShaderModule(ctx.device, vert, nullptr);
		shutdown();
		return false;
	}
}

void shutdown() {
	auto& context = graphics::internal::context;
	vkQueueWaitIdle(context.graphics_queue);
	if (pipeline) vkDestroyPipeline(context.device, pipeline, nullptr);
	if (pipeline_layout) vkDestroyPipelineLayout(context.device, pipeline_layout, nullptr);
	if (index_buffer) vmaDestroyBuffer(context.allocator, index_buffer, index_allocation);
	if (vertex_buffer) vmaDestroyBuffer(context.allocator, vertex_buffer, vertex_allocation);
	pipeline = {};
	pipeline_layout = {};
	index_buffer = {};
	vertex_buffer = {};
}

void update([[maybe_unused]] double time) {
	const auto& io = ImGui::GetIO();
	if (!io.WantCaptureMouse && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
		yaw += io.MouseDelta.x * 0.008f;
		pitch += io.MouseDelta.y * 0.008f;
	}

	ImGui::SetNextWindowPos(ImVec2(18, 18), ImGuiCond_Always);
	ImGui::SetNextWindowSize(ImVec2(288, 190), ImGuiCond_Always);
	ImGui::Begin("Lab 1 | Variant 7", nullptr,
	             ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse);
	ImGui::TextUnformatted("TORUS - basic assignment");
	ImGui::Separator();
	ImGui::Text("R = %.1f    r = %.1f", torus::major_radius, torus::minor_radius);
	ImGui::TextUnformatted("2048 vertices / 4096 triangles");
	ImGui::TextUnformatted("Perspective projection (45 deg)");
	ImGui::TextUnformatted("Drag LMB outside panel to rotate.");
	if (ImGui::Button("Reset view")) {
		pitch = 0.65f;
		yaw = 0.25f;
	}
	ImGui::Text("%.1f FPS", io.Framerate);
	ImGui::End();
}

void render(const graphics::internal::FrameData& fd) {
	auto& ctx = graphics::internal::context;
	if (!fd.command_buffer) return;

	check(vkResetCommandBuffer(fd.command_buffer, 0), "Resetting command buffer failed");
	VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
	begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
	check(vkBeginCommandBuffer(fd.command_buffer, &begin), "Beginning command buffer failed");

	VkClearValue clear[2]{};
	clear[0].color = {{0.045f, 0.065f, 0.095f, 1}};
	clear[1].depthStencil = {1, 0}; // 1 = far

	VkRenderPassBeginInfo pass{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
	pass.renderPass = ctx.render_pass;
	pass.framebuffer = fd.framebuffer;
	pass.renderArea.extent = ctx.swapchain_extent;
	pass.clearValueCount = 2;
	pass.pClearValues = clear;
	vkCmdBeginRenderPass(fd.command_buffer, &pass, VK_SUBPASS_CONTENTS_INLINE);

	const float w = static_cast<float>(ctx.swapchain_extent.width);
	const float h = static_cast<float>(ctx.swapchain_extent.height);
	VkViewport viewport{0, 0, w, h, 0, 1};
	VkRect2D scissor{{0, 0}, ctx.swapchain_extent};
	vkCmdSetViewport(fd.command_buffer, 0, 1, &viewport);
	vkCmdSetScissor(fd.command_buffer, 0, 1, &scissor);

	Transform transform{};
	transform.model = torus::rotate(pitch, yaw);
	auto view = torus::identity();
	view.data[14] = -6.5f; // камера отодвинута по Z
	transform.mvp = torus::multiply(torus::perspective(w / h),
	                                torus::multiply(view, transform.model));

	vkCmdBindPipeline(fd.command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
	const VkDeviceSize offset = 0;
	vkCmdBindVertexBuffers(fd.command_buffer, 0, 1, &vertex_buffer, &offset);
	vkCmdBindIndexBuffer(fd.command_buffer, index_buffer, 0, VK_INDEX_TYPE_UINT32);
	vkCmdPushConstants(fd.command_buffer, pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT, 0,
	                   sizeof(transform), &transform);
	vkCmdDrawIndexed(fd.command_buffer, index_count, 1, 0, 0, 0);

	vkCmdEndRenderPass(fd.command_buffer);
	check(vkEndCommandBuffer(fd.command_buffer), "Ending command buffer failed");
}

} // namespace application
