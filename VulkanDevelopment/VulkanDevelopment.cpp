// VulkanDevelopment.cpp : Defines the entry point for the application.
//

#include "VulkanDevelopment.h"



using namespace std;

#include <vk_engine.h>

int main(int argc, char* argv[])
{
	VulkanEngine engine;

	engine.init();

	engine.run();

	engine.cleanup();

	return 0;
}