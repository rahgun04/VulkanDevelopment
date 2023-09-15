

#include <SDL.h>
#include <SDL_vulkan.h>


// --- other includes ---

#include "vk_textures.h"


#include <iostream>
#include <chrono>

using namespace std::chrono;

//bootstrap library
#include "VkBootstrap.h"

#define VMA_IMPLEMENTATION
#include "vk_mem_alloc.h"
#include <stdio.h>
#include <iostream>


#include "check.h"


#include <cstring>

#include <glm/gtx/transform.hpp>
#include <glm/gtx/euler_angles.hpp>
#include "vk_engine.h"
#include <vk_types.h>
#include <vk_init.h>
#include "spdlog/spdlog.h"
#include "converter.h"
#include <set>
#include "imgui.h"
#include "imgui_impl_sdl2.h"
#include "imgui_impl_vulkan.h"


	 

namespace Math {
	namespace Pose {
		XrPosef Identity() {
			XrPosef t{};
			t.orientation.w = 1;
			return t;
		}

		XrPosef Translation(const XrVector3f& translation) {
			XrPosef t = Identity();
			t.position = translation;
			return t;
		}

		XrPosef RotateCCWAboutYAxis(float radians, XrVector3f translation) {
			XrPosef t = Identity();
			t.orientation.x = 0.f;
			t.orientation.y = std::sin(radians * 0.5f);
			t.orientation.z = 0.f;
			t.orientation.w = std::cos(radians * 0.5f);
			t.position = translation;
			return t;
		}
	}  // namespace Pose
}  // namespace Math



//we want to immediately abort when there is an error. In normal engines this would give an error message to the user, or perform a dump of state.
using namespace std;
#define VK_CHECK(x)                                                 \
	do                                                              \
	{                                                               \
		VkResult err = x;                                           \
		if (err)                                                    \
		{                                                           \
			std::cout <<"VKCHECKDetected Vulkan error: " << err << std::endl; \
			abort();                                                \
		}                                                           \
	} while (0)

#define CHECKVK(x)                                                 \
	do                                                              \
	{                                                               \
		VkResult err = x;                                           \
		if (err)                                                    \
		{                                                           \
			std::cout <<"CHECKVKDetected Vulkan error: " << err << std::endl; \
			abort();                                                \
		}                                                           \
	} while (0)


#define countof(x) (sizeof(x)/sizeof((x)[0]))



inline std::string GetXrVersionString(XrVersion ver) {
	return Fmt("%d.%d.%d", XR_VERSION_MAJOR(ver), XR_VERSION_MINOR(ver), XR_VERSION_PATCH(ver));
}

inline bool EqualsIgnoreCase(const std::string& s1, const std::string& s2, const std::locale& loc = std::locale()) {
	const std::ctype<char>& ctype = std::use_facet<std::ctype<char>>(loc);
	const auto compareCharLower = [&](char c1, char c2) { return ctype.tolower(c1) == ctype.tolower(c2); };
	return s1.size() == s2.size() && std::equal(s1.begin(), s1.end(), s2.begin(), compareCharLower);
}

void VulkanEngine::init_imgui()
{
	//1: create descriptor pool for IMGUI
	// the size of the pool is very oversize, but it's copied from imgui demo itself.
	VkDescriptorPoolSize pool_sizes[] =
	{
		{ VK_DESCRIPTOR_TYPE_SAMPLER, 1000 },
		{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000 },
		{ VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1000 },
		{ VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1000 },
		{ VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1000 },
		{ VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1000 },
		{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1000 },
		{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1000 },
		{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1000 },
		{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1000 },
		{ VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1000 }
	};

	VkDescriptorPoolCreateInfo pool_info = {};
	pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
	pool_info.maxSets = 1000;
	pool_info.poolSizeCount = std::size(pool_sizes);
	pool_info.pPoolSizes = pool_sizes;

	VkDescriptorPool imguiPool;
	VK_CHECK(vkCreateDescriptorPool(_device, &pool_info, nullptr, &imguiPool));


	// 2: initialize imgui library

	//this initializes the core structures of imgui
	
	//_imguictx = ImGui::CreateContext();
	

	//this initializes imgui for SDL
	ImGui_ImplSDL2_InitForVulkan(_window);

	//this initializes imgui for Vulkan
	ImGui_ImplVulkan_InitInfo init_info = {};
	init_info.Instance = _instance;
	init_info.PhysicalDevice = _chosenGPU;
	init_info.Device = _device;
	init_info.Queue = _graphicsQueue;
	init_info.DescriptorPool = imguiPool;
	init_info.MinImageCount = 3;
	init_info.ImageCount = 3;
	init_info.MSAASamples = VK_SAMPLE_COUNT_1_BIT;

	ImGui_ImplVulkan_Init(&init_info, _xrRenderTargets[1].renderPass);

	//execute a gpu command to upload imgui font textures
	immediate_submit([&](VkCommandBuffer cmd) {
		ImGui_ImplVulkan_CreateFontsTexture(cmd);
		});

	//clear font textures from cpu data
	ImGui_ImplVulkan_DestroyFontUploadObjects();

	//add the destroy the imgui created structures
	_mainDeletionQueue.push_function([=]() {

		vkDestroyDescriptorPool(_device, imguiPool, nullptr);
		ImGui_ImplVulkan_Shutdown();
		});
}


VkPipeline PipelineBuilder::build_pipeline(VkDevice device, VkRenderPass pass) {
	//make viewport state from our stored viewport and scissor.
	//at the moment we won't support multiple viewports or scissors
	VkPipelineViewportStateCreateInfo viewportState = {};
	viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
	viewportState.pNext = nullptr;

	viewportState.viewportCount = 1;
	viewportState.pViewports = &_viewport;
	viewportState.scissorCount = 1;
	viewportState.pScissors = &_scissor;

	//setup dummy color blending. We aren't using transparent objects yet
	//the blending is just "no blend", but we do write to the color attachment
	VkPipelineColorBlendStateCreateInfo colorBlending = {};
	colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
	colorBlending.pNext = nullptr;

	colorBlending.logicOpEnable = VK_FALSE;
	colorBlending.logicOp = VK_LOGIC_OP_COPY;
	colorBlending.attachmentCount = 1;
	colorBlending.pAttachments = &_colorBlendAttachment;

	VkGraphicsPipelineCreateInfo pipelineInfo = {};

	//build the actual pipeline
	//we now use all of the info structs we have been writing into into this one to create the pipeline
	//VkGraphicsPipelineCreateInfo pipelineInfo = {};
	pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
	pipelineInfo.pNext = nullptr;

	pipelineInfo.stageCount = _shaderStages.size();
	pipelineInfo.pStages = _shaderStages.data();
	pipelineInfo.pVertexInputState = &_vertexInputInfo;
	pipelineInfo.pInputAssemblyState = &_inputAssembly;
	pipelineInfo.pViewportState = &viewportState;
	pipelineInfo.pRasterizationState = &_rasterizer;
	pipelineInfo.pMultisampleState = &_multisampling;
	pipelineInfo.pColorBlendState = &colorBlending;
	pipelineInfo.layout = _pipelineLayout;
	pipelineInfo.renderPass = pass;
	pipelineInfo.subpass = 0;
	pipelineInfo.basePipelineHandle = VK_NULL_HANDLE;
	pipelineInfo.pDepthStencilState = &_depthStencil;

	//it's easy to error out on create graphics pipeline, so we handle it a bit better than the common VK_CHECK case
	VkPipeline newPipeline;
	if (vkCreateGraphicsPipelines(
		device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &newPipeline) != VK_SUCCESS) {
		std::cout << "failed to create pipeline\n";
		return VK_NULL_HANDLE; // failed to create graphics pipeline
	}
	else
	{
		return newPipeline;
	}

}



void VulkanEngine::load_meshes()
{
	_triangleMesh._vertices.resize(3);

	_triangleMesh._vertices[0].position = { 1.f,1.f, 0.5f };
	_triangleMesh._vertices[1].position = { -1.f,1.f, 0.5f };
	_triangleMesh._vertices[2].position = { 0.f,-1.f, 0.5f };

	_triangleMesh._vertices[0].color = { 1.f,0.f, 0.0f }; //pure green
	_triangleMesh._vertices[1].color = { 0.f,1.f, 0.0f }; //pure green
	_triangleMesh._vertices[2].color = { 0.f,0.f, 1.0f }; //pure green

	//load the monkey
	_monkeyMesh.load_from_obj("../../../../assets/lightTest.obj");
	//_monkeyMesh.load_from_obj("../../../../assets/uploads_files.obj");
	character.load_from_file("../../../../assets/Catwalk Walk.dae");
	//character.load_from_file("../../../../assets/suit.fbx");
	//make sure both meshes are sent to the GPU
	upload_animmesh(character);
	upload_mesh(_triangleMesh);
	upload_mesh(_monkeyMesh);

	//note that we are copying them. Eventually we will delete the hF"hand"ardcoded _monkey and _triangle meshes, so it's no problem now.
	_meshes["monkey"] = _monkeyMesh;
	_meshes["triangle"] = _triangleMesh;
	

}
void VulkanEngine::upload_animmesh(AnimatedMesh& mesh)
{
	const size_t bufferSize = mesh._vertices.size() * sizeof(AnimatedVertex);
	//allocate staging buffer
	VkBufferCreateInfo stagingBufferInfo = {};
	stagingBufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	stagingBufferInfo.pNext = nullptr;

	stagingBufferInfo.size = bufferSize;
	stagingBufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;

	//let the VMA library know that this data should be on CPU RAM
	VmaAllocationCreateInfo vmaallocInfo = {};
	vmaallocInfo.usage = VMA_MEMORY_USAGE_CPU_ONLY;

	AllocatedBuffer stagingBuffer;

	//allocate the buffer
	VK_CHECK(vmaCreateBuffer(_allocator, &stagingBufferInfo, &vmaallocInfo,
		&stagingBuffer._buffer,
		&stagingBuffer._allocation,
		nullptr));
	//copy vertex data
	void* data;
	vmaMapMemory(_allocator, stagingBuffer._allocation, &data);

	memcpy(data, mesh._vertices.data(), mesh._vertices.size() * sizeof(Vertex));

	vmaUnmapMemory(_allocator, stagingBuffer._allocation);

	//allocate vertex buffer
	VkBufferCreateInfo vertexBufferInfo = {};
	vertexBufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	vertexBufferInfo.pNext = nullptr;
	//this is the total size, in bytes, of the buffer we are allocating
	vertexBufferInfo.size = bufferSize;
	//this buffer is going to be used as a Vertex Buffer
	vertexBufferInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;

	//let the VMA library know that this data should be GPU native
	vmaallocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

	//allocate the buffer
	VK_CHECK(vmaCreateBuffer(_allocator, &vertexBufferInfo, &vmaallocInfo,
		&mesh._vertexBuffer._buffer,
		&mesh._vertexBuffer._allocation,
		nullptr));

	immediate_submit([=](VkCommandBuffer cmd) {
		VkBufferCopy copy;
		copy.dstOffset = 0;
		copy.srcOffset = 0;
		copy.size = bufferSize;
		vkCmdCopyBuffer(cmd, stagingBuffer._buffer, mesh._vertexBuffer._buffer, 1, &copy);
		});

	//add the destruction of mesh buffer to the deletion queue
	_mainDeletionQueue.push_function([=]() {
		vmaDestroyBuffer(_allocator, mesh._vertexBuffer._buffer, mesh._vertexBuffer._allocation);
		});

	vmaDestroyBuffer(_allocator, stagingBuffer._buffer, stagingBuffer._allocation);


	 //----------------//
	//----------------//
	const size_t bufferSize2 = mesh._indices.size() * sizeof(Index);
	//allocate staging buffer
	stagingBufferInfo = {};
	stagingBufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	stagingBufferInfo.pNext = nullptr;

	stagingBufferInfo.size = bufferSize2;
	stagingBufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;

	//let the VMA library know that this data should be on CPU RAM
	vmaallocInfo = {};
	vmaallocInfo.usage = VMA_MEMORY_USAGE_CPU_ONLY;

	AllocatedBuffer stagingBuffer2;

	//allocate the buffer
	VK_CHECK(vmaCreateBuffer(_allocator, &stagingBufferInfo, &vmaallocInfo,
		&stagingBuffer2._buffer,
		&stagingBuffer2._allocation,
		nullptr));
	//copy indices data
	void* data2;
	vmaMapMemory(_allocator, stagingBuffer2._allocation, &data2);

	memcpy(data2, mesh._indices.data(), mesh._indices.size() * sizeof(Index));

	vmaUnmapMemory(_allocator, stagingBuffer2._allocation);

	//allocate vertex buffer
	vertexBufferInfo = {};
	vertexBufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	vertexBufferInfo.pNext = nullptr;
	//this is the total size, in bytes, of the buffer we are allocating
	vertexBufferInfo.size = bufferSize2;
	//this buffer is going to be used as a Vertex Buffer
	vertexBufferInfo.usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;

	//let the VMA library know that this data should be GPU native
	vmaallocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

	//allocate the buffer
	VK_CHECK(vmaCreateBuffer(_allocator, &vertexBufferInfo, &vmaallocInfo,
		&mesh._indicesBuffer._buffer,
		&mesh._indicesBuffer._allocation,
		nullptr));

	immediate_submit([=](VkCommandBuffer cmd) {
		VkBufferCopy copy;
		copy.dstOffset = 0;
		copy.srcOffset = 0;
		copy.size = bufferSize2;
		vkCmdCopyBuffer(cmd, stagingBuffer2._buffer, mesh._indicesBuffer._buffer, 1, &copy);
		});

	//add the destruction of mesh buffer to the deletion queue
	_mainDeletionQueue.push_function([=]() {
		vmaDestroyBuffer(_allocator, mesh._indicesBuffer._buffer, mesh._indicesBuffer._allocation);
		});

	vmaDestroyBuffer(_allocator, stagingBuffer2._buffer, stagingBuffer2._allocation);


}


void VulkanEngine::upload_mesh(Mesh& mesh)
{
	const size_t bufferSize = mesh._vertices.size() * sizeof(Vertex);
	//allocate staging buffer
	VkBufferCreateInfo stagingBufferInfo = {};
	stagingBufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	stagingBufferInfo.pNext = nullptr;

	stagingBufferInfo.size = bufferSize;
	stagingBufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;

	//let the VMA library know that this data should be on CPU RAM
	VmaAllocationCreateInfo vmaallocInfo = {};
	vmaallocInfo.usage = VMA_MEMORY_USAGE_CPU_ONLY;

	AllocatedBuffer stagingBuffer;

	//allocate the buffer
	VK_CHECK(vmaCreateBuffer(_allocator, &stagingBufferInfo, &vmaallocInfo,
		&stagingBuffer._buffer,
		&stagingBuffer._allocation,
		nullptr));
	//copy vertex data
	void* data;
	vmaMapMemory(_allocator, stagingBuffer._allocation, &data);

	memcpy(data, mesh._vertices.data(), mesh._vertices.size() * sizeof(Vertex));

	vmaUnmapMemory(_allocator, stagingBuffer._allocation);

	//allocate vertex buffer
	VkBufferCreateInfo vertexBufferInfo = {};
	vertexBufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	vertexBufferInfo.pNext = nullptr;
	//this is the total size, in bytes, of the buffer we are allocating
	vertexBufferInfo.size = bufferSize;
	//this buffer is going to be used as a Vertex Buffer
	vertexBufferInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;

	//let the VMA library know that this data should be GPU native
	vmaallocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

	//allocate the buffer
	VK_CHECK(vmaCreateBuffer(_allocator, &vertexBufferInfo, &vmaallocInfo,
		&mesh._vertexBuffer._buffer,
		&mesh._vertexBuffer._allocation,
		nullptr));

	immediate_submit([=](VkCommandBuffer cmd) {
		VkBufferCopy copy;
		copy.dstOffset = 0;
		copy.srcOffset = 0;
		copy.size = bufferSize;
		vkCmdCopyBuffer(cmd, stagingBuffer._buffer, mesh._vertexBuffer._buffer, 1, &copy);
		});

	//add the destruction of mesh buffer to the deletion queue
	_mainDeletionQueue.push_function([=]() {
		vmaDestroyBuffer(_allocator, mesh._vertexBuffer._buffer, mesh._vertexBuffer._allocation);
		});

	vmaDestroyBuffer(_allocator, stagingBuffer._buffer, stagingBuffer._allocation);
}

void VulkanEngine::load_images()
{
	Texture lostEmpire;
	Texture charTex;

	vkutil::load_image_from_file(*this, "../../../../assets/Catwalk Walk.fbm/Ch03_1001_Diffuse.png", charTex.image);
	//vkutil::load_image_from_file(*this, "../../../../assets/diffuse.png", charTex.image);
	vkutil::load_image_from_asset(*this, "../../../../assets/map.tx", lostEmpire.image);

	VkImageViewCreateInfo imageinfo = vkinit::imageview_create_info(VK_FORMAT_R8G8B8A8_SRGB, lostEmpire.image._image, VK_IMAGE_ASPECT_COLOR_BIT);
	vkCreateImageView(_device, &imageinfo, nullptr, &lostEmpire.imageView);

	imageinfo = vkinit::imageview_create_info(VK_FORMAT_R8G8B8A8_SRGB, charTex.image._image, VK_IMAGE_ASPECT_COLOR_BIT);
	vkCreateImageView(_device, &imageinfo, nullptr, &charTex.imageView);

	_loadedTextures["empire_diffuse"] = lostEmpire;
	_loadedTextures["charTex"] = charTex;
}

void VulkanEngine::init()
{

	// We initialize SDL and create a window with it. 
	SDL_Init(SDL_INIT_VIDEO);
	SDL_SetRelativeMouseMode(SDL_TRUE);
	SDL_WindowFlags window_flags = (SDL_WindowFlags)(SDL_WINDOW_VULKAN);
	SDL_ShowCursor(SDL_ENABLE);
	//create blank SDL window for our application
	_window = SDL_CreateWindow(
		"Vulkan Engine", //window title
		SDL_WINDOWPOS_UNDEFINED, //window position x (don't care)
		SDL_WINDOWPOS_UNDEFINED, //window position y (don't care)
		_windowExtent.width,  //window width in pixels
		_windowExtent.height, //window height in pixels
		window_flags
	);

	//load the core Vulkan structures
	init_vulkan();
	//create the swapchain
	init_swapchain();

	init_commands();

	init_default_renderpass();

	init_framebuffers();
	init_sync_structures();
	init_pipelines();
	load_meshes();
	init_scene();

	//everything went fine
	_isInitialized = true;
}

inline XrReferenceSpaceCreateInfo GetXrReferenceSpaceCreateInfo(const std::string& referenceSpaceTypeStr) {
	XrReferenceSpaceCreateInfo referenceSpaceCreateInfo{ XR_TYPE_REFERENCE_SPACE_CREATE_INFO };
	referenceSpaceCreateInfo.poseInReferenceSpace = Math::Pose::Identity();
	if (EqualsIgnoreCase(referenceSpaceTypeStr, "View")) {
		referenceSpaceCreateInfo.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_VIEW;
	}
	else if (EqualsIgnoreCase(referenceSpaceTypeStr, "ViewFront")) {
		// Render head-locked 2m in front of device.
		referenceSpaceCreateInfo.poseInReferenceSpace = Math::Pose::Translation({ 0.f, 0.f, -2.f }),
			referenceSpaceCreateInfo.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_VIEW;
	}
	else if (EqualsIgnoreCase(referenceSpaceTypeStr, "Local")) {
		referenceSpaceCreateInfo.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;
	}
	else if (EqualsIgnoreCase(referenceSpaceTypeStr, "Stage")) {
		referenceSpaceCreateInfo.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_STAGE;
	}
	else if (EqualsIgnoreCase(referenceSpaceTypeStr, "StageLeft")) {
		referenceSpaceCreateInfo.poseInReferenceSpace = Math::Pose::RotateCCWAboutYAxis(0.f, { -2.f, 0.f, -2.f });
		referenceSpaceCreateInfo.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_STAGE;
	}
	else if (EqualsIgnoreCase(referenceSpaceTypeStr, "StageRight")) {
		referenceSpaceCreateInfo.poseInReferenceSpace = Math::Pose::RotateCCWAboutYAxis(0.f, { 2.f, 0.f, -2.f });
		referenceSpaceCreateInfo.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_STAGE;
	}
	else if (EqualsIgnoreCase(referenceSpaceTypeStr, "StageLeftRotated")) {
		referenceSpaceCreateInfo.poseInReferenceSpace = Math::Pose::RotateCCWAboutYAxis(3.14f / 3.f, { -2.f, 0.5f, -2.f });
		referenceSpaceCreateInfo.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_STAGE;
	}
	else if (EqualsIgnoreCase(referenceSpaceTypeStr, "StageRightRotated")) {
		referenceSpaceCreateInfo.poseInReferenceSpace = Math::Pose::RotateCCWAboutYAxis(-3.14f / 3.f, { 2.f, 0.5f, -2.f });
		referenceSpaceCreateInfo.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_STAGE;
	}
	else {
		throw std::invalid_argument(Fmt("Unknown reference space type '%s'", referenceSpaceTypeStr.c_str()));
	}
	return referenceSpaceCreateInfo;
}


