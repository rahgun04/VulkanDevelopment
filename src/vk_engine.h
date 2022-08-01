#pragma once

#include <vk_types.h>
#include <vector>
#include <fstream>
#include <functional>
#include <deque>
#include "vk_mem_alloc.h"
#include <vk_mesh.h>
#include <glm/glm.hpp>
#include <iostream>
#include <chrono>
#include <unordered_map>
#include "OVR_CAPI.h"
#include "OVR_ErrorCode.h"
#include "OVR_Version.h"

using namespace std::chrono;
//number of frames to overlap when rendering
constexpr unsigned int FRAME_OVERLAP = 2;

struct VrApi {
	// Get the required Vulkan extensions for Vulkan instance creation
	std::function<bool(char* extensionNames, uint32_t* extensionNamesSize)> GetInstanceExtensionsVk;
	// Get the physical device corresponding to the VR session
	std::function<bool(VkInstance instance, VkPhysicalDevice* physicalDevice)> GetSessionPhysicalDeviceVk;
	// Get the required Vulkan extensions for Vulkan device creation
	std::function<bool(char* extensionNames, uint32_t* extensionNamesSize)> GetDeviceExtensionsVk;
	// Create the vulkan instance (nullptr => call vkCreateInstance)
	std::function<VkResult(PFN_vkGetInstanceProcAddr pfnGetInstanceProcAddr, const VkInstanceCreateInfo* pCreateInfo,
		const VkAllocationCallbacks* pAllocator, VkInstance* pInstance)> CreateInstanceVk;
	// Create the vulkan device (nullptr => call vkCreateDevice)
	std::function<VkResult(PFN_vkGetInstanceProcAddr pfnGetInstanceProcAddr, VkPhysicalDevice physicalDevice,
		const VkDeviceCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkDevice* pDevice)> CreateDeviceVk;
};


struct Material {
	VkPipeline pipeline;
	VkPipelineLayout pipelineLayout;
};

struct RenderObject {
	Mesh* mesh;

	Material* material;

	glm::mat4 transformMatrix;
};


struct MeshPushConstants {
	glm::vec4 data;
	glm::mat4 render_matrix;
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
};

class VulkanEngine {
public:
	glm::mat4 cameraRotationTransform{ 0 };
	glm::vec3 _tgtPos{ 0.f,-6.f,-10.f };;
	glm::vec3 _camPos{ 0.f,-6.f,-10.f };;

	float pitch{ 0 };
	float yaw{ 0 };
	Mesh _monkeyMesh;

	VmaAllocator _allocator; //vma lib allocator

	DeletionQueue _mainDeletionQueue;

	bool _isInitialized{ false };
	int _frameNumber{ 0 };
	int _selectedShader{ 0 };


	

	//the format for the depth image
	VkFormat _depthFormat;

	VkInstance _instance; // Vulkan library handle
	VkDebugUtilsMessengerEXT _debug_messenger; // Vulkan debug output handle
	VkPhysicalDevice _chosenGPU; // GPU chosen as the default device
	VkDevice _device; // Vulkan device for commands
	VkSurfaceKHR _surface; // Vulkan window surface



	VkSwapchainKHR _swapchain; // from other articles

// image format expected by the windowing system
	VkFormat _swapchainImageFormat;

	//array of images from the swapchain
	std::vector<VkImage> _swapchainImages;
	std::vector<AllocatedImage> _swapchainDepthImages;
	//array of image-views from the swapchain
	std::vector<VkImageView> _swapchainImageViews;
	std::vector<VkImageView> _swapchainDepthImageViews;


	VkQueue _graphicsQueue; //queue we will submit to
	uint32_t _graphicsQueueFamily; //family of that queue




	VkRenderPass _renderPass;

	std::vector<VkFramebuffer> _framebuffers;





	VkPipelineLayout _meshPipelineLayout;
	VkPipeline _meshPipeline;
	Mesh _triangleMesh;

	VkExtent2D _windowExtent{ 1700 , 900 };

	struct SDL_Window* _window{ nullptr };

	std::chrono::high_resolution_clock::time_point _previousTime;

	//initializes everything in the engine
	void init();

	//shuts down the engine
	void cleanup();

	//draw loop
	void draw();

	//run main loop
	void run();

	FrameData& get_current_frame();

	FrameData _frames[FRAME_OVERLAP];

	//default array of renderable objects
	std::vector<RenderObject> _renderables;

	std::unordered_map<std::string, Material> _materials;
	std::unordered_map<std::string, Mesh> _meshes;
	//functions

	//create material and add it to the map
	Material* create_material(VkPipeline pipeline, VkPipelineLayout layout, const std::string& name);

	//returns nullptr if it can't be found
	Material* get_material(const std::string& name);

	//returns nullptr if it can't be found
	Mesh* get_mesh(const std::string& name);

	//our draw function
	void draw_objects(VkCommandBuffer cmd, RenderObject* first, int count);





private:

	void init_vulkan();

	void init_swapchain();
	void init_commands();
	void init_default_renderpass();

	void init_framebuffers();
	void init_sync_structures();

	void init_pipelines();

	bool load_shader_module(const char* filePath, VkShaderModule* outShaderModule);
	
	//other code ....
	void load_meshes();

	void upload_mesh(Mesh& mesh);

	void VulkanEngine::init_scene();




	//Oculus Specific
	VrApi vrApi;

	ovrSession                  _session;
	ovrGraphicsLuid             _luid;

	ovrTextureSwapChain         textureChain;
	ovrTextureSwapChain         depthChain;

	//array of images from the swapchain
	std::vector<VkImage> _oculusDepthSwapchainImages;
	std::vector<VkImage> _oculusSwapchainImages;

	void init_oculus();
	void create_oculus_swapchain();
	void init_oculus_commands();
	void init_oculus_frameBuffer();
	FrameData _oculusFrames[FRAME_OVERLAP];
	FrameData& get_current_oculus_frame();
	std::vector<VkFramebuffer> _oculusFramebuffers;



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