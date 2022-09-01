

#include <SDL.h>
#include <SDL_vulkan.h>


// --- other includes ---



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







#include "vk_engine.h"
#include <vk_types.h>
#include <vk_init.h>
#include "spdlog/spdlog.h"
#include "converter.h"



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
			std::cout <<"Detected Vulkan error: " << err << std::endl; \
			abort();                                                \
		}                                                           \
	} while (0)

#define CHECKVK(x)                                                 \
	do                                                              \
	{                                                               \
		VkResult err = x;                                           \
		if (err)                                                    \
		{                                                           \
			std::cout <<"Detected Vulkan error: " << err << std::endl; \
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

	_triangleMesh._vertices[0].color = { 0.f,1.f, 0.0f }; //pure green
	_triangleMesh._vertices[1].color = { 0.f,1.f, 0.0f }; //pure green
	_triangleMesh._vertices[2].color = { 0.f,1.f, 0.0f }; //pure green

	//load the monkey
	_monkeyMesh.load_from_obj("../../../../assets/monkey.obj");
	//_monkeyMesh.load_from_obj("../../../../assets/uploads_files.obj");

	//make sure both meshes are sent to the GPU
	upload_mesh(_triangleMesh);
	upload_mesh(_monkeyMesh);

	//note that we are copying them. Eventually we will delete the hardcoded _monkey and _triangle meshes, so it's no problem now.
	_meshes["monkey"] = _monkeyMesh;
	_meshes["triangle"] = _triangleMesh;

}

void VulkanEngine::upload_mesh(Mesh& mesh)
{
	//allocate vertex buffer
	VkBufferCreateInfo bufferInfo = {};
	bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	//this is the total size, in bytes, of the buffer we are allocating
	bufferInfo.size = mesh._vertices.size() * sizeof(Vertex);
	//this buffer is going to be used as a Vertex Buffer
	bufferInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;


	//let the VMA library know that this data should be writeable by CPU, but also readable by GPU
	VmaAllocationCreateInfo vmaallocInfo = {};
	vmaallocInfo.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;

	//allocate the buffer
	VK_CHECK(vmaCreateBuffer(_allocator, &bufferInfo, &vmaallocInfo,
		&mesh._vertexBuffer._buffer,
		&mesh._vertexBuffer._allocation,
		nullptr));

	//add the destruction of triangle mesh buffer to the deletion queue
	_mainDeletionQueue.push_function([=]() {

		vmaDestroyBuffer(_allocator, mesh._vertexBuffer._buffer, mesh._vertexBuffer._allocation);
		});



	void* data;
	vmaMapMemory(_allocator, mesh._vertexBuffer._allocation, &data);

	memcpy(data, mesh._vertices.data(), mesh._vertices.size() * sizeof(Vertex));

	vmaUnmapMemory(_allocator, mesh._vertexBuffer._allocation);

}



void VulkanEngine::init()
{

	// We initialize SDL and create a window with it. 
	SDL_Init(SDL_INIT_VIDEO);
	SDL_SetRelativeMouseMode(SDL_TRUE);
	SDL_WindowFlags window_flags = (SDL_WindowFlags)(SDL_WINDOW_VULKAN);

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
	const std::vector<std::string> xrLayers = { "XR_APILAYER_LUNARG_core_validation"};

	std::transform(graphicsExtensions.begin(), graphicsExtensions.end(), std::back_inserter(extensions),
		[](const std::string& ext) { return ext.c_str(); });

	std::transform(xrLayers.begin(), xrLayers.end(), std::back_inserter(valLayers),
		[](const std::string& extt) { return extt.c_str(); });

	XrInstanceCreateInfo createInfo{ XR_TYPE_INSTANCE_CREATE_INFO };
	createInfo.next = nullptr;
	createInfo.enabledExtensionCount = (uint32_t)extensions.size();
	createInfo.enabledExtensionNames = extensions.data();
	createInfo.enabledApiLayerCount = (uint32_t)valLayers.size();
	createInfo.enabledApiLayerNames = valLayers.data();

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
	SDL_SetRelativeMouseMode(SDL_TRUE);
	SDL_WindowFlags window_flags = (SDL_WindowFlags)(SDL_WINDOW_VULKAN);

	//create blank SDL window for our application
	_window = SDL_CreateWindow(
		"Vulkan Engine", //window title
		SDL_WINDOWPOS_UNDEFINED, //window position x (don't care)
		SDL_WINDOWPOS_UNDEFINED, //window position y (don't care)
		_windowExtent.width,  //window width in pixels
		_windowExtent.height, //window height in pixels
		window_flags
	);


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

	init_xrSwapchain();
	init_commands();
	init_default_xrRenderpass();
	init_xrFramebuffers();
	init_sync_structures();
	init_xrPipelines();
	load_meshes();
	init_scene();
	while ((_xrSessionState == XR_SESSION_STATE_IDLE) || (_xrSessionState == XR_SESSION_STATE_UNKNOWN)){
		xrPollEvents();
	}

	_isInitialized = true;


}



void VulkanEngine::init_scene()
{
	RenderObject monkey;
	monkey.mesh = get_mesh("monkey");
	monkey.material = get_material("defaultmesh");
	monkey.transformMatrix = glm::mat4{ 1.0f };

	_renderables.push_back(monkey);

	for (int x = -20; x <= 20; x++) {
		for (int y = -20; y <= 20; y++) {

			RenderObject tri;
			tri.mesh = get_mesh("triangle");
			tri.material = get_material("defaultmesh");
			glm::mat4 translation = glm::translate(glm::mat4{ 1.0 }, glm::vec3(x, 0, y));
			glm::mat4 scale = glm::scale(glm::mat4{ 1.0 }, glm::vec3(0.2, 0.2, 0.2));
			tri.transformMatrix = translation * scale;

			_renderables.push_back(tri);
		}
	}
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
	
	spdlog::log((spdlog::level::level_enum) level, Fmt("%s (%s 0x%llx) [%s] %s", flagNames.c_str(), objName.c_str(), object, pLayerPrefix, pMessage));
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

	//layers.push_back("VK_LAYER_KHRONOS_validation");
	std::vector<const char*> extensions;
	extensions.push_back("VK_EXT_debug_report");


	VkApplicationInfo appInfo{ VK_STRUCTURE_TYPE_APPLICATION_INFO };
	appInfo.pApplicationName = "Rahul Vulkan XR";
	appInfo.applicationVersion = 1;
	appInfo.pEngineName = "rahgunEngine";
	appInfo.engineVersion = 1;
	appInfo.apiVersion = VK_API_VERSION_1_0;

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

	VkPhysicalDeviceFeatures features{};
	// features.samplerAnisotropy = VK_TRUE;



	VkDeviceCreateInfo deviceInfo{ VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO };
	deviceInfo.queueCreateInfoCount = 1;
	deviceInfo.pQueueCreateInfos = &queueInfo;
	deviceInfo.enabledLayerCount = 0;
	deviceInfo.ppEnabledLayerNames = nullptr;
	deviceInfo.enabledExtensionCount = (uint32_t)deviceExtensions.size();
	deviceInfo.ppEnabledExtensionNames = deviceExtensions.empty() ? nullptr : deviceExtensions.data();
	deviceInfo.pEnabledFeatures = &features;

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
		_swapchainImageFormat = (VkFormat) *swapchainFormatIt;

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

void VulkanEngine::init_commands()
{
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

		VK_CHECK(vkCreateRenderPass(_device, &render_pass_info, nullptr, &_xrRenderTargets[j].renderPass));


		_mainDeletionQueue.push_function([=]() {
			vkDestroyRenderPass(_device, _xrRenderTargets[j].renderPass, nullptr);
			});
	}
	
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

		

		vkDestroyDevice(_device, nullptr);
		vkDestroyInstance(_instance, nullptr);

		SDL_DestroyWindow(_window);
	}
}