void VulkanEngine::openXR_init()
{
	uint32_t layerCount;
	CHECK_XRCMD(xrEnumerateApiLayerProperties(0, &layerCount, nullptr));

	std::vector<XrApiLayerProperties> layers(layerCount);
	for (XrApiLayerProperties& layer : layers) {
		layer.type = XR_TYPE_API_LAYER_PROPERTIES;
	}

	CHECK_XRCMD(xrEnumerateApiLayerProperties((uint32_t)layers.size(), &layerCount, layers.data()));

	spdlog::info("Available Layers: {}", layerCount);
	for (const XrApiLayerProperties& layer : layers) {
		spdlog::info("  Name={} SpecVersion={} LayerVersion={} Description={}", layer.layerName,
			GetXrVersionString(layer.specVersion).c_str(), layer.layerVersion, layer.description);

	}

	spdlog::set_level(spdlog::level::debug);
	// Create union of extensions required by platform and graphics plugins.
	std::vector<const char*> extensions;
	std::vector<const char*> valLayers;


	const std::vector<std::string> graphicsExtensions = { XR_KHR_VULKAN_ENABLE2_EXTENSION_NAME };
	const std::vector<std::string> xrLayers = {"XR_APILAYER_LUNARG_core_validation"};

	std::transform(graphicsExtensions.begin(), graphicsExtensions.end(), std::back_inserter(extensions),
		[](const std::string& ext) { return ext.c_str(); });

	std::transform(xrLayers.begin(), xrLayers.end(), std::back_inserter(valLayers),
		[](const std::string& extt) { return extt.c_str(); });

	XrInstanceCreateInfo createInfo{ XR_TYPE_INSTANCE_CREATE_INFO };
	createInfo.next = nullptr;
	createInfo.enabledExtensionCount = (uint32_t)extensions.size();
	createInfo.enabledExtensionNames = extensions.data();
	//createInfo.enabledApiLayerCount = (uint32_t)valLayers.size();
	//createInfo.enabledApiLayerNames = valLayers.data();

	strcpy(createInfo.applicationInfo.applicationName, "HelloXRWithVulkan");
	createInfo.applicationInfo.apiVersion = XR_CURRENT_API_VERSION;

	CHECK_XRCMD(xrCreateInstance(&createInfo, &_xrInstance));

	CHECK(_xrInstance != XR_NULL_HANDLE);

	XrInstanceProperties instanceProperties{ XR_TYPE_INSTANCE_PROPERTIES };
	CHECK_XRCMD(xrGetInstanceProperties(_xrInstance, &instanceProperties));



	spdlog::info("Instance RuntimeName={0} RuntimeVersion={1}", instanceProperties.runtimeName,
		GetXrVersionString(instanceProperties.runtimeVersion).c_str());


	CHECK(_xrInstance != XR_NULL_HANDLE);


	XrSystemGetInfo systemInfo{ XR_TYPE_SYSTEM_GET_INFO };
	systemInfo.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
	CHECK_XRCMD(xrGetSystem(_xrInstance, &systemInfo, &_xrSystemId));

	PFN_xrGetVulkanGraphicsRequirementsKHR pfn_get_vulkan_graphics_requirements_khr;
	CHECK_XRCMD(xrGetInstanceProcAddr(_xrInstance, "xrGetVulkanGraphicsRequirements2KHR",
		reinterpret_cast<PFN_xrVoidFunction*>(&pfn_get_vulkan_graphics_requirements_khr)));
	XrGraphicsRequirementsVulkanKHR req{ XR_TYPE_GRAPHICS_REQUIREMENTS_VULKAN_KHR };
	CHECK_XRCMD(pfn_get_vulkan_graphics_requirements_khr(_xrInstance, _xrSystemId, &req));

	spdlog::debug("Using system {0} for form factor {1}", _xrSystemId, to_string(XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY));
	CHECK(_xrInstance != XR_NULL_HANDLE);
	CHECK(_xrSystemId != XR_NULL_SYSTEM_ID);


	// We initialize SDL and create a window with it. 
	SDL_Init(SDL_INIT_VIDEO);
	//SDL_SetRelativeMouseMode(SDL_TRUE);
	SDL_ShowCursor(SDL_TRUE);
	SDL_WindowFlags window_flags = (SDL_WindowFlags)(SDL_WINDOW_VULKAN);





	init_xrVulkan();
	_xrGraphicsBinding.instance = _instance;
	_xrGraphicsBinding.physicalDevice = _chosenGPU;
	_xrGraphicsBinding.device = _device;
	_xrGraphicsBinding.queueFamilyIndex = _graphicsQueueFamily;
	_xrGraphicsBinding.queueIndex = 0;
	//spdlog::set_level(spdlog::level::off);
	spdlog::debug("Creating Session");
	_xrSession = XR_NULL_HANDLE;
	CHECK(_xrInstance != XR_NULL_HANDLE);
	CHECK(_xrSession == XR_NULL_HANDLE);

	XrSessionCreateInfo sessionCreateInfo{ XR_TYPE_SESSION_CREATE_INFO };
	sessionCreateInfo.next = reinterpret_cast<const XrBaseInStructure*>(&_xrGraphicsBinding);
	sessionCreateInfo.systemId = _xrSystemId;
	CHECK_XRCMD(xrCreateSession(_xrInstance, &sessionCreateInfo, &_xrSession));
	//CHECK_XRCMD(xrCreateSession(nullptr, &sessionCreateInfo, &_xrSession));
	std::string visualizedSpaces[] = { "ViewFront",        "Local", "Stage", "StageLeft", "StageRight", "StageLeftRotated",
								  "StageRightRotated" };

	for (const auto& visualizedSpace : visualizedSpaces) {
		XrReferenceSpaceCreateInfo referenceSpaceCreateInfo = GetXrReferenceSpaceCreateInfo(visualizedSpace);
		XrSpace space;
		XrResult res = xrCreateReferenceSpace(_xrSession, &referenceSpaceCreateInfo, &space);
		if (XR_SUCCEEDED(res)) {
			_xrVisualizedSpaces.push_back(space);
		}
		else {
			spdlog::warn("Failed to create reference space {} with error {}", visualizedSpace.c_str(), res);
		}
	}
	XrReferenceSpaceCreateInfo referenceSpaceCreateInfo = GetXrReferenceSpaceCreateInfo("Local");
	CHECK_XRCMD(xrCreateReferenceSpace(_xrSession, &referenceSpaceCreateInfo, &_xrSpace));
	
	xrInitialiseActions();
	init_xrSwapchain();
	
	//create blank SDL window for our application
	_window = SDL_CreateWindow(
		"Vulkan Engine", //window title
		SDL_WINDOWPOS_UNDEFINED, //window position x (don't care)
		SDL_WINDOWPOS_UNDEFINED, //window position y (don't care)
		_xrRenderTargets[0].width,// / 4,  //window width in pixels
		_xrRenderTargets[0].height,// / 4, //window height in pixels
		window_flags
	);
	// get the surface of the window we opened with SDL
	SDL_Vulkan_CreateSurface(_window, _instance, &_surface);
	init_mirSwapchain();
	init_miscImages();
	init_commands();
	init_miscCommands();
	
	init_default_xrRenderpass();
	init_shadow_Renderpass();
	

	init_xrFramebuffers();
	init_miscFramebuffers();
	init_sync_structures();
	init_descriptors();
	init_xrPipelines();
	init_miscPipelines();
	//init_imgui();
	load_images();
	load_meshes();
	init_scene();
	while ((_xrSessionState == XR_SESSION_STATE_IDLE) || (_xrSessionState == XR_SESSION_STATE_UNKNOWN)) {
		xrPollEvents();
	}

	_isInitialized = true;


}



void VulkanEngine::init_scene()
{
	mainScene = Scene(0);

	//create a sampler for the texture
	VkSamplerCreateInfo samplerInfo = vkinit::sampler_create_info(VK_FILTER_LINEAR);

	VkSampler blockySampler;
	vkCreateSampler(_device, &samplerInfo, nullptr, &blockySampler);
	Material* texturedMat = get_material("texturedmesh");
	Material* charTexMat = get_material("animMesh");


	//write to the descriptor set so that it points to our empire_diffuse texture
	VkDescriptorImageInfo imageBufferInfo;
	imageBufferInfo.sampler = blockySampler;
	imageBufferInfo.imageView = _loadedTextures["empire_diffuse"].imageView;
	imageBufferInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

	//write to the descriptor set so that it points to our empire_diffuse texture
	VkDescriptorImageInfo charTeximageBufferInfo;
	charTeximageBufferInfo.sampler = blockySampler;
	charTeximageBufferInfo.imageView = _loadedTextures["charTex"].imageView;
	charTeximageBufferInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

	VkDescriptorImageInfo shadowBufferInfo;
	shadowBufferInfo.sampler = blockySampler;
	shadowBufferInfo.imageView = shadowImage.imageView;
	shadowBufferInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;


	vkutil::DescriptorBuilder::begin(_descriptorLayoutCache, _descriptorAllocator)
		.bind_image(0, &imageBufferInfo, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT)
		.build(texturedMat->textureSet, _singleTextureSetLayout);
	vkutil::DescriptorBuilder::begin(_descriptorLayoutCache, _descriptorAllocator)
		.bind_image(0, &charTeximageBufferInfo, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT)
		.build(charTexMat->textureSet, _singleTextureSetLayout);
	vkutil::DescriptorBuilder::begin(_descriptorLayoutCache, _descriptorAllocator)
		.bind_image(0, &shadowBufferInfo, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT)
		.build(shadowSet, shadowTextureSetLayout);

	
	Entity* monkey = mainScene.add_entity("monkey");
	monkey->add_component<RenderObject>(get_mesh("monkey"), get_material("texturedmesh"));
	monkey->add_component <transformation>();


	Entity* triLight = monkey->add_child_entity("Tri");
	triLight->add_component<RenderObject>(get_mesh("triangle"), get_material("defaultmesh"));
	triLight->add_component<transformation>();
	triLight->get_component<transformation>().position = glm::vec3(0.0f, 10.0f, 0.0f);
	triLight->get_component<transformation>().scale = glm::vec3(0.2f, 0.2f, 0.2f);

	Entity* charEnt = mainScene.add_entity("character");
	charEnt->add_component<RenderObject>(nullptr, get_material("animMesh"), &character, RENDER_TYPE_ANIM);
	charEnt->add_component<transformation>();


	for (int x = -5; x <= 5; x++) {
		for (int y = -5; y <= 5; y++) {

			Entity* tri = monkey->add_child_entity("Tri");
			tri->add_component<RenderObject>(get_mesh("triangle"), get_material("defaultmesh"));
			tri->add_component<transformation>();
			tri->get_component<transformation>().position = glm::vec3(x, 1.0f, y);
			tri->get_component<transformation>().scale = glm::vec3(0.2f, 0.2f, 0.2f);
		}
	}
	
	Entity* hand = mainScene.add_entity("hand");
	hand->add_component<RenderObject>(get_mesh("triangle"), get_material("defaultmesh"));
	hand->add_component<transformation>();
	hand->get_component<transformation>().scale = glm::vec3(0.2f, 0.2f, 0.2f);
	
}

XrResult GetVulkanGraphicsRequirements2KHR(XrInstance instance, XrSystemId systemId,
	XrGraphicsRequirementsVulkan2KHR* graphicsRequirements) {
	PFN_xrGetVulkanGraphicsRequirements2KHR pfnGetVulkanGraphicsRequirements2KHR = nullptr;
	CHECK_XRCMD(xrGetInstanceProcAddr(instance, "xrGetVulkanGraphicsRequirements2KHR",
		reinterpret_cast<PFN_xrVoidFunction*>(&pfnGetVulkanGraphicsRequirements2KHR)));

	return pfnGetVulkanGraphicsRequirements2KHR(instance, systemId, graphicsRequirements);
}

XrResult CreateVulkanInstanceKHR(XrInstance instance, const XrVulkanInstanceCreateInfoKHR* createInfo,
	VkInstance* vulkanInstance, VkResult* vulkanResult) {
	PFN_xrCreateVulkanInstanceKHR pfnCreateVulkanInstanceKHR = nullptr;
	CHECK_XRCMD(xrGetInstanceProcAddr(instance, "xrCreateVulkanInstanceKHR",
		reinterpret_cast<PFN_xrVoidFunction*>(&pfnCreateVulkanInstanceKHR)));

	return pfnCreateVulkanInstanceKHR(instance, createInfo, vulkanInstance, vulkanResult);
}


VkBool32 debugReport(VkDebugReportFlagsEXT flags, VkDebugReportObjectTypeEXT objectType, uint64_t object, size_t /*location*/,
	int32_t /*messageCode*/, const char* pLayerPrefix, const char* pMessage) {
	std::string flagNames;
	std::string objName;
	int level = spdlog::level::err;

	if ((flags & VK_DEBUG_REPORT_DEBUG_BIT_EXT) != 0u) {
		flagNames += "DEBUG:";
		level = spdlog::level::trace;
	}
	if ((flags & VK_DEBUG_REPORT_INFORMATION_BIT_EXT) != 0u) {
		flagNames += "INFO:";
		level = spdlog::level::info;
	}
	if ((flags & VK_DEBUG_REPORT_PERFORMANCE_WARNING_BIT_EXT) != 0u) {
		flagNames += "PERF:";
		level = spdlog::level::warn;
	}
	if ((flags & VK_DEBUG_REPORT_WARNING_BIT_EXT) != 0u) {
		flagNames += "WARN:";
		level = spdlog::level::warn;
	}
	if ((flags & VK_DEBUG_REPORT_ERROR_BIT_EXT) != 0u) {
		flagNames += "ERROR:";
		level = level = spdlog::level::err;
	}

#define LIST_OBJECT_TYPES(_) \
    _(UNKNOWN)               \
    _(INSTANCE)              \
    _(PHYSICAL_DEVICE)       \
    _(DEVICE)                \
    _(QUEUE)                 \
    _(SEMAPHORE)             \
    _(COMMAND_BUFFER)        \
    _(FENCE)                 \
    _(DEVICE_MEMORY)         \
    _(BUFFER)                \
    _(IMAGE)                 \
    _(EVENT)                 \
    _(QUERY_POOL)            \
    _(BUFFER_VIEW)           \
    _(IMAGE_VIEW)            \
    _(SHADER_MODULE)         \
    _(PIPELINE_CACHE)        \
    _(PIPELINE_LAYOUT)       \
    _(RENDER_PASS)           \
    _(PIPELINE)              \
    _(DESCRIPTOR_SET_LAYOUT) \
    _(SAMPLER)               \
    _(DESCRIPTOR_POOL)       \
    _(DESCRIPTOR_SET)        \
    _(FRAMEBUFFER)           \
    _(COMMAND_POOL)          \
    _(SURFACE_KHR)           \
    _(SWAPCHAIN_KHR)         \
    _(DISPLAY_KHR)           \
    _(DISPLAY_MODE_KHR)

	switch (objectType) {
	default:
#define MK_OBJECT_TYPE_CASE(name)                  \
    case VK_DEBUG_REPORT_OBJECT_TYPE_##name##_EXT: \
        objName = #name;                           \
        break;
		LIST_OBJECT_TYPES(MK_OBJECT_TYPE_CASE)

#if VK_HEADER_VERSION >= 46
			MK_OBJECT_TYPE_CASE(DESCRIPTOR_UPDATE_TEMPLATE_KHR)
#endif
#if VK_HEADER_VERSION >= 70
			MK_OBJECT_TYPE_CASE(DEBUG_REPORT_CALLBACK_EXT)
#endif
	}

	if ((objectType == VK_DEBUG_REPORT_OBJECT_TYPE_INSTANCE_EXT) && (strcmp(pLayerPrefix, "Loader Message") == 0) &&
		(strncmp(pMessage, "Device Extension:", 17) == 0)) {
		return VK_FALSE;
	}

	spdlog::log((spdlog::level::level_enum)level, Fmt("%s (%s 0x%llx) [%s] %s", flagNames.c_str(), objName.c_str(), object, pLayerPrefix, pMessage));
	if ((flags & VK_DEBUG_REPORT_ERROR_BIT_EXT) != 0u) {
		return VK_FALSE;
	}
	if ((flags & VK_DEBUG_REPORT_WARNING_BIT_EXT) != 0u) {
		return VK_FALSE;
	}
	return VK_FALSE;
}

static VKAPI_ATTR VkBool32 VKAPI_CALL debugReportThunk(VkDebugReportFlagsEXT flags, VkDebugReportObjectTypeEXT objectType,
	uint64_t object, size_t location, int32_t messageCode,
	const char* pLayerPrefix, const char* pMessage, void* pUserData) {
	return debugReport(flags, objectType, object, location, messageCode,
		pLayerPrefix, pMessage);
}

XrResult GetVulkanGraphicsDevice2KHR(XrInstance instance, const XrVulkanGraphicsDeviceGetInfoKHR* getInfo,
	VkPhysicalDevice* vulkanPhysicalDevice) {
	PFN_xrGetVulkanGraphicsDevice2KHR pfnGetVulkanGraphicsDevice2KHR = nullptr;
	CHECK_XRCMD(xrGetInstanceProcAddr(instance, "xrGetVulkanGraphicsDevice2KHR",
		reinterpret_cast<PFN_xrVoidFunction*>(&pfnGetVulkanGraphicsDevice2KHR)));

	return pfnGetVulkanGraphicsDevice2KHR(instance, getInfo, vulkanPhysicalDevice);
}

XrResult CreateVulkanDeviceKHR(XrInstance instance, const XrVulkanDeviceCreateInfoKHR* createInfo,
	VkDevice* vulkanDevice, VkResult* vulkanResult) {
	PFN_xrCreateVulkanDeviceKHR pfnCreateVulkanDeviceKHR = nullptr;
	CHECK_XRCMD(xrGetInstanceProcAddr(instance, "xrCreateVulkanDeviceKHR",
		reinterpret_cast<PFN_xrVoidFunction*>(&pfnCreateVulkanDeviceKHR)));

	return pfnCreateVulkanDeviceKHR(instance, createInfo, vulkanDevice, vulkanResult);
}

bool checkValidationLayerSupport(std::vector<const char*> validationLayers) {
	uint32_t layerCount;
	vkEnumerateInstanceLayerProperties(&layerCount, nullptr);

	std::vector<VkLayerProperties> availableLayers(layerCount);
	vkEnumerateInstanceLayerProperties(&layerCount, availableLayers.data());

	for (const char* layerName : validationLayers) {
		bool layerFound = false;

		for (const auto& layerProperties : availableLayers) {
			if (strcmp(layerName, layerProperties.layerName) == 0) {
				layerFound = true;
				break;
			}
		}

		if (!layerFound) {
			return false;
		}
	}

	return true;
}

bool checkDeviceExtensionSupport(VkPhysicalDevice device, std::vector<const char*> deviceExtensions) {
	uint32_t extensionCount;
	vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, nullptr);

	std::vector<VkExtensionProperties> availableExtensions(extensionCount);
	vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, availableExtensions.data());

	std::set<std::string> requiredExtensions(deviceExtensions.begin(), deviceExtensions.end());

	for (const auto& extension : availableExtensions) {
		requiredExtensions.erase(extension.extensionName);
	}

	return requiredExtensions.empty();
}


