#include <glm/glm.hpp>
#include <string>
#include <entt/entt.hpp>



class Scene;


class transformation {
public:
	glm::vec3 position;
	glm::vec3 rotation;
	glm::vec3 scale;
	transformation();
	transformation(glm::vec3 pos, glm::vec3 rot, glm::vec3 sca);
	glm::mat4 get_mat();
private:
	glm::mat4 mat;

};





class Entity {
private:
	entt::entity entity_handle = entt::entity(0);
	Scene* scene = nullptr;
public:

	Entity();
	Entity(entt::entity p_entity_handle, Scene* p_scene);

	Entity* add_child_entity(std::string name);


	template<typename T, typename... Args>
	T& add_component(Args&&... args) {
		return scene->registry.emplace<T>(entity_handle, std::forward<Args>(args)...);
	}

	template<typename T>
	T& get_component() {
		return scene->registry.get<T>(entity_handle);
	}

	template<typename T>
	void remove_component() {
		return scene->registry.remove<T>(entity_handle);
	}

};

struct baseGameobject {
public:
	std::string name;
	Entity* parent;
	std::list<Entity*> children;
};


class Scene {
public:
	entt::registry registry;
	int id;
	std::list<Entity>* entities;
	Entity* add_entity(std::string name);
	Scene();
	Scene(int p_id);

};


