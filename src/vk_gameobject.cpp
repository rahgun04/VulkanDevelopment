#include "vk_gameobject.h"


using namespace std;

struct matrix_stackframe {
	glm::mat4 Transform;
	GameObject* go;
	
};

glm::mat4 GameObject::get_global_matrix()
{
	if (globalMatrixCacheValidity) {
		return globalMatrixCache;
	}
	stack<matrix_stackframe> transformStack;
	bool calcRootFound = false;

	matrix_stackframe currentMatrixStackframe = { };
	currentMatrixStackframe.Transform = this->transformMatrix;
	currentMatrixStackframe.go = this;

	transformStack.push(currentMatrixStackframe);
	GameObject*  currentGameObject = this;
	while (!calcRootFound) {
		
		currentGameObject = currentGameObject->parent;
		if (currentGameObject->globalMatrixCacheValidity) {
			currentMatrixStackframe.Transform = currentGameObject->globalMatrixCache;
			
			calcRootFound = true;
		}
		else if(currentGameObject->parent==nullptr) {
			calcRootFound = true;
		}
		else {
			currentMatrixStackframe.Transform = currentGameObject->transformMatrix;
			transformStack.push(currentMatrixStackframe);
		}

	}
	glm::mat4 transform = transformStack.top().Transform;
	transformStack.pop();
	while (!transformStack.empty()) {
		currentMatrixStackframe = transformStack.top();
		transformStack.pop();
		transform = currentMatrixStackframe.Transform * transform;
		if (!currentMatrixStackframe.go->globalMatrixCacheValidity) {
			currentMatrixStackframe.go->globalMatrixCache = transform;
		}
	}
	return transform;
}

void GameObject::move_object(glm::mat4 pose)
{
	recurse_invalidate_cache(this);
	transformMatrix = pose;
	
}

void GameObject::addChild(GameObject* go)
{
	this->childrenIndex++;
	//this->children.insert(pair<int, GameObject*>(this->childrenIndex, go));
	this->children[this->childrenIndex] = go;
}

void GameObject::recurse_invalidate_cache(GameObject* go) {
	auto iter = go->children.begin();
	while (iter != go->children.end()) {
		recurse_invalidate_cache(iter->second);
	}
	go->globalMatrixCacheValidity = false;
}


