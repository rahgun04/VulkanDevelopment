#pragma once

#include <vk_types.h>
#include <vector>
#include <fstream>
#include <functional>
#include <deque>
#include "vk_mem_alloc.h"
#include <vk_mesh.h>
#include <glm/glm.hpp>
#include <glm/gtx/quaternion.hpp>
#include <iostream>
#include <chrono>
#include <unordered_map>
#include <map>
#include <stack>
#include "imgui.h"
#include "vk_ecs.h"
#include "vk_descriptors.h"

//
//EnTT
//
#include <entt/entt.hpp>




//
// OpenXR Headers
//
#define XR_USE_GRAPHICS_API_VULKAN
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>
#include <openxr/openxr_reflection.h>




struct Material {
	VkDescriptorSet textureSet{ VK_NULL_HANDLE }; //texture defaulted to null
	VkPipeline pipeline;
	VkPipelineLayout pipelineLayout;
};


enum RenderType {
	RENDER_TYPE_NORMAL,
	RENDER_TYPE_ANIM,
};

struct RenderObject {
	Mesh* mesh;
	Material* material;
	AnimatedMesh* animMesh;
	RenderType renderType {RENDER_TYPE_NORMAL};
};








namespace Side {
	const int LEFT = 0;
	const int RIGHT = 1;
	const int COUNT = 2;
}  // namespace Side

struct InputState {
	XrActionSet actionSet{ XR_NULL_HANDLE };
	XrAction grabAction{ XR_NULL_HANDLE };
	XrAction poseAction{ XR_NULL_HANDLE };
	XrAction vibrateAction{ XR_NULL_HANDLE };
	XrAction quitAction{ XR_NULL_HANDLE };

	XrAction movementXAction{ XR_NULL_HANDLE };
	XrAction movementYAction{ XR_NULL_HANDLE };
	



	std::array<XrPath, Side::COUNT> handSubactionPath;
	std::array<XrSpace, Side::COUNT> handSpace;
	std::array<float, Side::COUNT> handScale = { {1.0f, 1.0f} };
	std::array<XrBool32, Side::COUNT> handActive;
};



using namespace std::chrono;
//number of frames to overlap when rendering
constexpr unsigned int FRAME_OVERLAP = 3;

struct SwapChainSupportDetails {
	VkSurfaceCapabilitiesKHR capabilities;
	std::vector<VkSurfaceFormatKHR> formats;
	std::vector<VkPresentModeKHR> presentModes;
};







struct MeshPushConstants {
	glm::vec4 data;
	glm::mat4 cameraVP;
	glm::mat4 lightSpaceMatrix;

};

struct ShadowPushConstants {
	glm::vec4 data;
	glm::mat4 cameraVP;

};


struct DeletionQueue
{
	std::deque<std::function<void()>> deletors;

	void push_function(std::function<void()>&& function) {
		deletors.push_back(function);
	}

	void flush() {
		// reverse iterate the deletion queue to execute all the functions
		for (auto it = deletors.rbegin(); it != deletors.rend(); it++) {
			(*it)(); //call the function
		}

		deletors.clear();
	}
};


struct FrameData {
	VkSemaphore _presentSemaphore, _renderSemaphore;
	VkFence _renderFence;
	VkCommandPool _commandPool;
	VkCommandBuffer _mainCommandBuffer;
	VkCommandBuffer shadowCommandBuffer;
	//buffer that holds a single GPUCameraData to use when rendering
	AllocatedBuffer cameraBuffer;

	AllocatedBuffer objectBuffer;
	VkDescriptorSet objectDescriptor;

	VkDescriptorSet globalDescriptor;

	AllocatedBuffer animationBuffer;
	VkDescriptorSet animationDescriptor;

};

struct xrFrameData {
	VkSemaphore _presentSemaphore, _renderSemaphore;
	VkFence _renderFence;
	VkCommandPool _commandPool;
	VkCommandBuffer _mainCommandBuffer;
};

struct GPUObjectData {
	glm::mat4 modelMatrix;
};



struct GPUCameraData {
	glm::mat4 view;
	glm::mat4 proj;
	glm::mat4 viewproj;
};

struct RenderTexture {
	VkImage depthImg;
	VkImage colImg;
	VkImageView depthView;
	VkImageView colView;
	VkFramebuffer fb;

};


struct Image {
	VkImage image;
	VmaAllocation allocation;
	VkImageView imageView;
};

struct PassData {
	VkRenderPass rp;
	VkFramebuffer fb;
	VkPipelineLayout pipelineLayout;
	VkPipeline pl;
};