void VulkanEngine::xrDraw()
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
	if (XR_TRUE){
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
		projectionLayerViews[i].subImage.imageRect.extent = {(int) _xrRenderTargets[i].width, (int) _xrRenderTargets[i].height };
		xrRenderView(projectionLayerViews[i], i, swapchainImageIndex);
		XrSwapchainImageReleaseInfo releaseInfo{ XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO };
		CHECK_XRCMD(xrReleaseSwapchainImage(_xrSwapchains[i], &releaseInfo));
	}
	layer.space = _xrSpace;
	layer.layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT | XR_COMPOSITION_LAYER_UNPREMULTIPLIED_ALPHA_BIT;
	layer.viewCount = (uint32_t)projectionLayerViews.size();
	layer.views = projectionLayerViews.data();
	return true;

}

void VulkanEngine::xrRenderView(const XrCompositionLayerProjectionView& layerView, int viewNumber, int imageIndex) {
	CHECK(layerView.subImage.imageArrayIndex == 0);
	//wait until the GPU has finished rendering the last frame. Timeout of 1 second
	VK_CHECK(vkWaitForFences(_device, 1, &get_current_frame()._renderFence, true, 1000000000));
	VK_CHECK(vkResetFences(_device, 1, &get_current_frame()._renderFence));
	VK_CHECK(vkResetCommandBuffer(get_current_frame()._mainCommandBuffer, 0));
	//naming it cmd for shorter writing
	VkCommandBuffer cmd = get_current_frame()._mainCommandBuffer;
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
	

	draw_objects(cmd, _renderables.data(), _renderables.size());
	vkCmdEndRenderPass(cmd);
	VK_CHECK(vkEndCommandBuffer(cmd));


	VkSubmitInfo submit = {};
	submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	submit.pNext = nullptr;

	submit.commandBufferCount = 1;
	submit.pCommandBuffers = &cmd;

	//submit command buffer to the queue and execute it.
	// _renderFence will now block until the graphic commands finish execution
	VK_CHECK(vkQueueSubmit(_graphicsQueue, 1, &submit, get_current_frame()._renderFence));
}