void VulkanEngine::init_xrVulkan()
{


	PFN_vkCreateDebugReportCallbackEXT vkCreateDebugReportCallbackEXT{ nullptr };
	PFN_vkDestroyDebugReportCallbackEXT vkDestroyDebugReportCallbackEXT{ nullptr };
	VkDebugReportCallbackEXT m_vkDebugReporter{ VK_NULL_HANDLE };

	// Create the Vulkan device for the adapter associated with the system.
		// Extension function must be loaded by name
	XrGraphicsRequirementsVulkan2KHR graphicsRequirements{ XR_TYPE_GRAPHICS_REQUIREMENTS_VULKAN2_KHR };
	CHECK_XRCMD(GetVulkanGraphicsRequirements2KHR(_xrInstance, _xrSystemId, &graphicsRequirements));




	std::vector<const char*> layers;
	layers.push_back("VK_LAYER_KHRONOS_validation");
	std::vector<const char*> extensions;
	extensions.push_back("VK_EXT_debug_report");

	//extensions.push_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
	//if (!checkDeviceExtensionSupport(_chosenGPU, extensions)) {
	//	throw std::runtime_error("extention unavailable");
	//}
	if (!checkValidationLayerSupport(layers)) {
		throw std::runtime_error("validation layers requested, but not available!");
	}

	VkApplicationInfo appInfo{ VK_STRUCTURE_TYPE_APPLICATION_INFO };
	appInfo.pApplicationName = "Rahul Vulkan XR";
	appInfo.applicationVersion = 1;
	appInfo.pEngineName = "rahgunEngine";
	appInfo.engineVersion = 1;
	appInfo.apiVersion = VK_API_VERSION_1_1;

	VkInstanceCreateInfo instInfo{ VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO };
	instInfo.pApplicationInfo = &appInfo;
	instInfo.enabledLayerCount = (uint32_t)layers.size();
	instInfo.ppEnabledLayerNames = layers.empty() ? nullptr : layers.data();
	instInfo.enabledExtensionCount = (uint32_t)extensions.size();
	instInfo.ppEnabledExtensionNames = extensions.empty() ? nullptr : extensions.data();

	XrVulkanInstanceCreateInfoKHR createInfo{ XR_TYPE_VULKAN_INSTANCE_CREATE_INFO_KHR };
	createInfo.systemId = _xrSystemId;
	createInfo.pfnGetInstanceProcAddr = &vkGetInstanceProcAddr;
	createInfo.vulkanCreateInfo = &instInfo;
	createInfo.vulkanAllocator = nullptr;
	VkResult err = VK_SUCCESS;
	CHECK_XRCMD(CreateVulkanInstanceKHR(_xrInstance, &createInfo, &_instance, &err));
	//CHECKVK(err);



	vkCreateDebugReportCallbackEXT =
		(PFN_vkCreateDebugReportCallbackEXT)vkGetInstanceProcAddr(_instance, "vkCreateDebugReportCallbackEXT");
	vkDestroyDebugReportCallbackEXT =
		(PFN_vkDestroyDebugReportCallbackEXT)vkGetInstanceProcAddr(_instance, "vkDestroyDebugReportCallbackEXT");
	VkDebugReportCallbackCreateInfoEXT debugInfo{ VK_STRUCTURE_TYPE_DEBUG_REPORT_CALLBACK_CREATE_INFO_EXT };
	debugInfo.flags = VK_DEBUG_REPORT_ERROR_BIT_EXT | VK_DEBUG_REPORT_WARNING_BIT_EXT;
	debugInfo.flags |=
		VK_DEBUG_REPORT_INFORMATION_BIT_EXT | VK_DEBUG_REPORT_PERFORMANCE_WARNING_BIT_EXT | VK_DEBUG_REPORT_DEBUG_BIT_EXT;

	debugInfo.pfnCallback = debugReportThunk;
	debugInfo.pUserData = this;
	CHECKVK(vkCreateDebugReportCallbackEXT(_instance, &debugInfo, nullptr, &m_vkDebugReporter));

	XrVulkanGraphicsDeviceGetInfoKHR deviceGetInfo{ XR_TYPE_VULKAN_GRAPHICS_DEVICE_GET_INFO_KHR };
	deviceGetInfo.systemId = _xrSystemId;
	deviceGetInfo.vulkanInstance = _instance;
	CHECK_XRCMD(GetVulkanGraphicsDevice2KHR(_xrInstance, &deviceGetInfo, &_chosenGPU));

	vkGetPhysicalDeviceProperties(_chosenGPU, &_gpuProperties);
	std::cout << "The GPU has a minimum buffer alignment of " << _gpuProperties.limits.minUniformBufferOffsetAlignment << std::endl;

	VkDeviceQueueCreateInfo queueInfo{ VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO };
	float queuePriorities = 0;
	queueInfo.queueCount = 1;
	queueInfo.pQueuePriorities = &queuePriorities;

	uint32_t queueFamilyCount = 0;
	vkGetPhysicalDeviceQueueFamilyProperties(_chosenGPU, &queueFamilyCount, nullptr);
	std::vector<VkQueueFamilyProperties> queueFamilyProps(queueFamilyCount);
	vkGetPhysicalDeviceQueueFamilyProperties(_chosenGPU, &queueFamilyCount, &queueFamilyProps[0]);

	for (uint32_t i = 0; i < queueFamilyCount; ++i) {
		// Only need graphics (not presentation) for draw queue
		if ((queueFamilyProps[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0u) {
			_graphicsQueueFamily = queueInfo.queueFamilyIndex = i;
			break;
		}
	}

	std::vector<const char*> deviceExtensions;
	deviceExtensions.push_back(VK_KHR_MULTIVIEW_EXTENSION_NAME);
	VkPhysicalDeviceMultiviewFeaturesKHR physicalDeviceMultiviewFeatures{};
	physicalDeviceMultiviewFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MULTIVIEW_FEATURES_KHR;
	physicalDeviceMultiviewFeatures.multiview = VK_TRUE;
	physicalDeviceMultiviewFeatures.pNext = nullptr;

	VkPhysicalDeviceFeatures features{};
	// features.samplerAnisotropy = VK_TRUE;

	VkPhysicalDeviceShaderDrawParametersFeatures shader_draw_parameters_features = {};
	shader_draw_parameters_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_DRAW_PARAMETERS_FEATURES;
	shader_draw_parameters_features.pNext = &physicalDeviceMultiviewFeatures;
	shader_draw_parameters_features.shaderDrawParameters = VK_TRUE;


	VkDeviceCreateInfo deviceInfo{ VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO };
	deviceInfo.queueCreateInfoCount = 1;
	deviceInfo.pQueueCreateInfos = &queueInfo;
	deviceInfo.enabledLayerCount = 0;
	deviceInfo.ppEnabledLayerNames = nullptr;
	deviceInfo.enabledExtensionCount = (uint32_t)deviceExtensions.size();
	deviceInfo.ppEnabledExtensionNames = deviceExtensions.empty() ? nullptr : deviceExtensions.data();
	deviceInfo.pEnabledFeatures = &features;
	deviceInfo.pNext = &shader_draw_parameters_features;


	XrVulkanDeviceCreateInfoKHR deviceCreateInfo{ XR_TYPE_VULKAN_DEVICE_CREATE_INFO_KHR };
	deviceCreateInfo.systemId = _xrSystemId;
	deviceCreateInfo.pfnGetInstanceProcAddr = &vkGetInstanceProcAddr;
	deviceCreateInfo.vulkanCreateInfo = &deviceInfo;
	deviceCreateInfo.vulkanPhysicalDevice = _chosenGPU;
	deviceCreateInfo.vulkanAllocator = nullptr;
	CHECK_XRCMD(CreateVulkanDeviceKHR(_xrInstance, &deviceCreateInfo, &_device, &err));
	//CHECKVK(err);

	vkGetDeviceQueue(_device, queueInfo.queueFamilyIndex, 0, &_graphicsQueue);


	//initialize the memory allocator
	VmaAllocatorCreateInfo allocatorInfo = {};
	allocatorInfo.physicalDevice = _chosenGPU;
	allocatorInfo.device = _device;
	allocatorInfo.instance = _instance;
	vmaCreateAllocator(&allocatorInfo, &_allocator);

}

size_t VulkanEngine::pad_uniform_buffer_size(size_t originalSize)
{
	// Calculate required alignment based on minimum device offset alignment
	size_t minUboAlignment = _gpuProperties.limits.minUniformBufferOffsetAlignment;
	size_t alignedSize = originalSize;
	if (minUboAlignment > 0) {
		alignedSize = (alignedSize + minUboAlignment - 1) & ~(minUboAlignment - 1);
	}
	return alignedSize;
}


void VulkanEngine::init_vulkan()
{
	vkb::InstanceBuilder builder;

	//make the Vulkan instance, with basic debug features
	auto inst_ret = builder.set_app_name("Example Vulkan Application")
		.request_validation_layers(true)
		.require_api_version(1, 0, 0)
		.use_default_debug_messenger()
		.enable_extension("VK_KHR_external_semaphore_capabilities")
		.build();



	vkb::Instance vkb_inst = inst_ret.value();

	//store the instance
	_instance = vkb_inst.instance;
	//store the debug messenger
	_debug_messenger = vkb_inst.debug_messenger;



	// get the surface of the window we opened with SDL
	SDL_Vulkan_CreateSurface(_window, _instance, &_surface);


	VkPhysicalDeviceFeatures deviceFeatures{};
	deviceFeatures.shaderStorageImageMultisample = VK_TRUE;

	//use vkbootstrap to select a GPU.
	//We want a GPU that can write to the SDL surface and supports Vulkan 1.1
	vkb::PhysicalDeviceSelector selector{ vkb_inst };
	vkb::PhysicalDevice physicalDevice = selector
		.set_minimum_version(1, 0)
		.set_surface(_surface)
		.add_required_extension("VK_KHR_external_semaphore")
		.set_required_features(deviceFeatures)
		.select()
		.value();

	//create the final Vulkan device
	vkb::DeviceBuilder deviceBuilder{ physicalDevice };



	vkb::Device vkbDevice = deviceBuilder.build().value();

	// Get the VkDevice handle used in the rest of a Vulkan application
	_device = vkbDevice.device;
	_chosenGPU = physicalDevice.physical_device;

	// use vkbootstrap to get a Graphics queue
	_graphicsQueue = vkbDevice.get_queue(vkb::QueueType::graphics).value();
	_graphicsQueueFamily = vkbDevice.get_queue_index(vkb::QueueType::graphics).value();
	//initialize the memory allocator
	VmaAllocatorCreateInfo allocatorInfo = {};
	allocatorInfo.physicalDevice = _chosenGPU;
	allocatorInfo.device = _device;
	allocatorInfo.instance = _instance;
	vmaCreateAllocator(&allocatorInfo, &_allocator);

}

void VulkanEngine::init_miscImages() {

	//hardcoding the depth format to 32 bit float
	VkFormat _depthFormat = VK_FORMAT_D32_SFLOAT;

	//the depth image will be an image with the format we selected and Depth Attachment usage flag
	VkImageCreateInfo dimg_info = vkinit::image_create_info(_depthFormat, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT| VK_IMAGE_USAGE_SAMPLED_BIT, shadowMapResolution);

	//for the depth image, we want to allocate it from GPU local memory
	VmaAllocationCreateInfo dimg_allocinfo = {};
	dimg_allocinfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;
	dimg_allocinfo.requiredFlags = VkMemoryPropertyFlags(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

	//allocate and create the image
	vmaCreateImage(_allocator, &dimg_info, &dimg_allocinfo, &shadowImage.image, &shadowImage.allocation, nullptr);

	//build an image-view for the depth image to use for rendering
	VkImageViewCreateInfo dview_info = vkinit::imageview_create_info(_depthFormat, shadowImage.image, VK_IMAGE_ASPECT_DEPTH_BIT);

	VK_CHECK(vkCreateImageView(_device, &dview_info, nullptr, &shadowImage.imageView));

	//add to deletion queues
	_mainDeletionQueue.push_function([=]() {
		vkDestroyImageView(_device, shadowImage.imageView, nullptr);
		vmaDestroyImage(_allocator, shadowImage.image, shadowImage.allocation);
		});

}

void VulkanEngine::init_xrSwapchain()
{

	// Read graphics properties for preferred swapchain length and logging.
	XrSystemProperties systemProperties{ XR_TYPE_SYSTEM_PROPERTIES };
	CHECK_XRCMD(xrGetSystemProperties(_xrInstance, _xrSystemId, &systemProperties));

	// Log system properties.
	spdlog::info("System Properties: Name={} VendorId={}", systemProperties.systemName, systemProperties.vendorId);
	spdlog::info("System Graphics Properties: MaxWidth={} MaxHeight={} MaxLayers={}",
		systemProperties.graphicsProperties.maxSwapchainImageWidth,
		systemProperties.graphicsProperties.maxSwapchainImageHeight,
		systemProperties.graphicsProperties.maxLayerCount);
	spdlog::info("System Tracking Properties: OrientationTracking={} PositionTracking={}",
		systemProperties.trackingProperties.orientationTracking == XR_TRUE ? "True" : "False",
		systemProperties.trackingProperties.positionTracking == XR_TRUE ? "True" : "False");


	// Query and cache view configuration views.
	uint32_t viewCount;
	CHECK_XRCMD(
		xrEnumerateViewConfigurationViews(_xrInstance, _xrSystemId, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, 0, &viewCount, nullptr));
	_xrConfigViews.resize(viewCount, { XR_TYPE_VIEW_CONFIGURATION_VIEW });
	CHECK_XRCMD(xrEnumerateViewConfigurationViews(_xrInstance, _xrSystemId, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, viewCount,
		&viewCount, _xrConfigViews.data()));

	// Create and cache view buffer for xrLocateViews later.
	_xrViews.resize(viewCount, { XR_TYPE_VIEW });


	// Create the swapchain and get the images.
	if (viewCount > 0) {
		// Select a swapchain format.
		uint32_t swapchainFormatCount;
		CHECK_XRCMD(xrEnumerateSwapchainFormats(_xrSession, 0, &swapchainFormatCount, nullptr));
		std::vector<int64_t> swapchainFormats(swapchainFormatCount);
		CHECK_XRCMD(xrEnumerateSwapchainFormats(_xrSession, (uint32_t)swapchainFormats.size(), &swapchainFormatCount,
			swapchainFormats.data()));
		CHECK(swapchainFormatCount == swapchainFormats.size());

		// List of supported color swapchain formats.
		constexpr int64_t SupportedColorSwapchainFormats[] = { VK_FORMAT_B8G8R8A8_SRGB, VK_FORMAT_R8G8B8A8_SRGB,
															  VK_FORMAT_B8G8R8A8_UNORM, VK_FORMAT_R8G8B8A8_UNORM };

		auto swapchainFormatIt =
			std::find_first_of(swapchainFormats.begin(), swapchainFormats.end(), std::begin(SupportedColorSwapchainFormats),
				std::end(SupportedColorSwapchainFormats));
		if (swapchainFormatIt == swapchainFormats.end()) {
			THROW("No runtime swapchain format supported for color swapchain");
		}
		_swapchainImageFormat = (VkFormat)*swapchainFormatIt;

		// Print swapchain formats and the selected one.
		{
			std::string swapchainFormatsString;
			for (int64_t format : swapchainFormats) {
				const bool selected = format == _swapchainImageFormat;
				swapchainFormatsString += " ";
				if (selected) {
					swapchainFormatsString += "[";
				}
				swapchainFormatsString += std::to_string(format);
				if (selected) {
					swapchainFormatsString += "]";
				}
			}
			spdlog::debug("Swapchain Formats: {}", swapchainFormatsString.c_str());
		}

		// Create a swapchain for each view.
		for (uint32_t j = 0; j < viewCount; j++) {
			_xrRenderTargets.push_back(xrRenderTarget());
			const XrViewConfigurationView& vp = _xrConfigViews[j];
			spdlog::info("Creating swapchain for view {} with dimensions Width={} Height={} SampleCount={}", j,
				vp.recommendedImageRectWidth, vp.recommendedImageRectHeight, vp.recommendedSwapchainSampleCount);
			_xrRenderTargets[j].width = vp.recommendedImageRectWidth;
			_xrRenderTargets[j].height = vp.recommendedImageRectHeight;

			// Create the swapchain.
			XrSwapchainCreateInfo swapchainCreateInfo{ XR_TYPE_SWAPCHAIN_CREATE_INFO };
			swapchainCreateInfo.arraySize = 1;
			swapchainCreateInfo.format = _swapchainImageFormat;
			swapchainCreateInfo.width = vp.recommendedImageRectWidth;
			swapchainCreateInfo.height = vp.recommendedImageRectHeight;
			swapchainCreateInfo.mipCount = 1;
			swapchainCreateInfo.faceCount = 1;
			swapchainCreateInfo.sampleCount = 1;
			swapchainCreateInfo.usageFlags = XR_SWAPCHAIN_USAGE_SAMPLED_BIT | XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT;

			XrSwapchain swapchain;
			CHECK_XRCMD(xrCreateSwapchain(_xrSession, &swapchainCreateInfo, &swapchain));

			_xrSwapchains.push_back(swapchain);

			uint32_t imageCount;
			CHECK_XRCMD(xrEnumerateSwapchainImages(swapchain, 0, &imageCount, nullptr));
			// XXX This should really just return XrSwapchainImageBaseHeader*
			std::vector<XrSwapchainImageVulkan2KHR> _xrSwapchainImagesVulk(imageCount);
			std::vector<XrSwapchainImageBaseHeader*> bases(imageCount);
			
			_xrRenderTargets[j].xrImageViews.resize(imageCount);
			_xrRenderTargets[j].xrImages.resize(imageCount);
			for (uint32_t i = 0; i < imageCount; ++i) {
				_xrSwapchainImagesVulk[i] = { XR_TYPE_SWAPCHAIN_IMAGE_VULKAN2_KHR };
				bases[i] = reinterpret_cast<XrSwapchainImageBaseHeader*>(&_xrSwapchainImagesVulk[i]);
				
			}

			CHECK_XRCMD(xrEnumerateSwapchainImages(swapchain, imageCount, &imageCount, bases[0]));
			for (uint32_t i = 0; i < imageCount; ++i) {
				_xrRenderTargets[j].xrImages[i] = _xrSwapchainImagesVulk[i].image;
				VkImageViewCreateInfo colorViewInfo = vkinit::xr_imageview_create_info((VkFormat)_swapchainImageFormat, _xrRenderTargets[j].xrImages[i], VK_IMAGE_ASPECT_COLOR_BIT);
				vkCreateImageView(_device, &colorViewInfo, nullptr, &_xrRenderTargets[j].xrImageViews[i]);
				//add to deletion queues
				_mainDeletionQueue.push_function([=]() {
					vkDestroyImageView(_device, _xrRenderTargets[j].xrImageViews[i], nullptr);
					});
			}

			//depth image size will match the window
			VkExtent3D depthImageExtent = {
				_xrRenderTargets[j].width,
				_xrRenderTargets[j].height,
				1
			};

			/*VkExtent3D depthImageExtent = {
				1700,
				1700,
				1
			};*/

			//hardcoding the depth format to 32 bit float
			_depthFormat = VK_FORMAT_D32_SFLOAT;

			//the depth image will be an image with the format we selected and Depth Attachment usage flag
			VkImageCreateInfo dimg_info = vkinit::image_create_info(_depthFormat, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, depthImageExtent);

			//for the depth image, we want to allocate it from GPU local memory
			VmaAllocationCreateInfo dimg_allocinfo = {};
			dimg_allocinfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;
			dimg_allocinfo.requiredFlags = VkMemoryPropertyFlags(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

			//allocate and create the image
			vmaCreateImage(_allocator, &dimg_info, &dimg_allocinfo, &_xrRenderTargets[j].xrDepthImage, &_xrRenderTargets[j].xrDepthAllocation, nullptr);

			//build an image-view for the depth image to use for rendering
			VkImageViewCreateInfo dview_info = vkinit::imageview_create_info(_depthFormat, _xrRenderTargets[j].xrDepthImage, VK_IMAGE_ASPECT_DEPTH_BIT);

			VK_CHECK(vkCreateImageView(_device, &dview_info, nullptr, &_xrRenderTargets[j].xrDepthView));

			//add to deletion queues
			_mainDeletionQueue.push_function([=]() {
				vkDestroyImageView(_device, _xrRenderTargets[j].xrDepthView, nullptr);
				vmaDestroyImage(_allocator, _xrRenderTargets[j].xrDepthImage, _xrRenderTargets[j].xrDepthAllocation);
				});


		}
	}


}


void VulkanEngine::init_mirSwapchain() {

	SwapChainSupportDetails details;
	vkGetPhysicalDeviceSurfaceCapabilitiesKHR(_chosenGPU, _surface, &details.capabilities);

	uint32_t formatCount;
	vkGetPhysicalDeviceSurfaceFormatsKHR(_chosenGPU, _surface, &formatCount, nullptr);

	if (formatCount != 0) {
		details.formats.resize(formatCount);
		vkGetPhysicalDeviceSurfaceFormatsKHR(_chosenGPU, _surface, &formatCount, details.formats.data());
	}
	uint32_t presentModeCount;
	vkGetPhysicalDeviceSurfacePresentModesKHR(_chosenGPU, _surface, &presentModeCount, nullptr);

	if (presentModeCount != 0) {
		details.presentModes.resize(presentModeCount);
		vkGetPhysicalDeviceSurfacePresentModesKHR(_chosenGPU, _surface, &presentModeCount, details.presentModes.data());
	}

	VkSurfaceFormatKHR surfaceFormat;
	bool formatFound = false;
	for (const auto& availableFormat : details.formats) {
		if (availableFormat.format == VK_FORMAT_B8G8R8A8_SRGB && availableFormat.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
			surfaceFormat = availableFormat;
			formatFound = true;
		}
	}
	if (!formatFound) {
		surfaceFormat = details.formats[0];
	}

	VkPresentModeKHR presentMode;
	bool presentModeFound = false;
	for (const auto& availablePresentMode : details.presentModes) {
		if (availablePresentMode == VK_PRESENT_MODE_MAILBOX_KHR) {
			presentModeFound = true;
			presentMode = availablePresentMode;
		}
	}
	if (!presentModeFound) {
		presentMode = VK_PRESENT_MODE_FIFO_KHR;
	}

	VkExtent2D extent = {
		static_cast<uint32_t>(_xrRenderTargets[0].width),// / 4,
		static_cast<uint32_t>(_xrRenderTargets[0].height),// / 4
	};

	uint32_t imageCount = details.capabilities.minImageCount + 1;
	if (details.capabilities.maxImageCount > 0 && imageCount > details.capabilities.maxImageCount) {
		imageCount = details.capabilities.maxImageCount;
	}
	VkSwapchainCreateInfoKHR createInfo{};
	createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
	createInfo.surface = _surface;
	createInfo.minImageCount = imageCount;
	createInfo.imageFormat = surfaceFormat.format;
	createInfo.imageColorSpace = surfaceFormat.colorSpace;
	createInfo.imageExtent = extent;
	createInfo.imageArrayLayers = 1;
	createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
	createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
	createInfo.queueFamilyIndexCount = 0; // Optional
	createInfo.pQueueFamilyIndices = nullptr; // Optional
	createInfo.preTransform = details.capabilities.currentTransform;
	createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
	createInfo.presentMode = presentMode;
	createInfo.clipped = VK_TRUE;
	createInfo.oldSwapchain = VK_NULL_HANDLE;
	if (vkCreateSwapchainKHR(_device, &createInfo, nullptr, &_mirrorSwapchain) != VK_SUCCESS) {
		throw std::runtime_error("failed to create swap chain!");
	}
	_mainDeletionQueue.push_function([=]() {
		vkDestroySwapchainKHR(_device, _mirrorSwapchain, nullptr);
		});


	vkGetSwapchainImagesKHR(_device, _mirrorSwapchain, &imageCount, nullptr);
	_mirrorSwapchainImages.resize(imageCount);
	vkGetSwapchainImagesKHR(_device, _mirrorSwapchain, &imageCount, _mirrorSwapchainImages.data());
	_mirrorSwapchainImageFormat = surfaceFormat.format;
	_mirrorSwapchainImageViews.resize(_mirrorSwapchainImages.size());
	for (size_t i = 0; i < _mirrorSwapchainImages.size(); i++) {
		VkImageViewCreateInfo createInfo{};
		createInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		createInfo.image = _mirrorSwapchainImages[i];
		createInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
		createInfo.format = _mirrorSwapchainImageFormat;
		createInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
		createInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
		createInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
		createInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
		createInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		createInfo.subresourceRange.baseMipLevel = 0;
		createInfo.subresourceRange.levelCount = 1;
		createInfo.subresourceRange.baseArrayLayer = 0;
		createInfo.subresourceRange.layerCount = 1;
		if (vkCreateImageView(_device, &createInfo, nullptr, &_mirrorSwapchainImageViews[i]) != VK_SUCCESS) {
			throw std::runtime_error("failed to create image views!");
		}
		_mainDeletionQueue.push_function([=]() {
			vkDestroyImageView(_device, _mirrorSwapchainImageViews[i], nullptr);
			});
	}

}


void VulkanEngine::init_swapchain()
{
	vkb::SwapchainBuilder swapchainBuilder{ _chosenGPU,_device,_surface };

	vkb::Swapchain vkbSwapchain = swapchainBuilder
		.use_default_format_selection()
		//use vsync present mode
		.set_desired_present_mode(VK_PRESENT_MODE_FIFO_KHR)
		.set_desired_extent(_windowExtent.width, _windowExtent.height)
		.build()
		.value();

	//store swapchain and its related images
	_swapchain = vkbSwapchain.swapchain;
	_swapchainImages = vkbSwapchain.get_images().value();
	_swapchainImageViews = vkbSwapchain.get_image_views().value();

	_swapchainImageFormat = vkbSwapchain.image_format;

	_mainDeletionQueue.push_function([=]() {
		vkDestroySwapchainKHR(_device, _swapchain, nullptr);
		});


	int numImages = vkbSwapchain.image_count;
	//_swapchainDepthImages.reserve(numImages);
	//_swapchainDepthImageViews.reserve(numImages);
	for (int i = 0; i < numImages; i++) {
		_swapchainDepthImages.push_back(AllocatedImage());
		_swapchainDepthImageViews.push_back(VkImageView());
		//depth image size will match the window
		VkExtent3D depthImageExtent = {
			_windowExtent.width,
			_windowExtent.height,
			1
		};

		//hardcoding the depth format to 32 bit float
		_depthFormat = VK_FORMAT_D32_SFLOAT;

		//the depth image will be an image with the format we selected and Depth Attachment usage flag
		VkImageCreateInfo dimg_info = vkinit::image_create_info(_depthFormat, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, depthImageExtent);

		//for the depth image, we want to allocate it from GPU local memory
		VmaAllocationCreateInfo dimg_allocinfo = {};
		dimg_allocinfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;
		dimg_allocinfo.requiredFlags = VkMemoryPropertyFlags(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

		//allocate and create the image
		vmaCreateImage(_allocator, &dimg_info, &dimg_allocinfo, &_swapchainDepthImages[i]._image, &_swapchainDepthImages[i]._allocation, nullptr);

		//build an image-view for the depth image to use for rendering
		VkImageViewCreateInfo dview_info = vkinit::imageview_create_info(_depthFormat, _swapchainDepthImages[i]._image, VK_IMAGE_ASPECT_DEPTH_BIT);

		VK_CHECK(vkCreateImageView(_device, &dview_info, nullptr, &_swapchainDepthImageViews[i]));

		//add to deletion queues
		_mainDeletionQueue.push_function([=]() {
			vkDestroyImageView(_device, _swapchainDepthImageViews[i], nullptr);
			vmaDestroyImage(_allocator, _swapchainDepthImages[i]._image, _swapchainDepthImages[i]._allocation);
			});
	}
}



void VulkanEngine::immediate_submit(std::function<void(VkCommandBuffer cmd)>&& function)
{
	VkCommandBuffer cmd = _uploadContext._commandBuffer;

	//begin the command buffer recording. We will use this command buffer exactly once before resetting, so we tell vulkan that
	VkCommandBufferBeginInfo cmdBeginInfo = vkinit::command_buffer_begin_info(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);

	VK_CHECK(vkBeginCommandBuffer(cmd, &cmdBeginInfo));

	//execute the function
	function(cmd);

	VK_CHECK(vkEndCommandBuffer(cmd));

	VkSubmitInfo submit = vkinit::submit_info(&cmd);


	//submit command buffer to the queue and execute it.
	// _uploadFence will now block until the graphic commands finish execution
	VK_CHECK(vkQueueSubmit(_graphicsQueue, 1, &submit, _uploadContext._uploadFence));

	vkWaitForFences(_device, 1, &_uploadContext._uploadFence, true, 9999999999);
	vkResetFences(_device, 1, &_uploadContext._uploadFence);

	// reset the command buffers inside the command pool
	vkResetCommandPool(_device, _uploadContext._commandPool, 0);
}


void VulkanEngine::init_miscCommands() {
	//create a command pool for commands submitted to the graphics queue.
	//we also want the pool to allow for resetting of individual command buffers
	VkCommandPoolCreateInfo commandPoolInfo = vkinit::command_pool_create_info(_graphicsQueueFamily, VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT);

	for (int i = 0; i < FRAME_OVERLAP; i++) {

		//allocate the default command buffer that we will use for rendering
		VkCommandBufferAllocateInfo cmdAllocInfo = vkinit::command_buffer_allocate_info(_frames[i]._commandPool, 1);

		VK_CHECK(vkAllocateCommandBuffers(_device, &cmdAllocInfo, &_frames[i].shadowCommandBuffer));

		_mainDeletionQueue.push_function([=]() {
			vkDestroyCommandPool(_device, _frames[i]._commandPool, nullptr);
			});
	}
}



void VulkanEngine::init_commands()
{

	VkCommandPoolCreateInfo uploadCommandPoolInfo = vkinit::command_pool_create_info(_graphicsQueueFamily);
	//create pool for upload context
	VK_CHECK(vkCreateCommandPool(_device, &uploadCommandPoolInfo, nullptr, &_uploadContext._commandPool));

	_mainDeletionQueue.push_function([=]() {
		vkDestroyCommandPool(_device, _uploadContext._commandPool, nullptr);
		});

	//allocate the default command buffer that we will use for the instant commands
	VkCommandBufferAllocateInfo cmdAllocInfo = vkinit::command_buffer_allocate_info(_uploadContext._commandPool, 1);

	VK_CHECK(vkAllocateCommandBuffers(_device, &cmdAllocInfo, &_uploadContext._commandBuffer));

	//create a command pool for commands submitted to the graphics queue.
	//we also want the pool to allow for resetting of individual command buffers
	VkCommandPoolCreateInfo commandPoolInfo = vkinit::command_pool_create_info(_graphicsQueueFamily, VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT);

	for (int i = 0; i < FRAME_OVERLAP; i++) {


		VK_CHECK(vkCreateCommandPool(_device, &commandPoolInfo, nullptr, &_frames[i]._commandPool));

		//allocate the default command buffer that we will use for rendering
		VkCommandBufferAllocateInfo cmdAllocInfo = vkinit::command_buffer_allocate_info(_frames[i]._commandPool, 1);

		VK_CHECK(vkAllocateCommandBuffers(_device, &cmdAllocInfo, &_frames[i]._mainCommandBuffer));

		_mainDeletionQueue.push_function([=]() {
			vkDestroyCommandPool(_device, _frames[i]._commandPool, nullptr);
			});
	}
}






void VulkanEngine::init_default_xrRenderpass()
{
	for (int j = 0; j < 2; j++) {
		// the renderpass will use this color attachment.
		VkAttachmentDescription color_attachment = {};
		//the attachment will have the format needed by the swapchain
		color_attachment.format = _swapchainImageFormat;
		//1 sample, we won't be doing MSAA
		color_attachment.samples = VK_SAMPLE_COUNT_1_BIT;
		// we Clear when this attachment is loaded
		color_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		// we keep the attachment stored when the renderpass ends
		color_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		//we don't care about stencil
		color_attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		color_attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;

		//we don't know or care about the starting layout of the attachment
		color_attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

		//after the renderpass ends, the image has to be on a layout ready for display
		color_attachment.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;




		//SUBPASS
		VkAttachmentReference color_attachment_ref = {};
		//attachment number will index into the pAttachments array in the parent renderpass itself
		color_attachment_ref.attachment = 0;
		color_attachment_ref.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

		//we are going to create 1 subpass, which is the minimum you can do
		VkSubpassDescription subpass = {};
		subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
		subpass.colorAttachmentCount = 1;
		subpass.pColorAttachments = &color_attachment_ref;




		//RENDERPASS
		VkRenderPassCreateInfo render_pass_info = {};
		render_pass_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;

		//connect the color attachment to the info
		render_pass_info.attachmentCount = 1;
		render_pass_info.pAttachments = &color_attachment;
		//connect the subpass to the info
		render_pass_info.subpassCount = 1;
		render_pass_info.pSubpasses = &subpass;




		VkAttachmentDescription depth_attachment = {};
		// Depth attachment
		depth_attachment.flags = 0;
		depth_attachment.format = _depthFormat;
		depth_attachment.samples = VK_SAMPLE_COUNT_1_BIT;
		depth_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		depth_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		depth_attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		depth_attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		depth_attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		depth_attachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

		VkAttachmentReference depth_attachment_ref = {};
		depth_attachment_ref.attachment = 1;
		depth_attachment_ref.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

		//we are going to create 1 subpass, which is the minimum you can do
		//VkSubpassDescription subpass = {};
		subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
		subpass.colorAttachmentCount = 1;
		subpass.pColorAttachments = &color_attachment_ref;
		//hook the depth attachment into the subpass
		subpass.pDepthStencilAttachment = &depth_attachment_ref;

		//array of 2 attachments, one for the color, and other for depth
		VkAttachmentDescription attachments[2] = { color_attachment,depth_attachment };

		//VkRenderPassCreateInfo render_pass_info = {};
		render_pass_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
		//2 attachments from said array
		render_pass_info.attachmentCount = 2;
		render_pass_info.pAttachments = &attachments[0];
		render_pass_info.subpassCount = 1;
		render_pass_info.pSubpasses = &subpass;


		VkSubpassDependency dependency = {};
		dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
		dependency.dstSubpass = 0;
		dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		dependency.srcAccessMask = 0;
		dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

		VkSubpassDependency depth_dependency = {};
		depth_dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
		depth_dependency.dstSubpass = 0;
		depth_dependency.srcStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
		depth_dependency.srcAccessMask = 0;
		depth_dependency.dstStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
		depth_dependency.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

		/*
		VkSubpassDependency shadow_dependency{};
		dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
		dependency.dstSubpass = 0;
		dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
		dependency.dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
		dependency.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
		dependency.dstAccessMask = VK_ACCESS_INPUT_ATTACHMENT_READ_BIT;
		dependency.dependencyFlags = 0;


		VkSubpassDependency dependencies[3] = { dependency, depth_dependency, shadow_dependency};


		render_pass_info.dependencyCount = 3;
		*/

		VkSubpassDependency dependencies[2] = { dependency, depth_dependency};


		render_pass_info.dependencyCount = 2;

		render_pass_info.pDependencies = &dependencies[0];

		VK_CHECK(vkCreateRenderPass(_device, &render_pass_info, nullptr, &_xrRenderTargets[j].renderPass));


		_mainDeletionQueue.push_function([=]() {
			vkDestroyRenderPass(_device, _xrRenderTargets[j].renderPass, nullptr);
			});
	}

}


void VulkanEngine::init_shadow_Renderpass() {
	VkAttachmentDescription attachments[2];

	// Depth attachment (shadow map)
	attachments[0].format = VK_FORMAT_D32_SFLOAT;
	attachments[0].samples = VK_SAMPLE_COUNT_1_BIT;
	attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	attachments[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	attachments[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	attachments[0].finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	attachments[0].flags = 0;

	// Attachment references from subpasses
	VkAttachmentReference depth_ref;
	depth_ref.attachment = 0;
	depth_ref.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

	// Subpass 0: shadow map rendering
	VkSubpassDescription subpass[1];
	subpass[0].pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
	subpass[0].flags = 0;
	subpass[0].inputAttachmentCount = 0;
	subpass[0].pInputAttachments = NULL;
	subpass[0].colorAttachmentCount = 0;
	subpass[0].pColorAttachments = NULL;
	subpass[0].pResolveAttachments = NULL;
	subpass[0].pDepthStencilAttachment = &depth_ref;
	subpass[0].preserveAttachmentCount = 0;
	subpass[0].pPreserveAttachments = NULL;

	// Create render pass
	VkRenderPassCreateInfo rp_info;
	rp_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
	rp_info.pNext = NULL;
	rp_info.attachmentCount = 1;
	rp_info.pAttachments = attachments;
	rp_info.subpassCount = 1;
	rp_info.pSubpasses = subpass;
	rp_info.dependencyCount = 0;
	rp_info.pDependencies = NULL;
	rp_info.flags = 0;

	
	VK_CHECK(vkCreateRenderPass(_device, &rp_info, NULL, &shadowPassData.rp));
}


void VulkanEngine::init_default_renderpass()
{
	// the renderpass will use this color attachment.
	VkAttachmentDescription color_attachment = {};
	//the attachment will have the format needed by the swapchain
	color_attachment.format = _swapchainImageFormat;
	//1 sample, we won't be doing MSAA
	color_attachment.samples = VK_SAMPLE_COUNT_1_BIT;
	// we Clear when this attachment is loaded
	color_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	// we keep the attachment stored when the renderpass ends
	color_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	//we don't care about stencil
	color_attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	color_attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;

	//we don't know or care about the starting layout of the attachment
	color_attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

	//after the renderpass ends, the image has to be on a layout ready for display
	color_attachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;




	//SUBPASS
	VkAttachmentReference color_attachment_ref = {};
	//attachment number will index into the pAttachments array in the parent renderpass itself
	color_attachment_ref.attachment = 0;
	color_attachment_ref.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

	//we are going to create 1 subpass, which is the minimum you can do
	VkSubpassDescription subpass = {};
	subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
	subpass.colorAttachmentCount = 1;
	subpass.pColorAttachments = &color_attachment_ref;




	//RENDERPASS
	VkRenderPassCreateInfo render_pass_info = {};
	render_pass_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;

	//connect the color attachment to the info
	render_pass_info.attachmentCount = 1;
	render_pass_info.pAttachments = &color_attachment;
	//connect the subpass to the info
	render_pass_info.subpassCount = 1;
	render_pass_info.pSubpasses = &subpass;




	VkAttachmentDescription depth_attachment = {};
	// Depth attachment
	depth_attachment.flags = 0;
	depth_attachment.format = _depthFormat;
	depth_attachment.samples = VK_SAMPLE_COUNT_1_BIT;
	depth_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	depth_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	depth_attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	depth_attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	depth_attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	depth_attachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

	VkAttachmentReference depth_attachment_ref = {};
	depth_attachment_ref.attachment = 1;
	depth_attachment_ref.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

	//we are going to create 1 subpass, which is the minimum you can do
	//VkSubpassDescription subpass = {};
	subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
	subpass.colorAttachmentCount = 1;
	subpass.pColorAttachments = &color_attachment_ref;
	//hook the depth attachment into the subpass
	subpass.pDepthStencilAttachment = &depth_attachment_ref;

	//array of 2 attachments, one for the color, and other for depth
	VkAttachmentDescription attachments[2] = { color_attachment,depth_attachment };

	//VkRenderPassCreateInfo render_pass_info = {};
	render_pass_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
	//2 attachments from said array
	render_pass_info.attachmentCount = 2;
	render_pass_info.pAttachments = &attachments[0];
	render_pass_info.subpassCount = 1;
	render_pass_info.pSubpasses = &subpass;


	VkSubpassDependency dependency = {};
	dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
	dependency.dstSubpass = 0;
	dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	dependency.srcAccessMask = 0;
	dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

	VkSubpassDependency depth_dependency = {};
	depth_dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
	depth_dependency.dstSubpass = 0;
	depth_dependency.srcStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
	depth_dependency.srcAccessMask = 0;
	depth_dependency.dstStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
	depth_dependency.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;


	VkSubpassDependency dependencies[2] = { dependency, depth_dependency };


	render_pass_info.dependencyCount = 2;
	render_pass_info.pDependencies = &dependencies[0];

	VK_CHECK(vkCreateRenderPass(_device, &render_pass_info, nullptr, &_renderPass));


	_mainDeletionQueue.push_function([=]() {
		vkDestroyRenderPass(_device, _renderPass, nullptr);
		});
}


void VulkanEngine::init_descriptors()
{

	_descriptorAllocator = new vkutil::DescriptorAllocator{};
	_descriptorAllocator->init(_device);

	_descriptorLayoutCache = new vkutil::DescriptorLayoutCache{};
	_descriptorLayoutCache->init(_device);

	_mainDeletionQueue.push_function([&]() {
		// other code ....
		_descriptorAllocator->cleanup();
		_descriptorLayoutCache->cleanup();
		delete _descriptorAllocator;
		delete _descriptorLayoutCache;
		});



	//binding for camera data at 0
	VkDescriptorSetLayoutBinding cameraBind = vkinit::descriptorset_layout_binding(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_VERTEX_BIT, 0);
	//binding for scene data at 1
	VkDescriptorSetLayoutBinding sceneBind = vkinit::descriptorset_layout_binding(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 1);
	VkDescriptorSetLayoutBinding bindings[] = { cameraBind,sceneBind };
	VkDescriptorSetLayoutCreateInfo setinfo = {};
	setinfo.bindingCount = 2;
	setinfo.flags = 0;
	setinfo.pNext = nullptr;
	setinfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	setinfo.pBindings = bindings;
	_globalSetLayout = _descriptorLayoutCache->create_descriptor_layout(&setinfo);



	VkDescriptorSetLayoutBinding objectBind = vkinit::descriptorset_layout_binding(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_VERTEX_BIT, 0);
	VkDescriptorSetLayoutCreateInfo set2info = {};
	set2info.bindingCount = 1;
	set2info.flags = 0;
	set2info.pNext = nullptr;
	set2info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	set2info.pBindings = &objectBind;
	_objectSetLayout = _descriptorLayoutCache->create_descriptor_layout(&set2info);



	const size_t sceneParamBufferSize = FRAME_OVERLAP * pad_uniform_buffer_size(sizeof(GPUSceneData));
	_sceneParameterBuffer = create_buffer(sceneParamBufferSize, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);
	_mainDeletionQueue.push_function([&]() {
		// other code ....
		vmaDestroyBuffer(_allocator, _sceneParameterBuffer._buffer, _sceneParameterBuffer._allocation);
		});


	//another set, one that holds a single texture
	VkDescriptorSetLayoutBinding textureBind = vkinit::descriptorset_layout_binding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, 0);
	VkDescriptorSetLayoutCreateInfo set3info = {};
	set3info.bindingCount = 1;
	set3info.flags = 0;
	set3info.pNext = nullptr;
	set3info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	set3info.pBindings = &textureBind;
	_singleTextureSetLayout = _descriptorLayoutCache->create_descriptor_layout(&set3info);
	shadowTextureSetLayout = _descriptorLayoutCache->create_descriptor_layout(&set3info);
	boneTransformSetLayout = _descriptorLayoutCache->create_descriptor_layout(&set2info); //Animation Buffer


	for (int i = 0; i < FRAME_OVERLAP; i++)
	{
		const int MAX_BONES = 100;
		const int MAX_OBJECTS = 10000;
		_frames[i].objectBuffer = create_buffer(sizeof(GPUObjectData) * MAX_OBJECTS, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);
		_frames[i].cameraBuffer = create_buffer(sizeof(GPUCameraData), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);
		_frames[i].animationBuffer = create_buffer(sizeof(BoneTransformData) * MAX_BONES, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);
		VkDescriptorBufferInfo cameraInfo;
		cameraInfo.buffer = _frames[i].cameraBuffer._buffer;
		cameraInfo.offset = 0;
		cameraInfo.range = sizeof(GPUCameraData);

		VkDescriptorBufferInfo sceneInfo;
		sceneInfo.buffer = _sceneParameterBuffer._buffer;
		sceneInfo.offset = 0;
		sceneInfo.range = sizeof(GPUSceneData);

		VkDescriptorBufferInfo objectBufferInfo;
		objectBufferInfo.buffer = _frames[i].objectBuffer._buffer;
		objectBufferInfo.offset = 0;
		objectBufferInfo.range = sizeof(GPUObjectData) * MAX_OBJECTS;

		VkDescriptorBufferInfo animInfo;
		animInfo.buffer = _frames[i].animationBuffer._buffer;
		animInfo.offset = 0;
		animInfo.range = sizeof(BoneTransformData) * character.numBones;


		vkutil::DescriptorBuilder::begin(_descriptorLayoutCache, _descriptorAllocator)
			.bind_buffer(0, &cameraInfo, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_VERTEX_BIT)
			.bind_buffer(1, &sceneInfo, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT)
			.build(_frames[i].globalDescriptor, _globalSetLayout);

		vkutil::DescriptorBuilder::begin(_descriptorLayoutCache, _descriptorAllocator)
			.bind_buffer(0, &objectBufferInfo, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_VERTEX_BIT)
			.build(_frames[i].objectDescriptor, _objectSetLayout);

		vkutil::DescriptorBuilder::begin(_descriptorLayoutCache, _descriptorAllocator)
			.bind_buffer(0, &objectBufferInfo, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_VERTEX_BIT)
			.build(_frames[i].animationDescriptor, boneTransformSetLayout);

	}
	// add buffers to deletion queues
	for (int i = 0; i < FRAME_OVERLAP; i++)
	{
		_mainDeletionQueue.push_function([&, i]() {
			vmaDestroyBuffer(_allocator, _frames[i].cameraBuffer._buffer, _frames[i].cameraBuffer._allocation);
			vmaDestroyBuffer(_allocator, _frames[i].objectBuffer._buffer, _frames[i].objectBuffer._allocation);
			vmaDestroyBuffer(_allocator, _frames[i].animationBuffer._buffer, _frames[i].animationBuffer._allocation);
			});
	}

}

void VulkanEngine::init_miscFramebuffers() {
	//create the framebuffers for the swapchain images. This will connect the render-pass to the images for rendering
	VkFramebufferCreateInfo fb_info = {};
	fb_info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
	fb_info.pNext = nullptr;

	fb_info.renderPass = shadowPassData.rp;
	fb_info.attachmentCount = 1;
	fb_info.width = shadowMapResolution.width;
	fb_info.height = shadowMapResolution.height;
	fb_info.layers = 1;


	VkImageView attachments[2];
	attachments[0] = shadowImage.imageView;

	fb_info.pAttachments = attachments;
	fb_info.attachmentCount = 1;

	VK_CHECK(vkCreateFramebuffer(_device, &fb_info, nullptr, &shadowPassData.fb));

	_mainDeletionQueue.push_function([=]() {
		vkDestroyFramebuffer(_device, shadowPassData.fb, nullptr);
		});
	
}

void VulkanEngine::init_xrFramebuffers()
{
	for (int j = 0; j < 2; j++) {
		//create the framebuffers for the swapchain images. This will connect the render-pass to the images for rendering
		VkFramebufferCreateInfo fb_info = {};
		fb_info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
		fb_info.pNext = nullptr;

		fb_info.renderPass = _xrRenderTargets[j].renderPass;
		fb_info.attachmentCount = 1;
		fb_info.width = _xrRenderTargets[j].width;
		fb_info.height = _xrRenderTargets[j].height;
		fb_info.layers = 1;

		//grab how many images we have in the swapchain
		const uint32_t swapchain_imagecount = _xrRenderTargets[j].xrImages.size();
		_xrRenderTargets[j].framebuffers = std::vector<VkFramebuffer>(swapchain_imagecount);

		//create framebuffers for each of the swapchain image views
		for (int i = 0; i < swapchain_imagecount; i++) {

			VkImageView attachments[2];
			attachments[0] = _xrRenderTargets[j].xrImageViews[i];
			attachments[1] = _xrRenderTargets[j].xrDepthView;

			fb_info.pAttachments = attachments;
			fb_info.attachmentCount = 2;

			VK_CHECK(vkCreateFramebuffer(_device, &fb_info, nullptr, &_xrRenderTargets[j].framebuffers[i]));

			_mainDeletionQueue.push_function([=]() {
				vkDestroyFramebuffer(_device, _xrRenderTargets[j].framebuffers[i], nullptr);
				});
		}
	}


}

void VulkanEngine::init_framebuffers()
{
	//create the framebuffers for the swapchain images. This will connect the render-pass to the images for rendering
	VkFramebufferCreateInfo fb_info = {};
	fb_info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
	fb_info.pNext = nullptr;

	fb_info.renderPass = _renderPass;
	fb_info.attachmentCount = 1;
	fb_info.width = _windowExtent.width;
	fb_info.height = _windowExtent.height;
	fb_info.layers = 1;

	//grab how many images we have in the swapchain
	const uint32_t swapchain_imagecount = _swapchainImages.size();
	_framebuffers = std::vector<VkFramebuffer>(swapchain_imagecount);

	//create framebuffers for each of the swapchain image views
	for (int i = 0; i < swapchain_imagecount; i++) {

		VkImageView attachments[2];
		attachments[0] = _swapchainImageViews[i];
		attachments[1] = _swapchainDepthImageViews[i];

		fb_info.pAttachments = attachments;
		fb_info.attachmentCount = 2;

		VK_CHECK(vkCreateFramebuffer(_device, &fb_info, nullptr, &_framebuffers[i]));

		_mainDeletionQueue.push_function([=]() {
			vkDestroyFramebuffer(_device, _framebuffers[i], nullptr);
			vkDestroyImageView(_device, _swapchainImageViews[i], nullptr);
			});
	}

}

void VulkanEngine::init_sync_structures()
{
	VkFenceCreateInfo uploadFenceCreateInfo = vkinit::fence_create_info();

	VK_CHECK(vkCreateFence(_device, &uploadFenceCreateInfo, nullptr, &_uploadContext._uploadFence));
	_mainDeletionQueue.push_function([=]() {
		vkDestroyFence(_device, _uploadContext._uploadFence, nullptr);
		});

	VkFenceCreateInfo fenceCreateInfo = vkinit::fence_create_info(VK_FENCE_CREATE_SIGNALED_BIT);

	VkSemaphoreCreateInfo semaphoreCreateInfo = vkinit::semaphore_create_info();

	for (int i = 0; i < FRAME_OVERLAP; i++) {

		VK_CHECK(vkCreateFence(_device, &fenceCreateInfo, nullptr, &_frames[i]._renderFence));

		//enqueue the destruction of the fence
		_mainDeletionQueue.push_function([=]() {
			vkDestroyFence(_device, _frames[i]._renderFence, nullptr);
			});


		VK_CHECK(vkCreateSemaphore(_device, &semaphoreCreateInfo, nullptr, &_frames[i]._presentSemaphore));
		VK_CHECK(vkCreateSemaphore(_device, &semaphoreCreateInfo, nullptr, &_frames[i]._renderSemaphore));

		//enqueue the destruction of semaphores
		_mainDeletionQueue.push_function([=]() {
			vkDestroySemaphore(_device, _frames[i]._presentSemaphore, nullptr);
			vkDestroySemaphore(_device, _frames[i]._renderSemaphore, nullptr);
			});
	}
}



void VulkanEngine::cleanup()
{
	if (_isInitialized) {

		//make sure the GPU has stopped doing its things
		vkWaitForFences(_device, 1, &get_current_frame()._renderFence, true, 1000000000);

		_mainDeletionQueue.flush();
		vmaDestroyAllocator(_allocator); //TODO: Place i nright place or put into deletion queue

		vkDestroySurfaceKHR(_instance, _surface, nullptr);

		vkDestroyDevice(_device, nullptr);
		vkDestroyInstance(_instance, nullptr);

		SDL_DestroyWindow(_window);
	}
}

void VulkanEngine::xrCleanup()
{
	if (_isInitialized) {

		//make sure the GPU has stopped doing its things
		vkWaitForFences(_device, 1, &get_current_frame()._renderFence, true, 1000000000);

		_mainDeletionQueue.flush();
		vmaDestroyAllocator(_allocator); //TODO: Place i nright place or put into deletion queue


		vkDestroySurfaceKHR(_instance, _surface, nullptr);
		vkDestroyDevice(_device, nullptr);
		vkDestroyInstance(_instance, nullptr);

		SDL_DestroyWindow(_window);
	}
}


void VulkanEngine::xrDraw()
{
	//ImGui::Render();
	//FPS Count
	auto finish = std::chrono::high_resolution_clock::now();
	float fps;
	if (!duration_cast<std::chrono::milliseconds>(finish - _previousTime).count() == 0) {
		fps = 1000 / duration_cast<std::chrono::milliseconds>(finish - _previousTime).count();
		deltaTime = 1 / fps;
		
	}
	if (_frameNumber % 20 == 0) {
		std::cout << "FPS: " << fps << "\n";


	}
	_previousTime = finish;

	CHECK(_xrSession != XR_NULL_HANDLE);


	XrFrameWaitInfo frameWaitInfo{ XR_TYPE_FRAME_WAIT_INFO };
	XrFrameState frameState{ XR_TYPE_FRAME_STATE };
	CHECK_XRCMD(xrWaitFrame(_xrSession, &frameWaitInfo, &frameState));

	XrFrameBeginInfo frameBeginInfo{ XR_TYPE_FRAME_BEGIN_INFO };
	CHECK_XRCMD(xrBeginFrame(_xrSession, &frameBeginInfo));

	std::vector<XrCompositionLayerBaseHeader*> layers;
	XrCompositionLayerProjection layer{ XR_TYPE_COMPOSITION_LAYER_PROJECTION };
	std::vector<XrCompositionLayerProjectionView> projectionLayerViews;
	//if (frameState.shouldRender == XR_TRUE) {
	if (XR_TRUE) {
		_predictedDisplayTime = frameState.predictedDisplayTime;
		if (xrRenderLayer(frameState.predictedDisplayTime, projectionLayerViews, layer)) {
			layers.push_back(reinterpret_cast<XrCompositionLayerBaseHeader*>(&layer));
		}

	}
	XrFrameEndInfo frameEndInfo{ XR_TYPE_FRAME_END_INFO };
	frameEndInfo.displayTime = frameState.predictedDisplayTime;
	frameEndInfo.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
	frameEndInfo.layerCount = (uint32_t)layers.size();
	frameEndInfo.layers = layers.data();
	CHECK_XRCMD(xrEndFrame(_xrSession, &frameEndInfo));
	//increase the number of frames drawn
	_frameNumber++;
}

void VulkanEngine::shadowPass(VkCommandBuffer cmd) {
	//Shaddow Pass
	VkClearValue clear_values[1];
	clear_values[0].depthStencil.depth = 1.0f;
	clear_values[0].depthStencil.stencil = 0;

	VkRenderPassBeginInfo rp_begin;
	rp_begin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
	rp_begin.pNext = NULL;
	rp_begin.renderPass = shadowPassData.rp;
	rp_begin.framebuffer = shadowPassData.fb;
	rp_begin.renderArea.offset.x = 0;
	rp_begin.renderArea.offset.y = 0;
	rp_begin.renderArea.extent.width = shadowMapResolution.width;
	rp_begin.renderArea.extent.height = shadowMapResolution.height;
	rp_begin.clearValueCount = 1;
	rp_begin.pClearValues = clear_values;

	vkCmdBeginRenderPass(cmd,
		&rp_begin,
		VK_SUBPASS_CONTENTS_INLINE);

	VkViewport viewport;
	viewport.height = shadowMapResolution.height;
	viewport.width = shadowMapResolution.width;
	viewport.minDepth = 0.0f;
	viewport.maxDepth = 1.0f;
	viewport.x = 0;
	viewport.y = 0;
	vkCmdSetViewport(cmd, 0, 1, &viewport);

	VkRect2D scissor;
	scissor.extent.width = shadowMapResolution.width;
	scissor.extent.height = shadowMapResolution.height;
	scissor.offset.x = 0;
	scissor.offset.y = 0;
	vkCmdSetScissor(cmd, 0, 1, &scissor);

	
	
	//glm::mat4 projection = glm::perspective(45.0f, 1.0f, 0.05f, 100.0f);
	//glm::mat4 view = glm::translate(view, glm::vec3(0.0f, 1.5f, 0.0f));


	XrResult res;
	XrSpaceLocation spaceLocation{ XR_TYPE_SPACE_LOCATION };
	res = xrLocateSpace(m_input.handSpace[Side::RIGHT], _xrSpace, _predictedDisplayTime, &spaceLocation);
	CHECK_XRRESULT(res, "xrLocateSpace");
	if (XR_UNQUALIFIED_SUCCESS(res)) {
		if ((spaceLocation.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT) != 0 &&
			(spaceLocation.locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT) != 0) {
			handPose = spaceLocation.pose;
		}
	}





	float near_plane = 1.0f, far_plane = 20.0f;
	//glm::mat4 projection = glm::ortho(-10.0f, 10.0f, -10.0f, 10.0f, near_plane, far_plane);
	glm::mat4 projection = glm::perspective(45.0f, 1.0f, near_plane, far_plane);
	glm::mat4 view = glm::lookAt(glm::vec3(0.0f, 6.0f, 0.0f),
		glm::vec3(0.0f, 0.0f, 0.0f),
		glm::vec3(1.0f, 0.0f, 1.0f));


	//fill a GPU camera data struct
	GPUCameraData camData;
	camData.proj = projection;
	camData.view = view;
	camData.viewproj =  view * projection;

	lightSpaceMatrix = projection * view;

	//and copy it to the buffer
	void* data;
	vmaMapMemory(_allocator, get_current_frame().cameraBuffer._allocation, &data);

	memcpy(data, &camData, sizeof(GPUCameraData));

	vmaUnmapMemory(_allocator, get_current_frame().cameraBuffer._allocation);
	float framed = (_frameNumber / 120.f);

	_sceneParameters.ambientColor = { sin(framed),0,cos(framed),1 };

	char* sceneData;
	vmaMapMemory(_allocator, _sceneParameterBuffer._allocation, (void**)&sceneData);

	int frameIndex = _frameNumber % FRAME_OVERLAP;

	sceneData += pad_uniform_buffer_size(sizeof(GPUSceneData)) * frameIndex;

	memcpy(sceneData, &_sceneParameters, sizeof(GPUSceneData));

	vmaUnmapMemory(_allocator, _sceneParameterBuffer._allocation);



	Mesh* lastMesh = nullptr;
	Material* lastMaterial = nullptr;
	auto regview = mainScene.registry.view<RenderObject, transformation, baseGameobject>();



	void* objectData;
	vmaMapMemory(_allocator, get_current_frame().objectBuffer._allocation, &objectData);
	GPUObjectData* objectSSBO = (GPUObjectData*)objectData;
	int object_index = 0;
	for (auto entity : regview) {
		RenderObject& object = regview.get<RenderObject>(entity);

		//Workout Model Transform
		glm::mat4 model = glm::mat4(1.0f);
		std::stack<glm::mat4> cumulative;
		cumulative.push(regview.get<transformation>(entity).get_mat());
		Entity* parent = regview.get<baseGameobject>(entity).parent;
		while (parent != NULL) {
			cumulative.push(parent->get_component<transformation>().get_mat());
			parent = parent->get_component<baseGameobject>().parent;
		}
		while (!cumulative.empty()) {
			model = model * cumulative.top();
			cumulative.pop();
		}
		objectSSBO[object_index].modelMatrix = model;
		object_index++;
	}
	vmaUnmapMemory(_allocator, get_current_frame().objectBuffer._allocation);

	object_index = 0;
	bool bound = false;
	for (auto entity : regview)
	{
		RenderObject& object = regview.get<RenderObject>(entity);

		if (object.mesh == nullptr) {
			object_index++;
			continue;
		}
		//only bind the pipeline if it doesn't match with the already bound one
		if (!bound) {
			bound = true;
			vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, shadowPassData.pl);
			lastMaterial = object.material;
			//offset for our scene buffer
			uint32_t uniform_offset = pad_uniform_buffer_size(sizeof(GPUSceneData)) * frameIndex;

			vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, shadowPassData.pipelineLayout, 0, 1, &get_current_frame().globalDescriptor, 1, &uniform_offset);
			//object data descriptor
			vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, shadowPassData.pipelineLayout, 1, 1, &get_current_frame().objectDescriptor, 0, nullptr);
			if (object.material->textureSet != VK_NULL_HANDLE) {
				//texture descriptor
				vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, shadowPassData.pipelineLayout, 2, 1, &object.material->textureSet, 0, nullptr);
			}
			ShadowPushConstants constants;
			constants.cameraVP = projection * view;
			vkCmdPushConstants(cmd, object.material->pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(ShadowPushConstants), &constants);
		}


		if (regview.get<baseGameobject>(entity).name == "monkey") {
			//regview.get<transformation>(entity).rotation.y += 0.005f;
		}

		if (regview.get<baseGameobject>(entity).name == "hand") {

			XrResult res;
			XrSpaceLocation spaceLocation{ XR_TYPE_SPACE_LOCATION };
			res = xrLocateSpace(m_input.handSpace[Side::RIGHT], _xrSpace, _predictedDisplayTime, &spaceLocation);
			CHECK_XRRESULT(res, "xrLocateSpace");
			if (XR_UNQUALIFIED_SUCCESS(res)) {
				if ((spaceLocation.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT) != 0 &&
					(spaceLocation.locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT) != 0) {
					//std::cout << "Found" << std::endl;
					regview.get<transformation>(entity).position = -_camPos + 1.0f * glm::vec3(spaceLocation.pose.position.x, spaceLocation.pose.position.y, spaceLocation.pose.position.z);
					regview.get<transformation>(entity).scale = glm::vec3(0.1f, 0.1f, 0.1f);
				}
			}
		}



		//only bind the mesh if it's a different one from last bind
		if (object.mesh != lastMesh) {
			//bind the mesh vertex buffer with offset 0
			VkDeviceSize offset = 0;
			vkCmdBindVertexBuffers(cmd, 0, 1, &object.mesh->_vertexBuffer._buffer, &offset);
			lastMesh = object.mesh;
		}
		//we can now draw
		vkCmdDraw(cmd, object.mesh->_vertices.size(), 1, 0, object_index);
		object_index++;
	}

	vkCmdEndRenderPass(cmd);

}

bool VulkanEngine::xrRenderLayer(XrTime predictedDisplayTime, std::vector<XrCompositionLayerProjectionView>& projectionLayerViews,
	XrCompositionLayerProjection& layer) {
	XrResult res;

	XrViewState viewState{ XR_TYPE_VIEW_STATE };
	uint32_t viewCapacityInput = (uint32_t)_xrViews.size();
	uint32_t viewCountOutput;

	XrViewLocateInfo viewLocateInfo{ XR_TYPE_VIEW_LOCATE_INFO };
	viewLocateInfo.viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
	viewLocateInfo.displayTime = predictedDisplayTime;
	viewLocateInfo.space = _xrSpace;

	res = xrLocateViews(_xrSession, &viewLocateInfo, &viewState, viewCapacityInput, &viewCountOutput, _xrViews.data());
	CHECK_XRRESULT(res, "xrLocateViews");
	if ((viewState.viewStateFlags & XR_VIEW_STATE_POSITION_VALID_BIT) == 0 ||
		(viewState.viewStateFlags & XR_VIEW_STATE_ORIENTATION_VALID_BIT) == 0) {
		return false;  // There is no valid tracking poses for the views.
	}

	CHECK(viewCountOutput == viewCapacityInput);
	projectionLayerViews.resize(viewCountOutput);

	
	//wait until the GPU has finished rendering the last frame. Timeout of 1 second
	VK_CHECK(vkWaitForFences(_device, 1, &get_current_frame()._renderFence, true, 1000000000));
	VK_CHECK(vkResetFences(_device, 1, &get_current_frame()._renderFence));
	uint32_t mirrSwapchainImageIndex;
	//request image from the mirror swapchain, one second timeout
	VK_CHECK(vkAcquireNextImageKHR(_device, _mirrorSwapchain, 1000000000, get_current_frame()._presentSemaphore, nullptr, &mirrSwapchainImageIndex));
	VK_CHECK(vkResetCommandBuffer(get_current_frame()._mainCommandBuffer, 0));
	//naming it cmd for shorter writing
	VkCommandBuffer cmd = get_current_frame()._mainCommandBuffer;
	VkCommandBufferBeginInfo cmdBeginInfo = {};
	cmdBeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	cmdBeginInfo.pNext = nullptr;

	cmdBeginInfo.pInheritanceInfo = nullptr;
	cmdBeginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
	VK_CHECK(vkBeginCommandBuffer(cmd, &cmdBeginInfo));


	shadowPass(cmd);

	character.update_anim();
	void* dataptr;
	vmaMapMemory(_allocator, get_current_frame().animationBuffer._allocation, &dataptr);
	BoneTransformData* boneUniBuffer = (BoneTransformData*)dataptr;
	memcpy(boneUniBuffer, character.boneTransforms.data(), character.boneTransforms.size() * sizeof(BoneTransformData));
	vmaUnmapMemory(_allocator, get_current_frame().animationBuffer._allocation);


	for (uint32_t i = 0; i < viewCountOutput; i++) {
		XrSwapchainImageAcquireInfo acquireInfo{ XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO };
		uint32_t swapchainImageIndex;
		CHECK_XRCMD(xrAcquireSwapchainImage(_xrSwapchains[i], &acquireInfo, &swapchainImageIndex));
		XrSwapchainImageWaitInfo waitInfo{ XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO };
		waitInfo.timeout = XR_INFINITE_DURATION;
		CHECK_XRCMD(xrWaitSwapchainImage(_xrSwapchains[i], &waitInfo));
		projectionLayerViews[i] = { XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW };
		projectionLayerViews[i].pose = _xrViews[i].pose;
		projectionLayerViews[i].fov = _xrViews[i].fov;
		projectionLayerViews[i].subImage.swapchain = _xrSwapchains[i];
		projectionLayerViews[i].subImage.imageRect.offset = { 0, 0 };
		projectionLayerViews[i].subImage.imageRect.extent = { (int)_xrRenderTargets[i].width, (int)_xrRenderTargets[i].height };
		CHECK(projectionLayerViews[i].subImage.imageArrayIndex == 0);
		xrRenderView(cmd, projectionLayerViews[i], i, swapchainImageIndex, mirrSwapchainImageIndex);
		
	}
	VK_CHECK(vkEndCommandBuffer(cmd));

	VkSubmitInfo submit = {};
	submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	submit.pNext = nullptr;
	VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	submit.pWaitDstStageMask = &waitStage;
	submit.waitSemaphoreCount = 1;
	submit.pWaitSemaphores = &get_current_frame()._presentSemaphore;
	submit.signalSemaphoreCount = 1;
	submit.pSignalSemaphores = &get_current_frame()._renderSemaphore;
	submit.commandBufferCount = 1;
	submit.pCommandBuffers = &cmd;

	//submit command buffer to the queue and execute it.
	// _renderFence will now block until the graphic commands finish execution
	VK_CHECK(vkQueueSubmit(_graphicsQueue, 1, &submit, get_current_frame()._renderFence));


	VkPresentInfoKHR presentInfo = {};
	presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
	presentInfo.pNext = nullptr;

	presentInfo.pSwapchains = &_mirrorSwapchain;
	presentInfo.swapchainCount = 1;

	presentInfo.pWaitSemaphores = &get_current_frame()._renderSemaphore;
	presentInfo.waitSemaphoreCount = 1;

	presentInfo.pImageIndices = &mirrSwapchainImageIndex;

	VK_CHECK(vkQueuePresentKHR(_graphicsQueue, &presentInfo));
	

	for (uint32_t i = 0; i < viewCountOutput; i++) {
		XrSwapchainImageReleaseInfo releaseInfo{ XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO };
		CHECK_XRCMD(xrReleaseSwapchainImage(_xrSwapchains[i], &releaseInfo));
	}
	layer.space = _xrSpace;
	layer.layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT | XR_COMPOSITION_LAYER_UNPREMULTIPLIED_ALPHA_BIT;
	layer.viewCount = (uint32_t)projectionLayerViews.size();
	layer.views = projectionLayerViews.data();
	return true;

}

void VulkanEngine::xrRenderView(VkCommandBuffer cmd, const XrCompositionLayerProjectionView& layerView, int viewNumber, int imageIndex, int mirrSwapchainImageIndex) {
	

	//make a clear-color from frame number. This will flash with a 120*pi frame period.
	VkClearValue clearValue;
	float flash = abs(sin(_frameNumber / 120.f));
	clearValue.color = { { flash, flash, flash, 1.0f } };

	//clear depth at 1
	VkClearValue depthClear;
	depthClear.depthStencil.depth = 1.f;
	VkExtent2D extent{ _xrRenderTargets[viewNumber].width, _xrRenderTargets[viewNumber].height };

	VkRenderPassBeginInfo rpInfo = vkinit::renderpass_begin_info(_xrRenderTargets[viewNumber].renderPass, extent, _xrRenderTargets[viewNumber].framebuffers[imageIndex]);



	//connect clear values
	rpInfo.clearValueCount = 2;
	VkClearValue clearValues[] = { clearValue, depthClear };

	rpInfo.pClearValues = &clearValues[0];


	vkCmdBeginRenderPass(cmd, &rpInfo, VK_SUBPASS_CONTENTS_INLINE);



	_vrPos.x = layerView.pose.position.x;
	_vrPos.y = layerView.pose.position.y;
	_vrPos.z = layerView.pose.position.z;
	_vrRot.x = layerView.pose.orientation.x;
	_vrRot.y = layerView.pose.orientation.y;
	_vrRot.z = layerView.pose.orientation.z;
	_vrRot.w = layerView.pose.orientation.w;
	_xrFovDetails = layerView.fov;

	_xrProjView = layerView;


	xrDraw_objects(cmd);

	if (viewNumber == 1) {
		//ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmd);
	}
	else {
		//ImGui::EndFrame();
		//ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmd);
	}


	vkCmdEndRenderPass(cmd);


	if (viewNumber == 1) {
		VkImageMemoryBarrier preCpyMemBarrier = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
		preCpyMemBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		preCpyMemBarrier.srcAccessMask = VK_ACCESS_MEMORY_READ_BIT;
		preCpyMemBarrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		//preCpyMemBarrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		preCpyMemBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
		preCpyMemBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		preCpyMemBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		preCpyMemBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		preCpyMemBarrier.subresourceRange.baseMipLevel = 0;
		preCpyMemBarrier.subresourceRange.levelCount = 1;
		preCpyMemBarrier.subresourceRange.baseArrayLayer = 0;
		preCpyMemBarrier.subresourceRange.layerCount = 1;
		preCpyMemBarrier.image = _xrRenderTargets[viewNumber].xrImages[imageIndex];
		vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 0, NULL, 1,
			&preCpyMemBarrier);



		preCpyMemBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		preCpyMemBarrier.srcAccessMask = VK_ACCESS_MEMORY_READ_BIT;
		preCpyMemBarrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		preCpyMemBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
		preCpyMemBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		preCpyMemBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		preCpyMemBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		preCpyMemBarrier.subresourceRange.baseMipLevel = 0;
		preCpyMemBarrier.subresourceRange.levelCount = 1;
		preCpyMemBarrier.subresourceRange.baseArrayLayer = 0;
		preCpyMemBarrier.subresourceRange.layerCount = 1;
		preCpyMemBarrier.image = _mirrorSwapchainImages[mirrSwapchainImageIndex];
		vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 0, NULL, 1,
			&preCpyMemBarrier);



		// Do a image copy to part of the dst image - checks should stay small
		//VkImageCopy cregion;
		//cregion.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		//cregion.srcSubresource.mipLevel = 0;
		//cregion.srcSubresource.baseArrayLayer = 0;
		//cregion.srcSubresource.layerCount = 1;
		//cregion.srcOffset.x = 0;
		//cregion.srcOffset.y = 0;
		//cregion.srcOffset.z = 0;
		//cregion.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		//cregion.dstSubresource.mipLevel = 0;
		//cregion.dstSubresource.baseArrayLayer = 0;
		//cregion.dstSubresource.layerCount = 1;
		//cregion.dstOffset.x = 0;
		//cregion.dstOffset.y = 0;
		//cregion.dstOffset.z = 0;
		//cregion.extent.width = _xrRenderTargets[0].width;
		//cregion.extent.height = _xrRenderTargets[0].height;
		//cregion.extent.depth = 1;

		//vkCmdCopyImage(cmd, _xrRenderTargets[viewNumber].xrImages[imageIndex], VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, _mirrorSwapchainImages[mirrSwapchainImageIndex], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
		//	1, &cregion);
		////vkCmdCopyImage(cmd, _xrRenderTargets[viewNumber].xrImages[imageIndex], VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, _mirrorSwapchainImages[mirrSwapchainImageIndex], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
		////		1, &cregion);


		VkImageBlit bregion;
		bregion.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		bregion.srcSubresource.baseArrayLayer = 0;
		bregion.srcSubresource.layerCount = 1;
		bregion.srcSubresource.mipLevel = 0;
		bregion.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		bregion.dstSubresource.baseArrayLayer = 0;
		bregion.dstSubresource.layerCount = 1;
		bregion.dstSubresource.mipLevel = 0;
		bregion.srcOffsets[0].x = 0;
		bregion.srcOffsets[0].y = 0;
		bregion.srcOffsets[0].z = 0;
		bregion.srcOffsets[1].x = _xrRenderTargets[0].width;
		bregion.srcOffsets[1].y = _xrRenderTargets[0].height;
		bregion.srcOffsets[1].z = 1;
		bregion.dstOffsets[0].x = 0;
		bregion.dstOffsets[0].y = 0;
		bregion.dstOffsets[0].z = 0;
		//bregion.dstOffsets[1].x = _xrRenderTargets[0].width;
		bregion.dstOffsets[1].x = _xrRenderTargets[0].width;// / 4;
		bregion.dstOffsets[1].y = _xrRenderTargets[0].height;// / 4;
		//bregion.dstOffsets[1].y = _xrRenderTargets[0].height;
		bregion.dstOffsets[1].z = 1;

		vkCmdBlitImage(cmd, _xrRenderTargets[viewNumber].xrImages[imageIndex], VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, _mirrorSwapchainImages[mirrSwapchainImageIndex], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			1, &bregion, VK_FILTER_NEAREST);


		VkImageMemoryBarrier postCopyMemBarrier = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
		postCopyMemBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		postCopyMemBarrier.srcAccessMask = VK_ACCESS_MEMORY_READ_BIT;
		postCopyMemBarrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		postCopyMemBarrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		postCopyMemBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		postCopyMemBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		postCopyMemBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		postCopyMemBarrier.subresourceRange.baseMipLevel = 0;
		postCopyMemBarrier.subresourceRange.levelCount = 1;
		postCopyMemBarrier.subresourceRange.baseArrayLayer = 0;
		postCopyMemBarrier.subresourceRange.layerCount = 1;
		postCopyMemBarrier.image = _xrRenderTargets[viewNumber].xrImages[imageIndex];
		vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 0, NULL, 1,
			&postCopyMemBarrier);

		VkImageMemoryBarrier prePresentBarrier = {};
		prePresentBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		prePresentBarrier.pNext = NULL;
		prePresentBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		prePresentBarrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
		prePresentBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
		prePresentBarrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
		prePresentBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		prePresentBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		prePresentBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		prePresentBarrier.subresourceRange.baseMipLevel = 0;
		prePresentBarrier.subresourceRange.levelCount = 1;
		prePresentBarrier.subresourceRange.baseArrayLayer = 0;
		prePresentBarrier.subresourceRange.layerCount = 1;
		prePresentBarrier.image = _mirrorSwapchainImages[mirrSwapchainImageIndex];
		vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, NULL, 0, NULL, 1,
			&prePresentBarrier);
	}
		




	




}