struct xrRenderTarget {
	uint32_t width;
	uint32_t height;
	VkImage xrDepthImage;
	VkImageView xrDepthView;
	VmaAllocation xrDepthAllocation;
	std::vector<VkImage> xrImages;
	std::vector<VkImageView> xrImageViews;
	VkRenderPass renderPass;
	std::vector<VkFramebuffer> framebuffers;

};

struct GPUSceneData {
	glm::vec4 fogColor; // w is for exponent
	glm::vec4 fogDistances; //x for min, y for max, zw unused.
	glm::vec4 ambientColor;
	glm::vec4 sunlightDirection; //w for sun power
	glm::vec4 sunlightColor;
	glm::vec3 sunPosition;
	glm::vec3 camPosition;
};

struct UploadContext {
	VkFence _uploadFence;
	VkCommandPool _commandPool;
	VkCommandBuffer _commandBuffer;
};

struct Texture {
	AllocatedImage image;
	VkImageView imageView;
};



class VulkanEngine {
public:

	Scene mainScene;
	AnimatedMesh character;
	GPUSceneData _sceneParameters;
	AllocatedBuffer _sceneParameterBuffer;
	float deltaTime;
	//Entity monkey;
	
	InputState m_input;
	glm::vec3 thumstickPos {0.0f, 0.0f, 0.0f};

	XrInstance _xrInstance;
	XrSystemId _xrSystemId;
	XrSpace _xrSpace;
	XrGraphicsBindingVulkan2KHR _xrGraphicsBinding{ XR_TYPE_GRAPHICS_BINDING_VULKAN2_KHR };
	XrSession _xrSession;
	std::vector<XrSpace> _xrVisualizedSpaces;
	std::vector<XrViewConfigurationView> _xrConfigViews;
	std::vector<XrView> _xrViews;

	std::vector<XrSwapchain> _xrSwapchains;
	std::vector<xrRenderTarget> _xrRenderTargets;
	XrFovf _xrFovDetails;
	XrEventDataBuffer _xrEventDataBuffer;
	XrSessionState _xrSessionState{XR_SESSION_STATE_UNKNOWN};
	bool _xrSessionRunning{false};

	bool bQuit{ false };

	XrTime _predictedDisplayTime;

	UploadContext _uploadContext;

	void immediate_submit(std::function<void(VkCommandBuffer cmd)>&& function);


	VulkanEngine();

	glm::mat4 cameraRotationTransform{ 0 };
	glm::vec3 _tgtPos{ 0.f,-2.f,-5.f };
	glm::vec3 _camPos{ 0.f, -5.f,-2.f };
	glm::vec3 _vrPos{ 0.f, 0.f, 0.f };
	glm::quat _vrRot;
	XrPosef handPose;
	glm::mat4 lightSpaceMatrix;


	XrCompositionLayerProjectionView _xrProjView;

	float pitch{ 0 };
	float yaw{ 0 };
	Mesh _monkeyMesh;
	ImGuiContext* _imguictx;

	VmaAllocator _allocator; //vma lib allocator

	DeletionQueue _mainDeletionQueue;

	bool _isInitialized{ false };
	int _frameNumber{ 0 };
	int _selectedShader{ 0 };


	VkPhysicalDeviceProperties _gpuProperties;
	//the format for the depth image
	VkFormat _depthFormat;

	VkInstance _instance; // Vulkan library handle
	VkDebugUtilsMessengerEXT _debug_messenger; // Vulkan debug output handle
	VkPhysicalDevice _chosenGPU; // GPU chosen as the default device
	VkDevice _device; // Vulkan device for commands
	VkSurfaceKHR _surface; // Vulkan window surface

	VkSwapchainKHR _swapchain; // from other articles

	VkDescriptorSetLayout _globalSetLayout;
	VkDescriptorSetLayout _objectSetLayout;
	VkDescriptorSetLayout _singleTextureSetLayout;
	VkDescriptorSetLayout boneTransformSetLayout;
	VkDescriptorSetLayout shadowTextureSetLayout;
	VkDescriptorSet shadowSet;
	VkDescriptorPool _descriptorPool;

	vkutil::DescriptorAllocator* _descriptorAllocator;
	vkutil::DescriptorLayoutCache* _descriptorLayoutCache;

// image format expected by the windowing system
	VkFormat _swapchainImageFormat;

	//array of images from the swapchain
	std::vector<VkImage> _swapchainImages;
	std::vector<AllocatedImage> _swapchainDepthImages;
	//array of image-views from the swapchain
	std::vector<VkImageView> _swapchainImageViews;
	std::vector<VkImageView> _swapchainDepthImageViews;

