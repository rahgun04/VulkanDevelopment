#include "vk_engine.h"

#include <SDL.h>
#include <SDL_vulkan.h>

#include <vk_types.h>
#include <vk_init.h>

void VulkanEngine::init()
{
	// We initialize SDL and create a window with it. 
	SDL_Init(SDL_INIT_VIDEO);

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

	//everything went fine
	_isInitialized = true;
}

void VulkanEngine::cleanup()
{
	if (_isInitialized) {
		SDL_DestroyWindow(_window);
	}
}


void VulkanEngine::draw()
{
	//nothing yet
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
			//close the window when user clicks the X button or alt-f4s
			if (e.type == SDL_QUIT) bQuit = true;
		}

		draw();
	}
}