
#include "vk_ecs.h"
#include <glm/ext/matrix_transform.hpp>
#include <glm/gtx/quaternion.hpp>


transformation::transformation() {
	position = glm::vec3(0, 0, 0);
	scale = glm::vec3(1.f, 1.f, 1.f);
	rotation = glm::vec3(0, 0, 0);
	mat = glm::identity<glm::mat4>();
}

transformation::transformation(glm::vec3 pos, glm::vec3 rot, glm::vec3 sca) {
	position = pos;
	rotation = rot;
	scale = sca;
	mat = get_mat();
}

glm::mat4 transformation::get_mat() {

	glm::mat4 translation = glm::translate(glm::mat4{ 1.0 }, position);
	glm::mat4 sca = glm::scale(glm::mat4{ 1.0 }, scale);

	glm::mat4 rot = glm::toMat4(glm::quat(rotation));
	return translation * rot * sca;

	//return translation * sca;
	//return glm::mat4(1.0f);
}









Entity* Entity::add_child_entity(std::string name)
{
	Entity* ent = scene->add_entity(name);
	ent->get_component<baseGameobject>().parent = this;
	get_component<baseGameobject>().children.push_back(ent);
	return ent;
}

Entity::Entity(entt::entity p_entity_handle, Scene* p_scene)
{
	entity_handle = p_entity_handle;
	scene = p_scene;
}

Entity::Entity()
{
	entity_handle = entt::entity(0);
	scene = 0;
}


Scene::Scene() {
	id = 0;
	registry = entt::registry();
	entities = new std::list<Entity>();
}


Scene::Scene(int p_id) {
	id = p_id;
	registry = entt::registry();
	entities = new std::list<Entity>();
}

Entity* Scene::add_entity(std::string name)
{
	Entity* entity = new Entity(registry.create(), this);
	entity->add_component<baseGameobject>(name, nullptr);
	return entity;
}