	VkSwapchainKHR _mirrorSwapchain;
	//array of images for the mirror swapchain
	std::vector<VkImage> _mirrorSwapchainImages;
	std::vector<VkImageView> _mirrorSwapchainImageViews;
	VkFormat _mirrorSwapchainImageFormat;

	VkQueue _graphicsQueue; //queue we will submit to
	uint32_t _graphicsQueueFamily; //family of that queue

	VkExtent3D shadowMapResolution{ 2048, 2048, 1 };
	Image shadowImage;

	VkRenderPass _renderPass;
	PassData shadowPassData;

	std::vector<VkFramebuffer> _framebuffers;


	VkPipelineLayout _meshPipelineLayout, _texturedPipeLayout;
	VkPipeline _meshPipeline;
	Mesh _triangleMesh;
	

	VkExtent2D _windowExtent{ 1700 , 1700 };

	struct SDL_Window* _window{ nullptr };

	std::chrono::high_resolution_clock::time_point _previousTime;

	//initializes everything in the engine
	void init();
	void openXR_init();

	//shuts down the engine
	void cleanup();
	void xrCleanup();

	//draw loop
	void draw();
	void xrDraw();
	bool xrRenderLayer(XrTime predictedDisplayTime, std::vector<XrCompositionLayerProjectionView>& projectionLayerViews,
		XrCompositionLayerProjection& layer);
	void xrRenderView(VkCommandBuffer cmd, const XrCompositionLayerProjectionView& layerView, int viewNumber, int imageIndex, int mirrSwapchainImageIndex);

	//run main loop
	void run();
	void xrRun();


	void load_images();


	FrameData& get_current_frame();

	FrameData _frames[FRAME_OVERLAP];

	//default array of renderable objects
	std::vector<RenderObject> _renderables;

	std::unordered_map<std::string, Material> _materials;
	std::unordered_map<std::string, Mesh> _meshes;
	
	std::unordered_map<std::string, Texture> _loadedTextures;

	//create material and add it to the map
	Material* create_material(VkPipeline pipeline, VkPipelineLayout layout, const std::string& name);

	//returns nullptr if it can't be found
	Material* get_material(const std::string& name);

	//returns nullptr if it can't be found
	Mesh* get_mesh(const std::string& name);

	//our draw function
	void draw_objects(VkCommandBuffer cmd, RenderObject* first, int count);
	void xrDraw_objects(VkCommandBuffer cmd);

	AllocatedBuffer create_buffer(size_t allocSize, VkBufferUsageFlags usage, VmaMemoryUsage memoryUsage);


private:

	void shadowPass(VkCommandBuffer cmd);


	void init_vulkan();
	void init_xrVulkan();
	void init_descriptors();
	void init_imgui();


	void init_swapchain();
	void init_xrSwapchain();
	void init_mirSwapchain();

	void init_miscImages();

	void init_commands();
	void init_miscCommands();

	void init_default_xrRenderpass();
	void init_default_renderpass();
	void init_shadow_Renderpass();

	void init_framebuffers();
	void init_xrFramebuffers();
	void init_miscFramebuffers();

	void init_sync_structures();

	void init_pipelines();
	void init_xrPipelines();
	void init_miscPipelines();
	

	bool load_shader_module(const char* filePath, VkShaderModule* outShaderModule);
	
	//other code ....
	void load_meshes();
	

	void upload_animmesh(AnimatedMesh& mesh);
	void upload_mesh(Mesh& mesh);

	void init_scene();

	void xrInitialiseActions();
	void xrPollEvents();
	void xrPollActions();
	void xrHandleStateChange(const XrEventDataSessionStateChanged& stateChangedEvent);

	
	size_t pad_uniform_buffer_size(size_t originalSize);

};




class PipelineBuilder {
public:

	std::vector<VkPipelineShaderStageCreateInfo> _shaderStages;
	VkPipelineVertexInputStateCreateInfo _vertexInputInfo;
	VkPipelineInputAssemblyStateCreateInfo _inputAssembly;
	VkViewport _viewport;
	VkRect2D _scissor;
	VkPipelineRasterizationStateCreateInfo _rasterizer;
	VkPipelineColorBlendAttachmentState _colorBlendAttachment;
	VkPipelineMultisampleStateCreateInfo _multisampling;
	VkPipelineLayout _pipelineLayout;
	VkPipelineDepthStencilStateCreateInfo _depthStencil;

	VkPipeline build_pipeline(VkDevice device, VkRenderPass pass);
};