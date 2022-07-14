#pragma once

#include <stack> 
#include <map>
#include "glm/glm.hpp"
#include "vulkan.h"
#include <vk_mesh.h>


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


class GameObject {
public:
	
	GameObject* parent;
	RenderObject renderObject;
	glm::mat4 get_global_matrix();
	void move_object(glm::mat4 pose);

	void addChild(GameObject* go);
	

private:
	int childrenIndex{ 0 };
	std::map<int, GameObject*> children;
	glm::mat4 transformMatrix;
	glm::mat4 globalMatrixCache;
	bool globalMatrixCacheValidity{ false };
	void recurse_invalidate_cache(GameObject* go);

};