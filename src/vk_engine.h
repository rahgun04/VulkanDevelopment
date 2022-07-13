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

using namespace std::chrono;


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

class VulkanEngine {
public:

	Mesh _monkeyMesh;

	VmaAllocator _allocator; //vma lib allocator

	DeletionQueue _mainDeletionQueue;

	bool _isInitialized{ false };
	int _frameNumber{ 0 };
	int _selectedShader{ 0 };


	VkImageView _depthImageView;
	AllocatedImage _depthImage;

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

	//array of image-views from the swapchain
	std::vector<VkImageView> _swapchainImageViews;


	VkQueue _graphicsQueue; //queue we will submit to
	uint32_t _graphicsQueueFamily; //family of that queue

	VkCommandPool _commandPool; //the command pool for our commands
	VkCommandBuffer _mainCommandBuffer; //the buffer we will record into


	VkRenderPass _renderPass;

	std::vector<VkFramebuffer> _framebuffers;


	//Semaphore and Fence
	VkSemaphore _presentSemaphore, _renderSemaphore;
	VkFence _renderFence;



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