void VulkanEngine::draw()
{

	//FPS Count
	auto finish = std::chrono::high_resolution_clock::now();
	if (_frameNumber % 20 == 0) {
		if (!duration_cast<std::chrono::milliseconds>(finish - _previousTime).count() == 0) {
			float fps = 1000 / duration_cast<std::chrono::milliseconds>(finish - _previousTime).count();
			std::cout << "FPS: " << fps << "\n";
		}

	}
	_previousTime = finish;



	//wait until the GPU has finished rendering the last frame. Timeout of 1 second
	VK_CHECK(vkWaitForFences(_device, 1, &get_current_frame()._renderFence, true, 1000000000));
	VK_CHECK(vkResetFences(_device, 1, &get_current_frame()._renderFence));

	//request image from the swapchain, one second timeout
	uint32_t swapchainImageIndex;
	VK_CHECK(vkAcquireNextImageKHR(_device, _swapchain, 1000000000, get_current_frame()._presentSemaphore, nullptr, &swapchainImageIndex));

	//now that we are sure that the commands finished executing, we can safely reset the command buffer to begin recording again.
		//now that we are sure that the commands finished executing, we can safely reset the command buffer to begin recording again.
	VK_CHECK(vkResetCommandBuffer(get_current_frame()._mainCommandBuffer, 0));

	//naming it cmd for shorter writing
	VkCommandBuffer cmd = get_current_frame()._mainCommandBuffer;

	//begin the command buffer recording. We will use this command buffer exactly once, so we want to let Vulkan know that
	VkCommandBufferBeginInfo cmdBeginInfo = {};
	cmdBeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	cmdBeginInfo.pNext = nullptr;

	cmdBeginInfo.pInheritanceInfo = nullptr;
	cmdBeginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

	VK_CHECK(vkBeginCommandBuffer(cmd, &cmdBeginInfo));





	//make a clear-color from frame number. This will flash with a 120*pi frame period.
	VkClearValue clearValue;
	float flash = abs(sin(_frameNumber / 120.f));
	clearValue.color = { { 0.0f, 0.0f, flash, 1.0f } };

	//clear depth at 1
	VkClearValue depthClear;
	depthClear.depthStencil.depth = 1.f;

	VkRenderPassBeginInfo rpInfo = vkinit::renderpass_begin_info(_renderPass, _windowExtent, _framebuffers[swapchainImageIndex]);



	//connect clear values
	rpInfo.clearValueCount = 2;
	VkClearValue clearValues[] = { clearValue, depthClear };

	rpInfo.pClearValues = &clearValues[0];


	vkCmdBeginRenderPass(cmd, &rpInfo, VK_SUBPASS_CONTENTS_INLINE);

	draw_objects(cmd, _renderables.data(), _renderables.size());
	
	//finalize the render pass
	vkCmdEndRenderPass(cmd);
	//finalize the command buffer (we can no longer add commands, but it can now be executed)
	VK_CHECK(vkEndCommandBuffer(cmd));




	//prepare the submission to the queue.
//we want to wait on the _presentSemaphore, as that semaphore is signaled when the swapchain is ready
//we will signal the _renderSemaphore, to signal that rendering has finished

	VkSubmitInfo submit = {};
	submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	submit.pNext = nullptr;

	VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;

	submit.pWaitDstStageMask = &waitStage;

	submit.waitSemaphoreCount = 1;
	submit.pWaitSemaphores = &get_current_frame()._presentSemaphore;

	submit.signalSemaphoreCount = 1;
	submit.pSignalSemaphores = &get_current_frame()._renderSemaphore;

	submit.commandBufferCount = 1;
	submit.pCommandBuffers = &cmd;

	//submit command buffer to the queue and execute it.
	// _renderFence will now block until the graphic commands finish execution
	VK_CHECK(vkQueueSubmit(_graphicsQueue, 1, &submit, get_current_frame()._renderFence));






	// this will put the image we just rendered into the visible window.
// we want to wait on the _renderSemaphore for that,
// as it's necessary that drawing commands have finished before the image is displayed to the user
	VkPresentInfoKHR presentInfo = {};
	presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
	presentInfo.pNext = nullptr;

	presentInfo.pSwapchains = &_swapchain;
	presentInfo.swapchainCount = 1;

	presentInfo.pWaitSemaphores = &get_current_frame()._renderSemaphore;
	presentInfo.waitSemaphoreCount = 1;

	presentInfo.pImageIndices = &swapchainImageIndex;

	VK_CHECK(vkQueuePresentKHR(_graphicsQueue, &presentInfo));

	//increase the number of frames drawn
	_frameNumber++;
}