void VulkanEngine::draw()
{

	//FPS Count
	auto finish = std::chrono::high_resolution_clock::now();
	if (_frameNumber % 20 == 0) {
		if (!duration_cast<std::chrono::milliseconds>(finish - _previousTime).count() == 0){
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

	//main loop
	while (!bQuit)
	{
		xrPollEvents();
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
		if (_xrSessionRunning) {
			xrDraw();
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
	if (vkCreateShaderModule(_device, &createInfo, nullptr, &shaderModule) != VK_SUCCESS) {
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
	if (!load_shader_module("../../../../shaders/coloured_triangle.frag.spv", &triangleFragShader))
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


	VkShaderModule meshVertShader;
	if (!load_shader_module("../../../../shaders/tri_mesh.vert.spv", &meshVertShader))
	{
		std::cout << "Error when building the triangle mesh vertex shader module" << std::endl;
	}
	else {
		std::cout << "Triangle mesh vertex shader successfully loaded" << std::endl;
	}


	VkShaderModule triangleFragShader;
	if (!load_shader_module("../../../../shaders/coloured_triangle.frag.spv", &triangleFragShader))
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



	_meshPipeline = pipelineBuilder.build_pipeline(_device, _xrRenderTargets[0].renderPass);


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

	Mesh* lastMesh = nullptr;
	Material* lastMaterial = nullptr;
	for (int i = 0; i < count; i++)
	{
		RenderObject& object = first[i];

		//only bind the pipeline if it doesn't match with the already bound one
		if (object.material != lastMaterial) {

			vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, object.material->pipeline);
			lastMaterial = object.material;
		}


		glm::mat4 model = object.transformMatrix;
		//final render matrix, that we are calculating on the cpu
		glm::mat4 mesh_matrix = projection * view * model;

		MeshPushConstants constants;
		constants.render_matrix = mesh_matrix;

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



FrameData& VulkanEngine::get_current_frame()
{
	return _frames[_frameNumber % FRAME_OVERLAP];
}