void VulkanEngine::xrRun()
{
	SDL_Event e;
	bQuit = false;
	//ImGui::SetCurrentContext(_imguictx);
	//main loop
	while (!bQuit)
	{
		xrPollEvents();
		xrPollActions();
		//Handle events on queue
		while (SDL_PollEvent(&e) != 0)
		{
			//ImGui_ImplSDL2_ProcessEvent(&e);
			if (e.type == SDL_QUIT)
			{
				bQuit = true;
			}
			else if (e.type == SDL_KEYDOWN)
			{


			}
			else if (e.type == SDL_MOUSEMOTION) {
				pitch += glm::radians((float)e.motion.yrel) * 5;
				yaw += glm::radians((float)e.motion.xrel) * 5;

			}
		}
		glm::mat4 inverted;
		const Uint8* keystate = SDL_GetKeyboardState(NULL);
		if (keystate[SDL_SCANCODE_W]) {
			inverted = glm::inverse(cameraRotationTransform);
			glm::vec3 forward = normalize(glm::vec3(inverted[2]));
			_tgtPos += forward * 0.22f;
		}if (keystate[SDL_SCANCODE_A]) {
			inverted = glm::inverse(cameraRotationTransform);
			glm::vec3 left = normalize(glm::vec3(inverted[0]));
			_tgtPos += left * 0.22f;
		}if (keystate[SDL_SCANCODE_S]) {
			inverted = glm::inverse(cameraRotationTransform);
			glm::vec3 back = -normalize(glm::vec3(inverted[2]));
			_tgtPos += back * 0.22f;
		}if (keystate[SDL_SCANCODE_D]) {
			inverted = glm::inverse(cameraRotationTransform);
			glm::vec3 right = -normalize(glm::vec3(inverted[0]));
			_tgtPos += right * 0.22f;
		}


		//imgui new frame
		
		//ImGui_ImplVulkan_NewFrame();
		//ImGui_ImplSDL2_NewFrame(_window);

		//ImGui::NewFrame();


		//imgui commands
		///ImGui::ShowDemoWindow();


		//_camPos = 0.125f * _tgtPos + 0.875f * _camPos;
		if (_xrSessionRunning) {
			xrDraw();
		}
		else {
			//ImGui::EndFrame();
		}

	}

}



void VulkanEngine::run()
{
	SDL_Event e;
	bool bQuit = false;

	//main loop
	while (!bQuit)
	{

		//Handle events on queue
		while (SDL_PollEvent(&e) != 0)
		{
			if (e.type == SDL_QUIT)
			{
				bQuit = true;
			}
			else if (e.type == SDL_KEYDOWN)
			{


			}
			else if (e.type == SDL_MOUSEMOTION) {
				pitch += glm::radians((float)e.motion.yrel) * 5;
				yaw += glm::radians((float)e.motion.xrel) * 5;

			}
		}
		glm::mat4 inverted;
		const Uint8* keystate = SDL_GetKeyboardState(NULL);
		if (keystate[SDL_SCANCODE_W]) {
			inverted = glm::inverse(cameraRotationTransform);
			glm::vec3 forward = normalize(glm::vec3(inverted[2]));
			_tgtPos += forward * 0.22f;
		}if (keystate[SDL_SCANCODE_A]) {
			inverted = glm::inverse(cameraRotationTransform);
			glm::vec3 left = normalize(glm::vec3(inverted[0]));
			_tgtPos += left * 0.22f;
		}if (keystate[SDL_SCANCODE_S]) {
			inverted = glm::inverse(cameraRotationTransform);
			glm::vec3 back = -normalize(glm::vec3(inverted[2]));
			_tgtPos += back * 0.22f;
		}if (keystate[SDL_SCANCODE_D]) {
			inverted = glm::inverse(cameraRotationTransform);
			glm::vec3 right = -normalize(glm::vec3(inverted[0]));
			_tgtPos += right * 0.22f;
		}


		_camPos = 0.125f * _tgtPos + 0.875f * _camPos;
		draw();
	}

}


void VulkanEngine::xrPollEvents() {

	XrEventDataBaseHeader* baseHeader = reinterpret_cast<XrEventDataBaseHeader*>(&_xrEventDataBuffer);
	*baseHeader = { XR_TYPE_EVENT_DATA_BUFFER };
	const XrResult xr = xrPollEvent(_xrInstance, &_xrEventDataBuffer);
	if (xr == XR_EVENT_UNAVAILABLE) {
		return;
	}
	if (xr == XR_SUCCESS) {
		if (baseHeader->type == XR_TYPE_EVENT_DATA_EVENTS_LOST) {
			const XrEventDataEventsLost* const eventsLost = reinterpret_cast<const XrEventDataEventsLost*>(baseHeader);
			//spdlog::warn("{} events lost", eventsLost);
		}
	}
	else {
		THROW_XR(xr, "xrPollEvent");
		return;
	}
	switch (baseHeader->type) {
	case XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING: {
		const auto& instanceLossPending = *reinterpret_cast<const XrEventDataInstanceLossPending*>(baseHeader);
		//spdlog::warn("XrEventDataInstanceLossPending by {}", instanceLossPending.lossTime);
		bQuit = true;
		return;
	}
	case XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED: {
		auto sessionStateChangedEvent = *reinterpret_cast<const XrEventDataSessionStateChanged*>(baseHeader);
		xrHandleStateChange(sessionStateChangedEvent);
		break;
	}
	case XR_TYPE_EVENT_DATA_INTERACTION_PROFILE_CHANGED:

		break;
	case XR_TYPE_EVENT_DATA_REFERENCE_SPACE_CHANGE_PENDING:
	default: {
		//spdlog::info("Ignoring event type %d", baseHeader->type);
		break;
	}
	}

}

void VulkanEngine::xrHandleStateChange(const XrEventDataSessionStateChanged& stateChangedEvent) {
	const XrSessionState oldState = _xrSessionState;
	_xrSessionState = stateChangedEvent.state;

	//spdlog::info("XrEventDataSessionStateChanged: state %s->%s session=%lld time=%lld", to_string(oldState),
//		to_string(_xrSessionState), stateChangedEvent.session, stateChangedEvent.time);

	if ((stateChangedEvent.session != XR_NULL_HANDLE) && (stateChangedEvent.session != _xrSession)) {
		//spdlog::error( "XrEventDataSessionStateChanged for unknown session");
		return;
	}
	switch (_xrSessionState) {
	case XR_SESSION_STATE_READY: {
		CHECK(_xrSession != XR_NULL_HANDLE);
		XrSessionBeginInfo sessionBeginInfo{ XR_TYPE_SESSION_BEGIN_INFO };
		sessionBeginInfo.primaryViewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
		CHECK_XRCMD(xrBeginSession(_xrSession, &sessionBeginInfo));
		_xrSessionRunning = true;
		break;
	}
	case XR_SESSION_STATE_STOPPING: {
		CHECK(_xrSession != XR_NULL_HANDLE);
		_xrSessionRunning = false;
		CHECK_XRCMD(xrEndSession(_xrSession))
			break;
	}
	case XR_SESSION_STATE_EXITING: {
		bQuit = true;
		_xrSessionRunning = false;
		break;
	}
	case XR_SESSION_STATE_LOSS_PENDING: {
		bQuit = true;
		_xrSessionRunning = false;
		break;
	}
	default:
		break;
	}
}


bool VulkanEngine::load_shader_module(const char* filePath, VkShaderModule* outShaderModule)
{
	//open the file. With cursor at the end
	std::ifstream file(filePath, std::ios::ate | std::ios::binary);

	if (!file.is_open()) {
		return false;
	}

	//find what the size of the file is by looking up the location of the cursor
//because the cursor is at the end, it gives the size directly in bytes
	size_t fileSize = (size_t)file.tellg();

	//spirv expects the buffer to be on uint32, so make sure to reserve an int vector big enough for the entire file
	std::vector<uint32_t> buffer(fileSize / sizeof(uint32_t));

	//put file cursor at beginning
	file.seekg(0);

	//load the entire file into the buffer
	file.read((char*)buffer.data(), fileSize);

	//now that the file is loaded into the buffer, we can close it
	file.close();


	//create a new shader module, using the buffer we loaded
	VkShaderModuleCreateInfo createInfo = {};
	createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
	createInfo.pNext = nullptr;

	//codeSize has to be in bytes, so multiply the ints in the buffer by size of int to know the real size of the buffer
	createInfo.codeSize = buffer.size() * sizeof(uint32_t);
	createInfo.pCode = buffer.data();

	//check that the creation goes well.
	VkShaderModule shaderModule;
	VkResult ret = vkCreateShaderModule(_device, &createInfo, nullptr, &shaderModule);

	if ( ret != VK_SUCCESS) {
		return false;
	}
	*outShaderModule = shaderModule;
	return true;

}

void VulkanEngine::init_pipelines() {
	//build the pipeline layout that controls the inputs/outputs of the shader
//we are not using descriptor sets or other systems yet, so no need to use anything other than empty default
	VkPipelineLayoutCreateInfo pipeline_layout_info = vkinit::pipeline_layout_create_info();

	//build the stage-create-info for both vertex and fragment stages. This lets the pipeline know the shader modules per stage
	PipelineBuilder pipelineBuilder;

	//vertex input controls how to read vertices from vertex buffers. We aren't using it yet
	pipelineBuilder._vertexInputInfo = vkinit::vertex_input_state_create_info();

	//input assembly is the configuration for drawing triangle lists, strips, or individual points.
	//we are just going to draw triangle list
	pipelineBuilder._inputAssembly = vkinit::input_assembly_create_info(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);

	//build viewport and scissor from the swapchain extents
	pipelineBuilder._viewport.x = 0.0f;
	pipelineBuilder._viewport.y = 0.0f;
	pipelineBuilder._viewport.width = (float)_windowExtent.width;
	pipelineBuilder._viewport.height = (float)_windowExtent.height;
	pipelineBuilder._viewport.minDepth = 0.0f;
	pipelineBuilder._viewport.maxDepth = 1.0f;

	pipelineBuilder._scissor.offset = { 0, 0 };
	pipelineBuilder._scissor.extent = _windowExtent;

	//configure the rasterizer to draw filled triangles
	pipelineBuilder._rasterizer = vkinit::rasterization_state_create_info(VK_POLYGON_MODE_FILL);

	//we don't use multisampling, so just run the default one
	pipelineBuilder._multisampling = vkinit::multisampling_state_create_info();

	//a single blend attachment with no blending and writing to RGBA
	pipelineBuilder._colorBlendAttachment = vkinit::color_blend_attachment_state();


	//default depthtesting
	pipelineBuilder._depthStencil = vkinit::depth_stencil_create_info(true, true, VK_COMPARE_OP_LESS_OR_EQUAL);

	//build the mesh pipeline

	VertexInputDescription vertexDescription = Vertex::get_vertex_description();

	//connect the pipeline builder vertex input info to the one we get from Vertex
	pipelineBuilder._vertexInputInfo.pVertexAttributeDescriptions = vertexDescription.attributes.data();
	pipelineBuilder._vertexInputInfo.vertexAttributeDescriptionCount = vertexDescription.attributes.size();

	pipelineBuilder._vertexInputInfo.pVertexBindingDescriptions = vertexDescription.bindings.data();
	pipelineBuilder._vertexInputInfo.vertexBindingDescriptionCount = vertexDescription.bindings.size();

	//clear the shader stages for the builder
	pipelineBuilder._shaderStages.clear();

	//compile mesh vertex shader


	VkShaderModule meshVertShader;
	if (!load_shader_module("../../../../shaders/tri_mesh.vert.spv", &meshVertShader))
	{
		std::cout << "Error when building the triangle mesh vertex shader module" << std::endl;
	}
	else {
		std::cout << "Triangle mesh vertex shader successfully loaded" << std::endl;
	}


	VkShaderModule triangleFragShader;
	if (!load_shader_module("../../../../shaders/default_lit.frag.spv", &triangleFragShader))
	{
		std::cout << "Error when building the triangle mesh vertex shader module" << std::endl;
	}
	else {
		std::cout << "Triangle mesh vertex shader successfully loaded" << std::endl;
	}


	//add the other shaders
	pipelineBuilder._shaderStages.push_back(
		vkinit::pipeline_shader_stage_create_info(VK_SHADER_STAGE_VERTEX_BIT, meshVertShader));

	//make sure that triangleFragShader is holding the compiled colored_triangle.frag
	pipelineBuilder._shaderStages.push_back(
		vkinit::pipeline_shader_stage_create_info(VK_SHADER_STAGE_FRAGMENT_BIT, triangleFragShader));


	//Mesh Pipeline Layout
	// 
		//we start from just the default empty pipeline layout info
	VkPipelineLayoutCreateInfo mesh_pipeline_layout_info = vkinit::pipeline_layout_create_info();

	//setup push constants
	VkPushConstantRange push_constant;
	//this push constant range starts at the beginning
	push_constant.offset = 0;
	//this push constant range takes up the size of a MeshPushConstants struct
	push_constant.size = sizeof(MeshPushConstants);
	//this push constant range is accessible only in the vertex shader
	push_constant.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

	mesh_pipeline_layout_info.pPushConstantRanges = &push_constant;
	mesh_pipeline_layout_info.pushConstantRangeCount = 1;

	VK_CHECK(vkCreatePipelineLayout(_device, &mesh_pipeline_layout_info, nullptr, &_meshPipelineLayout));

	pipelineBuilder._pipelineLayout = _meshPipelineLayout;

	_meshPipeline = pipelineBuilder.build_pipeline(_device, _renderPass);


	create_material(_meshPipeline, _meshPipelineLayout, "defaultmesh");

	//destroy all shader modules, outside of the queue
	vkDestroyShaderModule(_device, meshVertShader, nullptr);

	vkDestroyShaderModule(_device, triangleFragShader, nullptr);


	_mainDeletionQueue.push_function([=]() {
		//destroy the 2 pipelines we have created
		vkDestroyPipeline(_device, _meshPipeline, nullptr);
		//destroy the pipeline layout that they use		
		vkDestroyPipelineLayout(_device, _meshPipelineLayout, nullptr);
		});

}


AllocatedBuffer VulkanEngine::create_buffer(size_t allocSize, VkBufferUsageFlags usage, VmaMemoryUsage memoryUsage)
{
	//allocate vertex buffer
	VkBufferCreateInfo bufferInfo = {};
	bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	bufferInfo.pNext = nullptr;

	bufferInfo.size = allocSize;
	bufferInfo.usage = usage;


	VmaAllocationCreateInfo vmaallocInfo = {};
	vmaallocInfo.usage = memoryUsage;

	AllocatedBuffer newBuffer;

	//allocate the bufferxrFrameData
	VK_CHECK(vmaCreateBuffer(_allocator, &bufferInfo, &vmaallocInfo,
		&newBuffer._buffer,
		&newBuffer._allocation,
		nullptr));

	return newBuffer;
}


void VulkanEngine::init_miscPipelines() {
	//build the pipeline layout that controls the inputs/outputs of the shader
//we are not using descriptor sets or other systems yet, so no need to use anything other than empty default
	VkPipelineLayoutCreateInfo pipeline_layout_info = vkinit::pipeline_layout_create_info();

	//build the stage-create-info for both vertex and fragment stages. This lets the pipeline know the shader modules per stage
	PipelineBuilder pipelineBuilder;

	//vertex input controls how to read vertices from vertex buffers. We aren't using it yet
	pipelineBuilder._vertexInputInfo = vkinit::vertex_input_state_create_info();

	//input assembly is the configuration for drawing triangle lists, strips, or individual points.
	//we are just going to draw triangle list
	pipelineBuilder._inputAssembly = vkinit::input_assembly_create_info(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);

	//build viewport and scissor from the swapchain extents
	pipelineBuilder._viewport.x = 0.0f;
	pipelineBuilder._viewport.y = 0.0f;
	pipelineBuilder._viewport.width = (float)shadowMapResolution.width;
	pipelineBuilder._viewport.height = (float)shadowMapResolution.height;
	pipelineBuilder._viewport.minDepth = 0.0f;
	pipelineBuilder._viewport.maxDepth = 1.0f;

	pipelineBuilder._scissor.offset = { 0, 0 };
	VkExtent2D extent{ shadowMapResolution.width , shadowMapResolution.height };

	pipelineBuilder._scissor.extent = extent;

	//configure the rasterizer to draw filled triangles
	pipelineBuilder._rasterizer = vkinit::rasterization_state_create_info(VK_POLYGON_MODE_FILL);

	//we don't use multisampling, so just run the default one
	pipelineBuilder._multisampling = vkinit::multisampling_state_create_info();

	//a single blend attachment with no blending and writing to RGBA
	pipelineBuilder._colorBlendAttachment = vkinit::color_blend_attachment_state(); //TODO: Get rid of colour



	//default depthtesting
	pipelineBuilder._depthStencil = vkinit::depth_stencil_create_info(true, true, VK_COMPARE_OP_LESS_OR_EQUAL);



	//build the mesh pipeline

	VertexInputDescription vertexDescription = Vertex::get_vertex_description();

	//connect the pipeline builder vertex input info to the one we get from Vertex
	pipelineBuilder._vertexInputInfo.pVertexAttributeDescriptions = vertexDescription.attributes.data();
	pipelineBuilder._vertexInputInfo.vertexAttributeDescriptionCount = vertexDescription.attributes.size();

	pipelineBuilder._vertexInputInfo.pVertexBindingDescriptions = vertexDescription.bindings.data();
	pipelineBuilder._vertexInputInfo.vertexBindingDescriptionCount = vertexDescription.bindings.size();

	//clear the shader stages for the builder
	pipelineBuilder._shaderStages.clear();

	//compile mesh vertex shader

	VkShaderModule shadowFragShader;
	if (!load_shader_module("../../../../shaders/shadow.frag.spv", &shadowFragShader))
	{
		std::cout << "Error when building the shadow map fragment shader" << std::endl;
	}
	else {
		std::cout << "shadow map fragment shader successfully loaded" << std::endl;
	}


	VkShaderModule shadowVertShader;
	if (!load_shader_module("../../../../shaders/shadow.vert.spv", &shadowVertShader))
	{
		std::cout << "Error when building the shadow map vertex shader module" << std::endl;
	}
	else {
		std::cout << "shadow map vertex shader successfully loaded" << std::endl;
	}




	//add the other shaders
	pipelineBuilder._shaderStages.push_back(
		vkinit::pipeline_shader_stage_create_info(VK_SHADER_STAGE_VERTEX_BIT, shadowVertShader));

	//make sure that triangleFragShader is holding the compiled colored_triangle.frag
	pipelineBuilder._shaderStages.push_back(
		vkinit::pipeline_shader_stage_create_info(VK_SHADER_STAGE_FRAGMENT_BIT, shadowFragShader));


	//Mesh Pipeline Layout
	// 
		//we start from just the default empty pipeline layout info
	VkPipelineLayoutCreateInfo shadow_pipeline_layout_info = vkinit::pipeline_layout_create_info();

	//setup push constants
	VkPushConstantRange push_constant;
	//this push constant range starts at the beginning
	push_constant.offset = 0;
	//this push constant range takes up the size of a MeshPushConstants struct
	push_constant.size = sizeof(ShadowPushConstants);
	//this push constant range is accessible only in the vertex shader
	push_constant.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

	shadow_pipeline_layout_info.pPushConstantRanges = &push_constant;
	shadow_pipeline_layout_info.pushConstantRangeCount = 1;

	VkDescriptorSetLayout texturedSetLayouts[] = { _globalSetLayout, _objectSetLayout,_singleTextureSetLayout };

	shadow_pipeline_layout_info.setLayoutCount = 3;
	shadow_pipeline_layout_info.pSetLayouts = texturedSetLayouts;



	VK_CHECK(vkCreatePipelineLayout(_device, &shadow_pipeline_layout_info, nullptr, &shadowPassData.pipelineLayout));

	pipelineBuilder._pipelineLayout = shadowPassData.pipelineLayout;



	shadowPassData.pl = pipelineBuilder.build_pipeline(_device, shadowPassData.rp);


	create_material(shadowPassData.pl, shadowPassData.pipelineLayout, "shadowMat");


	//destroy all shader modules, outside of the queue
	vkDestroyShaderModule(_device, shadowFragShader, nullptr);
	vkDestroyShaderModule(_device, shadowVertShader, nullptr);




	_mainDeletionQueue.push_function([=]() {
		//destroy the 2 pipelines we have created
		vkDestroyPipeline(_device, shadowPassData.pl, nullptr);
		//destroy the pipeline layout that they use		
		vkDestroyPipelineLayout(_device, shadowPassData.pipelineLayout, nullptr);
		});
}


void VulkanEngine::init_xrPipelines() {
	//build the pipeline layout that controls the inputs/outputs of the shader
//we are not using descriptor sets or other systems yet, so no need to use anything other than empty default
	VkPipelineLayoutCreateInfo pipeline_layout_info = vkinit::pipeline_layout_create_info();

	//build the stage-create-info for both vertex and fragment stages. This lets the pipeline know the shader modules per stage
	PipelineBuilder pipelineBuilder;

	//vertex input controls how to read vertices from vertex buffers. We aren't using it yet
	pipelineBuilder._vertexInputInfo = vkinit::vertex_input_state_create_info();

	//input assembly is the configuration for drawing triangle lists, strips, or individual points.
	//we are just going to draw triangle list
	pipelineBuilder._inputAssembly = vkinit::input_assembly_create_info(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);

	//build viewport and scissor from the swapchain extents
	pipelineBuilder._viewport.x = 0.0f;
	pipelineBuilder._viewport.y = 0.0f;
	pipelineBuilder._viewport.width = (float)_xrRenderTargets[0].width;
	pipelineBuilder._viewport.height = (float)_xrRenderTargets[0].height;
	pipelineBuilder._viewport.minDepth = 0.0f;
	pipelineBuilder._viewport.maxDepth = 1.0f;

	pipelineBuilder._scissor.offset = { 0, 0 };
	VkExtent2D extent{ _xrRenderTargets[0].width , _xrRenderTargets[0].height };

	pipelineBuilder._scissor.extent = extent;

	//configure the rasterizer to draw filled triangles
	pipelineBuilder._rasterizer = vkinit::rasterization_state_create_info(VK_POLYGON_MODE_FILL);

	//we don't use multisampling, so just run the default one
	pipelineBuilder._multisampling = vkinit::multisampling_state_create_info();

	//a single blend attachment with no blending and writing to RGBA
	pipelineBuilder._colorBlendAttachment = vkinit::color_blend_attachment_state();



	//default depthtesting
	pipelineBuilder._depthStencil = vkinit::depth_stencil_create_info(true, true, VK_COMPARE_OP_LESS_OR_EQUAL);

	//build the mesh pipeline

	VertexInputDescription vertexDescription = Vertex::get_vertex_description();

	//connect the pipeline builder vertex input info to the one we get from Vertex
	pipelineBuilder._vertexInputInfo.pVertexAttributeDescriptions = vertexDescription.attributes.data();
	pipelineBuilder._vertexInputInfo.vertexAttributeDescriptionCount = vertexDescription.attributes.size();

	pipelineBuilder._vertexInputInfo.pVertexBindingDescriptions = vertexDescription.bindings.data();
	pipelineBuilder._vertexInputInfo.vertexBindingDescriptionCount = vertexDescription.bindings.size();

	//clear the shader stages for the builder
	pipelineBuilder._shaderStages.clear();

	//compile mesh vertex shader

	VkShaderModule texturedMeshShader;
	if (!load_shader_module("../../../../shaders/textured_lit.frag.spv", &texturedMeshShader))
	{
		std::cout << "Error when building the textured mesh fragment shader" << std::endl;
	}
	else {
		std::cout << "Textured mesh fragment shader successfully loaded" << std::endl;
	}


	VkShaderModule meshVertShader;
	if (!load_shader_module("../../../../shaders/tri_mesh.vert.spv", &meshVertShader))
	{
		std::cout << "Error when building the triangle mesh vertex shader module" << std::endl;
	}
	else {
		std::cout << "Triangle mesh vertex shader successfully loaded" << std::endl;
	}


	VkShaderModule triangleFragShader;
	if (!load_shader_module("../../../../shaders/default_lit.frag.spv", &triangleFragShader))
	{
		std::cout << "Error when building the default lit frag shader module" << std::endl;
	}
	else {
		std::cout << "default lit frag shadersuccessfully loaded" << std::endl;
	}

	VkShaderModule animVertShader;
	if (!load_shader_module("../../../../shaders/anim.vert.spv", &animVertShader))
	{
		std::cout << "Error when building the default anim vert shader module" << std::endl;
	}
	else {
		std::cout << "default anim vertshadersuccessfully loaded" << std::endl;
	}


	//add the other shaders
	pipelineBuilder._shaderStages.push_back(
		vkinit::pipeline_shader_stage_create_info(VK_SHADER_STAGE_VERTEX_BIT, meshVertShader));

	//make sure that triangleFragShader is holding the compiled colored_triangle.frag
	pipelineBuilder._shaderStages.push_back(
		vkinit::pipeline_shader_stage_create_info(VK_SHADER_STAGE_FRAGMENT_BIT, triangleFragShader));


	//Mesh Pipeline Layout
	// 
		//we start from just the default empty pipeline layout info
	VkPipelineLayoutCreateInfo mesh_pipeline_layout_info = vkinit::pipeline_layout_create_info();

	//setup push constants
	VkPushConstantRange push_constant;
	//this push constant range starts at the beginning
	push_constant.offset = 0;
	//this push constant range takes up the size of a MeshPushConstants struct
	push_constant.size = sizeof(MeshPushConstants);
	//this push constant range is accessible only in the vertex shader
	push_constant.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

	mesh_pipeline_layout_info.pPushConstantRanges = &push_constant;
	mesh_pipeline_layout_info.pushConstantRangeCount = 1;

	VkDescriptorSetLayout setLayouts[] = { _globalSetLayout, _objectSetLayout };
	//hook the global set layout
	mesh_pipeline_layout_info.setLayoutCount = 2;
	mesh_pipeline_layout_info.pSetLayouts = setLayouts;




	VK_CHECK(vkCreatePipelineLayout(_device, &mesh_pipeline_layout_info, nullptr, &_meshPipelineLayout));

	pipelineBuilder._pipelineLayout = _meshPipelineLayout;



	_meshPipeline = pipelineBuilder.build_pipeline(_device, _xrRenderTargets[0].renderPass);


	create_material(_meshPipeline, _meshPipelineLayout, "defaultmesh");


	//create pipeline layout for the textured mesh, which has 3 descriptor sets
	//we start from  the normal mesh layout
	VkPipelineLayoutCreateInfo textured_pipeline_layout_info = mesh_pipeline_layout_info;

	VkDescriptorSetLayout texturedSetLayouts[] = { _globalSetLayout, _objectSetLayout,_singleTextureSetLayout, shadowTextureSetLayout };

	textured_pipeline_layout_info.setLayoutCount = 4;
	textured_pipeline_layout_info.pSetLayouts = texturedSetLayouts;

	VkPipelineLayout texturedPipeLayout;
	VK_CHECK(vkCreatePipelineLayout(_device, &textured_pipeline_layout_info, nullptr, &texturedPipeLayout));

	pipelineBuilder._shaderStages.clear();
	pipelineBuilder._shaderStages.push_back(
		vkinit::pipeline_shader_stage_create_info(VK_SHADER_STAGE_VERTEX_BIT, meshVertShader));

	pipelineBuilder._shaderStages.push_back(
		vkinit::pipeline_shader_stage_create_info(VK_SHADER_STAGE_FRAGMENT_BIT, texturedMeshShader));

	//connect the new pipeline layout to the pipeline builder
	//connect the new pipeline layout to the pipeline bui2lder
	pipelineBuilder._pipelineLayout = texturedPipeLayout;
	VkPipeline texPipeline = pipelineBuilder.build_pipeline(_device, _xrRenderTargets[0].renderPass);
	create_material(texPipeline, texturedPipeLayout, "texturedmesh");





	//create pipeline layout for the textured mesh, which has 3 descriptor sets
	//we start from  the normal mesh layout
	VkPipelineLayoutCreateInfo animated_pipeline_layout_info = mesh_pipeline_layout_info;
	VkDescriptorSetLayout animSetLayouts[] = { _globalSetLayout, _objectSetLayout,_singleTextureSetLayout, shadowTextureSetLayout, boneTransformSetLayout };

	animated_pipeline_layout_info.setLayoutCount = 5;
	animated_pipeline_layout_info.pSetLayouts = animSetLayouts;

	vertexDescription = AnimatedVertex::get_vertex_description();

	//connect the pipeline builder vertex input info to the one we get from Vertex
	pipelineBuilder._vertexInputInfo.pVertexAttributeDescriptions = vertexDescription.attributes.data();
	pipelineBuilder._vertexInputInfo.vertexAttributeDescriptionCount = vertexDescription.attributes.size();

	pipelineBuilder._vertexInputInfo.pVertexBindingDescriptions = vertexDescription.bindings.data();
	pipelineBuilder._vertexInputInfo.vertexBindingDescriptionCount = vertexDescription.bindings.size();

	VkPipelineLayout animPipeLayout;
	spdlog::debug("building anim pipeline layout");
	VK_CHECK(vkCreatePipelineLayout(_device, &animated_pipeline_layout_info, nullptr, &animPipeLayout));

	pipelineBuilder._shaderStages.clear();
	pipelineBuilder._shaderStages.push_back(
		vkinit::pipeline_shader_stage_create_info(VK_SHADER_STAGE_VERTEX_BIT, animVertShader));

	pipelineBuilder._shaderStages.push_back(
		vkinit::pipeline_shader_stage_create_info(VK_SHADER_STAGE_FRAGMENT_BIT, texturedMeshShader));

	//connect the new pipeline layout to the pipeline builder
	//connect the new pipeline layout to the pipeline bui2lder
	pipelineBuilder._pipelineLayout = animPipeLayout;
	spdlog::debug("building anim pipeline");
	VkPipeline animPipeline = pipelineBuilder.build_pipeline(_device, _xrRenderTargets[0].renderPass);
	create_material(animPipeline, animPipeLayout, "animMesh");



	//destroy all shader modules, outside of the queue
	vkDestroyShaderModule(_device, meshVertShader, nullptr);
	vkDestroyShaderModule(_device, texturedMeshShader, nullptr);
	vkDestroyShaderModule(_device, triangleFragShader, nullptr);
	vkDestroyShaderModule(_device, animVertShader, nullptr);



	_mainDeletionQueue.push_function([=]() {
		//destroy the 2 pipelines we have created
		vkDestroyPipeline(_device, _meshPipeline, nullptr);
		//destroy the pipeline layout that they use		
		vkDestroyPipelineLayout(_device, texturedPipeLayout, nullptr);
		vkDestroyPipelineLayout(_device, _meshPipelineLayout, nullptr);

		vkDestroyPipeline(_device, texPipeline, nullptr);

		});

}







Material* VulkanEngine::create_material(VkPipeline pipeline, VkPipelineLayout layout, const std::string& name)
{
	Material mat;
	mat.pipeline = pipeline;
	mat.pipelineLayout = layout;
	_materials[name] = mat;
	return &_materials[name];
}

Material* VulkanEngine::get_material(const std::string& name)
{
	//search for the object, and return nullptr if not found
	auto it = _materials.find(name);
	if (it == _materials.end()) {
		return nullptr;
	}
	else {
		return &(*it).second;
	}
}


Mesh* VulkanEngine::get_mesh(const std::string& name)
{
	auto it = _meshes.find(name);
	if (it == _meshes.end()) {
		return nullptr;
	}
	else {
		return &(*it).second;
	}
}

VulkanEngine::VulkanEngine() {

}

void VulkanEngine::draw_objects(VkCommandBuffer cmd, RenderObject* first, int count)
{
	XrMatrix4x4f proj;
	XrMatrix4x4f_CreateProjectionFov(&proj, GRAPHICS_VULKAN, _xrProjView.fov, 0.05f, 100.0f);
	glm::mat4 projection = Converter::xrMat4x4_to_glmMat4x4(proj);
	XrMatrix4x4f toView, viewInv;
	XrVector3f scale{ 1.f, 1.f, 1.f };
	XrMatrix4x4f_CreateTranslationRotationScale(&toView, &_xrProjView.pose.position, &_xrProjView.pose.orientation, &scale);
	XrMatrix4x4f_InvertRigidBody(&viewInv, &toView);
	glm::mat4 view = Converter::xrMat4x4_to_glmMat4x4(viewInv);
	view = glm::translate(view, _camPos);



	//fill a GPU camera data struct
	GPUCameraData camData;
	camData.proj = projection;
	camData.view = view;
	camData.viewproj = view * projection;

	//and copy it to the buffer
	void* data;
	vmaMapMemory(_allocator, get_current_frame().cameraBuffer._allocation, &data);
	vmaMapMemory(_allocator, get_current_frame().cameraBuffer._allocation, &data);

	memcpy(data, &camData, sizeof(GPUCameraData));

	vmaUnmapMemory(_allocator, get_current_frame().cameraBuffer._allocation);

	float framed = (_frameNumber / 120.f);

	_sceneParameters.ambientColor = { sin(framed),0,cos(framed),1 };

	char* sceneData;
	vmaMapMemory(_allocator, _sceneParameterBuffer._allocation, (void**)&sceneData);

	int frameIndex = _frameNumber % FRAME_OVERLAP;

	sceneData += pad_uniform_buffer_size(sizeof(GPUSceneData)) * frameIndex;

	memcpy(sceneData, &_sceneParameters, sizeof(GPUSceneData));

	vmaUnmapMemory(_allocator, _sceneParameterBuffer._allocation);

	Mesh* lastMesh = nullptr;
	Material* lastMaterial = nullptr;
	for (int i = 0; i < count; i++)
	{


		RenderObject& object = first[i];

		//only bind the pipeline if it doesn't match with the already bound one
		if (object.material != lastMaterial) {

			vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, object.material->pipeline);
			lastMaterial = object.material;
			//offset for our scene buffer
			uint32_t uniform_offset = pad_uniform_buffer_size(sizeof(GPUSceneData)) * frameIndex;

			vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, object.material->pipelineLayout, 0, 1, &get_current_frame().globalDescriptor, 1, &uniform_offset);
		}


		glm::mat4 model = glm::identity<glm::mat4>(); //TODO:fix for non-VR object.transformMatrix;
		//final render matrix, that we are calculating on the cpu
		glm::mat4 mesh_matrix = projection * view * model;

		MeshPushConstants constants;
		//constants.render_matrix = mesh_matrix; TODO:broken for non-vr

		//upload the mesh to the GPU via push constants
		vkCmdPushConstants(cmd, object.material->pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(MeshPushConstants), &constants);

		//only bind the mesh if it's a different one from last bind
		if (object.mesh != lastMesh) {
			//bind the mesh vertex buffer with offset 0
			VkDeviceSize offset = 0;
			vkCmdBindVertexBuffers(cmd, 0, 1, &object.mesh->_vertexBuffer._buffer, &offset);
			lastMesh = object.mesh;
		}
		//we can now draw
		vkCmdDraw(cmd, object.mesh->_vertices.size(), 1, 0, 0);
	}
}

void VulkanEngine::xrDraw_objects(VkCommandBuffer cmd)
{

	_camPos += clamp(deltaTime, 0.0f,1.0f) * thumstickPos;
	XrMatrix4x4f proj;
	XrMatrix4x4f_CreateProjectionFov(&proj, GRAPHICS_VULKAN, _xrProjView.fov, 0.05f, 100.0f);
	glm::mat4 projection = Converter::xrMat4x4_to_glmMat4x4(proj);
	XrMatrix4x4f toView, viewInv;
	XrVector3f scale{ 1.f, 1.f, 1.f };
	XrMatrix4x4f_CreateTranslationRotationScale(&toView, &_xrProjView.pose.position, &_xrProjView.pose.orientation, &scale);
	XrMatrix4x4f_InvertRigidBody(&viewInv, &toView);
	glm::mat4 view = Converter::xrMat4x4_to_glmMat4x4(viewInv);
	view = glm::translate(view, _camPos);



	//fill a GPU camera data struct
	GPUCameraData camData;
	camData.proj = projection;
	camData.view = view;
	camData.viewproj = projection * view;

	//and copy it to the buffer
	void* data;
	vmaMapMemory(_allocator, get_current_frame().cameraBuffer._allocation, &data);

	memcpy(data, &camData, sizeof(GPUCameraData));

	vmaUnmapMemory(_allocator, get_current_frame().cameraBuffer._allocation);
	float framed = (_frameNumber / 120.f);

	_sceneParameters.ambientColor = { sin(framed),0,cos(framed),1 };
	//_sceneParameters.sunPosition = glm::vec3(handPose.position.x, handPose.position.y, handPose.position.z);
	_sceneParameters.sunPosition = glm::vec3(0.0f, 10.0f, 0.0f);
	_sceneParameters.camPosition = - _camPos - glm::vec3(_xrProjView.pose.position.x, _xrProjView.pose.position.y, _xrProjView.pose.position.z);
	spdlog::debug("{}, {}, {}", _sceneParameters.camPosition.x, _sceneParameters.camPosition.y, _sceneParameters.camPosition.z);
	char* sceneData;
	vmaMapMemory(_allocator, _sceneParameterBuffer._allocation, (void**)&sceneData);

	int frameIndex = _frameNumber % FRAME_OVERLAP;

	sceneData += pad_uniform_buffer_size(sizeof(GPUSceneData)) * frameIndex;

	memcpy(sceneData, &_sceneParameters, sizeof(GPUSceneData));

	vmaUnmapMemory(_allocator, _sceneParameterBuffer._allocation);



	Mesh* lastMesh = nullptr;
	Material* lastMaterial = nullptr;
	auto regview = mainScene.registry.view<RenderObject, transformation, baseGameobject>();



	void* objectData;
	vmaMapMemory(_allocator, get_current_frame().objectBuffer._allocation, &objectData);
	GPUObjectData* objectSSBO = (GPUObjectData*)objectData;
	int object_index = 0;
	for (auto entity : regview) {
		//Workout Model Transform
		glm::mat4 model = glm::mat4(1.0f);
		std::stack<glm::mat4> cumulative;
		cumulative.push(regview.get<transformation>(entity).get_mat());
		Entity* parent = regview.get<baseGameobject>(entity).parent;
		while (parent != NULL) {
			cumulative.push(parent->get_component<transformation>().get_mat());
			parent = parent->get_component<baseGameobject>().parent;
		}
		while (!cumulative.empty()) {
			model = model * cumulative.top();
			cumulative.pop();
		}
		objectSSBO[object_index].modelMatrix = model;
		object_index++;
	}
	vmaUnmapMemory(_allocator, get_current_frame().objectBuffer._allocation);

	object_index = 0;
	for (auto entity : regview)
	{
		RenderObject& object = regview.get<RenderObject>(entity);


		//only bind the pipeline if it doesn't match with the already bound one
		if (object.material != lastMaterial) {

			vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, object.material->pipeline);
			lastMaterial = object.material;
			//offset for our scene buffer
			uint32_t uniform_offset = pad_uniform_buffer_size(sizeof(GPUSceneData)) * frameIndex;

			vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, object.material->pipelineLayout, 0, 1, &get_current_frame().globalDescriptor, 1, &uniform_offset);
			//object data descriptor
			vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, object.material->pipelineLayout, 1, 1, &get_current_frame().objectDescriptor, 0, nullptr);
			if (object.material->textureSet != VK_NULL_HANDLE) {
				//texture descriptor
				vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, object.material->pipelineLayout, 3, 1, &shadowSet, 0, nullptr);
				vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, object.material->pipelineLayout, 2, 1, &object.material->textureSet, 0, nullptr);
			}
			if (object.renderType == RENDER_TYPE_ANIM) {
				vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, object.material->pipelineLayout, 4, 1, &get_current_frame().animationDescriptor, 0, nullptr);
			}
			MeshPushConstants constants;
			constants.cameraVP = projection * view;
			constants.lightSpaceMatrix = lightSpaceMatrix;
			vkCmdPushConstants(cmd, object.material->pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(MeshPushConstants), &constants);
		}
		

		if (regview.get<baseGameobject>(entity).name == "monkey") {
			//regview.get<transformation>(entity).rotation.y += 0.005f;
		}

		if (regview.get<baseGameobject>(entity).name == "hand") {
			
			XrResult res;
			XrSpaceLocation spaceLocation{ XR_TYPE_SPACE_LOCATION };
			res = xrLocateSpace(m_input.handSpace[Side::RIGHT], _xrSpace, _predictedDisplayTime, &spaceLocation);
			CHECK_XRRESULT(res, "xrLocateSpace");
			if (XR_UNQUALIFIED_SUCCESS(res)) {
				if ((spaceLocation.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT) != 0 &&
					(spaceLocation.locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT) != 0) {
					//std::cout << "Found" << std::endl;
					regview.get<transformation>(entity).position = - _camPos +  1.0f * glm::vec3(spaceLocation.pose.position.x, spaceLocation.pose.position.y, spaceLocation.pose.position.z);
					regview.get<transformation>(entity).scale = glm::vec3(0.1f, 0.1f, 0.1f);
				}
			}
		}



		//only bind the mesh if it's a different one from last bind
		if ((object.mesh != lastMesh) || (lastMesh == nullptr)) {
			//bind the mesh vertex buffer with offset 0
			VkDeviceSize offset = 0;
			switch (object.renderType) {
			case (RENDER_TYPE_NORMAL):
				vkCmdBindVertexBuffers(cmd, 0, 1, &object.mesh->_vertexBuffer._buffer, &offset);
				break;
			case (RENDER_TYPE_ANIM):
				vkCmdBindVertexBuffers(cmd, 0, 1, &object.animMesh->_vertexBuffer._buffer, &offset);
				vkCmdBindIndexBuffer(cmd, object.animMesh->_indicesBuffer._buffer, 0, ANIM_INDEX_BUF_TYPE);
				break;
			}
			
			lastMesh = object.mesh;
		}
		//we can now draw
		switch (object.renderType) {
		case (RENDER_TYPE_NORMAL):
			vkCmdDraw(cmd, object.mesh->_vertices.size(), 1, 0, object_index);
			break;
		case (RENDER_TYPE_ANIM):
			vkCmdDrawIndexed(cmd, static_cast<uint32_t>(object.animMesh->_indices.size()), 1, 0, 0,object_index);
			break;
		}
		
		object_index++;
	}
}



FrameData& VulkanEngine::get_current_frame()
{
	return _frames[_frameNumber % FRAME_OVERLAP];
}

void VulkanEngine::xrInitialiseActions() {
	// Create an action set.
	{
		XrActionSetCreateInfo actionSetInfo{ XR_TYPE_ACTION_SET_CREATE_INFO };
		strcpy_s(actionSetInfo.actionSetName, "gameplay");
		strcpy_s(actionSetInfo.localizedActionSetName, "Gameplay");
		actionSetInfo.priority = 0;
		CHECK_XRCMD(xrCreateActionSet(_xrInstance, &actionSetInfo, &m_input.actionSet));
	}

	// Get the XrPath for the left and right hands - we will use them as subaction paths.
	CHECK_XRCMD(xrStringToPath(_xrInstance, "/user/hand/left", &m_input.handSubactionPath[Side::LEFT]));
	CHECK_XRCMD(xrStringToPath(_xrInstance, "/user/hand/right", &m_input.handSubactionPath[Side::RIGHT]));

	// Create actions.
	{
		// Create an input action for grabbing objects with the left and right hands.
		XrActionCreateInfo actionInfo{ XR_TYPE_ACTION_CREATE_INFO };
		actionInfo.actionType = XR_ACTION_TYPE_FLOAT_INPUT;
		strcpy_s(actionInfo.actionName, "grab_object");
		strcpy_s(actionInfo.localizedActionName, "Grab Object");
		actionInfo.countSubactionPaths = uint32_t(m_input.handSubactionPath.size());
		actionInfo.subactionPaths = m_input.handSubactionPath.data();
		CHECK_XRCMD(xrCreateAction(m_input.actionSet, &actionInfo, &m_input.grabAction));

		// Create an input action getting the left and right hand poses.
		actionInfo.actionType = XR_ACTION_TYPE_POSE_INPUT;
		strcpy_s(actionInfo.actionName, "hand_pose");
		strcpy_s(actionInfo.localizedActionName, "Hand Pose");
		actionInfo.countSubactionPaths = uint32_t(m_input.handSubactionPath.size());
		actionInfo.subactionPaths = m_input.handSubactionPath.data();
		CHECK_XRCMD(xrCreateAction(m_input.actionSet, &actionInfo, &m_input.poseAction));

		// Create output actions for vibrating the left and right controller.
		actionInfo.actionType = XR_ACTION_TYPE_VIBRATION_OUTPUT;
		strcpy_s(actionInfo.actionName, "vibrate_hand");
		strcpy_s(actionInfo.localizedActionName, "Vibrate Hand");
		actionInfo.countSubactionPaths = uint32_t(m_input.handSubactionPath.size());
		actionInfo.subactionPaths = m_input.handSubactionPath.data();
		CHECK_XRCMD(xrCreateAction(m_input.actionSet, &actionInfo, &m_input.vibrateAction));

		// Create input actions for quitting the session using the left and right controller.
		// Since it doesn't matter which hand did this, we do not specify subaction paths for it.
		// We will just suggest bindings for both hands, where possible.
		actionInfo.actionType = XR_ACTION_TYPE_BOOLEAN_INPUT;
		strcpy_s(actionInfo.actionName, "quit_session");
		strcpy_s(actionInfo.localizedActionName, "Quit Session");
		actionInfo.countSubactionPaths = 0;
		actionInfo.subactionPaths = nullptr;
		CHECK_XRCMD(xrCreateAction(m_input.actionSet, &actionInfo, &m_input.quitAction));


		actionInfo.actionType = XR_ACTION_TYPE_FLOAT_INPUT;
		strcpy_s(actionInfo.actionName, "movement_x");
		strcpy_s(actionInfo.localizedActionName, "Movement X");
		actionInfo.countSubactionPaths = uint32_t(m_input.handSubactionPath.size());
		actionInfo.subactionPaths = m_input.handSubactionPath.data();
		CHECK_XRCMD(xrCreateAction(m_input.actionSet, &actionInfo, &m_input.movementXAction));

		actionInfo.actionType = XR_ACTION_TYPE_FLOAT_INPUT;
		strcpy_s(actionInfo.actionName, "movement_y");
		strcpy_s(actionInfo.localizedActionName, "Movement Y");
		actionInfo.countSubactionPaths = uint32_t(m_input.handSubactionPath.size());
		actionInfo.subactionPaths = m_input.handSubactionPath.data();
		CHECK_XRCMD(xrCreateAction(m_input.actionSet, &actionInfo, &m_input.movementYAction));

	}

	std::array<XrPath, Side::COUNT> selectPath;
	std::array<XrPath, Side::COUNT> squeezeValuePath;
	std::array<XrPath, Side::COUNT> squeezeForcePath;
	std::array<XrPath, Side::COUNT> squeezeClickPath;
	std::array<XrPath, Side::COUNT> posePath;
	std::array<XrPath, Side::COUNT> hapticPath;
	std::array<XrPath, Side::COUNT> menuClickPath;
	std::array<XrPath, Side::COUNT> bClickPath;
	std::array<XrPath, Side::COUNT> triggerValuePath;

	std::array<XrPath, Side::COUNT> thumbstickXPath;
	std::array<XrPath, Side::COUNT> thumbstickYPath;


	CHECK_XRCMD(xrStringToPath(_xrInstance, "/user/hand/left/input/select/click", &selectPath[Side::LEFT]));
	CHECK_XRCMD(xrStringToPath(_xrInstance, "/user/hand/right/input/select/click", &selectPath[Side::RIGHT]));
	CHECK_XRCMD(xrStringToPath(_xrInstance, "/user/hand/left/input/squeeze/value", &squeezeValuePath[Side::LEFT]));
	CHECK_XRCMD(xrStringToPath(_xrInstance, "/user/hand/right/input/squeeze/value", &squeezeValuePath[Side::RIGHT]));
	CHECK_XRCMD(xrStringToPath(_xrInstance, "/user/hand/left/input/squeeze/force", &squeezeForcePath[Side::LEFT]));
	CHECK_XRCMD(xrStringToPath(_xrInstance, "/user/hand/right/input/squeeze/force", &squeezeForcePath[Side::RIGHT]));
	CHECK_XRCMD(xrStringToPath(_xrInstance, "/user/hand/left/input/squeeze/click", &squeezeClickPath[Side::LEFT]));
	CHECK_XRCMD(xrStringToPath(_xrInstance, "/user/hand/right/input/squeeze/click", &squeezeClickPath[Side::RIGHT]));
	CHECK_XRCMD(xrStringToPath(_xrInstance, "/user/hand/left/input/grip/pose", &posePath[Side::LEFT]));
	CHECK_XRCMD(xrStringToPath(_xrInstance, "/user/hand/right/input/grip/pose", &posePath[Side::RIGHT]));
	CHECK_XRCMD(xrStringToPath(_xrInstance, "/user/hand/left/output/haptic", &hapticPath[Side::LEFT]));
	CHECK_XRCMD(xrStringToPath(_xrInstance, "/user/hand/right/output/haptic", &hapticPath[Side::RIGHT]));
	CHECK_XRCMD(xrStringToPath(_xrInstance, "/user/hand/left/input/menu/click", &menuClickPath[Side::LEFT]));
	CHECK_XRCMD(xrStringToPath(_xrInstance, "/user/hand/right/input/menu/click", &menuClickPath[Side::RIGHT]));
	CHECK_XRCMD(xrStringToPath(_xrInstance, "/user/hand/left/input/b/click", &bClickPath[Side::LEFT]));
	CHECK_XRCMD(xrStringToPath(_xrInstance, "/user/hand/right/input/b/click", &bClickPath[Side::RIGHT]));
	CHECK_XRCMD(xrStringToPath(_xrInstance, "/user/hand/left/input/trigger/value", &triggerValuePath[Side::LEFT]));
	CHECK_XRCMD(xrStringToPath(_xrInstance, "/user/hand/right/input/trigger/value", &triggerValuePath[Side::RIGHT]));


	CHECK_XRCMD(xrStringToPath(_xrInstance, "/user/hand/right/input/thumbstick/x", &thumbstickXPath[Side::RIGHT]));


	CHECK_XRCMD(xrStringToPath(_xrInstance, "/user/hand/right/input/thumbstick/y", &thumbstickYPath[Side::RIGHT]));
	CHECK_XRCMD(xrStringToPath(_xrInstance, "/user/hand/left/input/thumbstick/y", &thumbstickYPath[Side::LEFT]));

	// Suggest bindings for KHR Simple.
	{/*
		XrPath khrSimpleInteractionProfilePath;
		CHECK_XRCMD(
			xrStringToPath(_xrInstance, "/interaction_profiles/khr/simple_controller", &khrSimpleInteractionProfilePath));
		std::vector<XrActionSuggestedBinding> bindings{ {// Fall back to a click input for the grab action.
														{m_input.grabAction, selectPath[Side::LEFT]},
														{m_input.grabAction, selectPath[Side::RIGHT]},
														{m_input.poseAction, posePath[Side::LEFT]},
														{m_input.poseAction, posePath[Side::RIGHT]},
														{m_input.quitAction, menuClickPath[Side::LEFT]},
														{m_input.quitAction, menuClickPath[Side::RIGHT]},
														{m_input.vibrateAction, hapticPath[Side::LEFT]},
														{m_input.vibrateAction, hapticPath[Side::RIGHT]},
														{m_input.movementAction, thumbstickPath[Side::LEFT]},
														{m_input.movementAction, thumbstickPath[Side::RIGHT]}} };
		XrInteractionProfileSuggestedBinding suggestedBindings{ XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING };
		suggestedBindings.interactionProfile = khrSimpleInteractionProfilePath;
		suggestedBindings.suggestedBindings = bindings.data();
		suggestedBindings.countSuggestedBindings = (uint32_t)bindings.size();
		CHECK_XRCMD(xrSuggestInteractionProfileBindings(_xrInstance, &suggestedBindings));
		*/
	}
	// Suggest bindings for the Oculus Touch.
	{
		XrPath oculusTouchInteractionProfilePath;
		CHECK_XRCMD(
			xrStringToPath(_xrInstance, "/interaction_profiles/oculus/touch_controller", &oculusTouchInteractionProfilePath));
		std::vector<XrActionSuggestedBinding> bindings{ {{m_input.grabAction, squeezeValuePath[Side::LEFT]},
														{m_input.grabAction, squeezeValuePath[Side::RIGHT]},
														{m_input.poseAction, posePath[Side::LEFT]},
														{m_input.poseAction, posePath[Side::RIGHT]},
														{m_input.quitAction, menuClickPath[Side::LEFT]},
														{m_input.vibrateAction, hapticPath[Side::LEFT]},
														{m_input.vibrateAction, hapticPath[Side::RIGHT]},
														{m_input.movementYAction, thumbstickYPath[Side::LEFT]},
														{m_input.movementXAction, thumbstickXPath[Side::RIGHT]},
														{m_input.movementYAction, thumbstickYPath[Side::RIGHT]}} };
		XrInteractionProfileSuggestedBinding suggestedBindings{ XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING };
		suggestedBindings.interactionProfile = oculusTouchInteractionProfilePath;
		suggestedBindings.suggestedBindings = bindings.data();
		suggestedBindings.countSuggestedBindings = (uint32_t)bindings.size();
		CHECK_XRCMD(xrSuggestInteractionProfileBindings(_xrInstance, &suggestedBindings));
	}

	XrActionSpaceCreateInfo actionSpaceInfo{ XR_TYPE_ACTION_SPACE_CREATE_INFO };
	actionSpaceInfo.action = m_input.poseAction;
	actionSpaceInfo.poseInActionSpace.orientation.w = 1.f;
	actionSpaceInfo.subactionPath = m_input.handSubactionPath[Side::LEFT];
	CHECK_XRCMD(xrCreateActionSpace(_xrSession, &actionSpaceInfo, &m_input.handSpace[Side::LEFT]));
	actionSpaceInfo.subactionPath = m_input.handSubactionPath[Side::RIGHT];
	CHECK_XRCMD(xrCreateActionSpace(_xrSession, &actionSpaceInfo, &m_input.handSpace[Side::RIGHT]));

	XrSessionActionSetsAttachInfo attachInfo{ XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO };
	attachInfo.countActionSets = 1;
	attachInfo.actionSets = &m_input.actionSet;
	CHECK_XRCMD(xrAttachSessionActionSets(_xrSession, &attachInfo));
}

void VulkanEngine::xrPollActions()  {
	m_input.handActive = { XR_FALSE, XR_FALSE };

	// Sync actions
	const XrActiveActionSet activeActionSet{ m_input.actionSet, XR_NULL_PATH };
	XrActionsSyncInfo syncInfo{ XR_TYPE_ACTIONS_SYNC_INFO };
	syncInfo.countActiveActionSets = 1;
	syncInfo.activeActionSets = &activeActionSet;
	CHECK_XRCMD(xrSyncActions(_xrSession, &syncInfo));

	// Get pose and grab action state and start haptic vibrate when hand is 90% squeezed.
	for (auto hand : { Side::LEFT, Side::RIGHT }) {

		if (hand == Side::RIGHT) {
			XrActionStateGetInfo getInfo{ XR_TYPE_ACTION_STATE_GET_INFO };
			getInfo.action = m_input.movementXAction;
			getInfo.subactionPath = m_input.handSubactionPath[hand];
			XrActionStateFloat thumbPos{ XR_TYPE_ACTION_STATE_FLOAT };
			CHECK_XRCMD(xrGetActionStateFloat(_xrSession, &getInfo, &thumbPos));
			thumstickPos.x = clamp(thumbPos.currentState, -1.0f, 1.0f);

			//thumstickPos.y = 0;

			getInfo.action = m_input.movementYAction;
			thumbPos = { XR_TYPE_ACTION_STATE_FLOAT };
			CHECK_XRCMD(xrGetActionStateFloat(_xrSession, &getInfo, &thumbPos));
			if (thumbPos.isActive == XR_TRUE) {
				thumstickPos.z = clamp(thumbPos.currentState, -1.0f, 1.0f);
			}
			
		}
		else {
			XrActionStateGetInfo getInfo{ XR_TYPE_ACTION_STATE_GET_INFO };
			getInfo.action = m_input.movementXAction;
			getInfo.subactionPath = m_input.handSubactionPath[hand];
			XrActionStateFloat thumbPos{ XR_TYPE_ACTION_STATE_FLOAT };
			
		
			getInfo.action = m_input.movementYAction;
			thumbPos = { XR_TYPE_ACTION_STATE_FLOAT };
			CHECK_XRCMD(xrGetActionStateFloat(_xrSession, &getInfo, &thumbPos));
			if (thumbPos.isActive == XR_TRUE) {
				thumstickPos.y = clamp(thumbPos.currentState, -1.0f, 1.0f);
			}
		}

		XrActionStateGetInfo getInfo{ XR_TYPE_ACTION_STATE_GET_INFO };
		getInfo.action = m_input.grabAction;
		getInfo.subactionPath = m_input.handSubactionPath[hand];

		XrActionStateFloat grabValue{ XR_TYPE_ACTION_STATE_FLOAT };
		CHECK_XRCMD(xrGetActionStateFloat(_xrSession, &getInfo, &grabValue));
		if (grabValue.isActive == XR_TRUE) {
			// Scale the rendered hand by 1.0f (open) to 0.5f (fully squeezed).
			m_input.handScale[hand] = 1.0f - 0.5f * grabValue.currentState;
			if (grabValue.currentState > 0.9f) {
				XrHapticVibration vibration{ XR_TYPE_HAPTIC_VIBRATION };
				vibration.amplitude = 0.5;
				vibration.duration = XR_MIN_HAPTIC_DURATION;
				vibration.frequency = XR_FREQUENCY_UNSPECIFIED;

				XrHapticActionInfo hapticActionInfo{ XR_TYPE_HAPTIC_ACTION_INFO };
				hapticActionInfo.action = m_input.vibrateAction;
				hapticActionInfo.subactionPath = m_input.handSubactionPath[hand];
				CHECK_XRCMD(xrApplyHapticFeedback(_xrSession, &hapticActionInfo, (XrHapticBaseHeader*)&vibration));
			}
		}

		getInfo.action = m_input.poseAction;
		XrActionStatePose poseState{ XR_TYPE_ACTION_STATE_POSE };
		CHECK_XRCMD(xrGetActionStatePose(_xrSession, &getInfo, &poseState));
		m_input.handActive[hand] = poseState.isActive;
	}

	// There were no subaction paths specified for the quit action, because we don't care which hand did it.
	XrActionStateGetInfo getInfo{ XR_TYPE_ACTION_STATE_GET_INFO, nullptr, m_input.quitAction, XR_NULL_PATH };
	XrActionStateBoolean quitValue{ XR_TYPE_ACTION_STATE_BOOLEAN };
	CHECK_XRCMD(xrGetActionStateBoolean(_xrSession, &getInfo, &quitValue));
	if ((quitValue.isActive == XR_TRUE) && (quitValue.changedSinceLastSync == XR_TRUE) && (quitValue.currentState == XR_TRUE)) {
		CHECK_XRCMD(xrRequestExitSession(_xrSession));
	}
}