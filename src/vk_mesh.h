#pragma once

#include <vk_types.h>
#include <map>
#include <vector>
#include <glm/glm.hpp>

#include <assimp/Importer.hpp>      // C++ importer interface
#include <assimp/scene.h>           // Output data structure
#include <assimp/postprocess.h>     // Post processing flags

typedef uint32_t Index;
#define ANIM_INDEX_BUF_TYPE VK_INDEX_TYPE_UINT32

struct VertexInputDescription {

    std::vector<VkVertexInputBindingDescription> bindings;
    std::vector<VkVertexInputAttributeDescription> attributes;

    VkPipelineVertexInputStateCreateFlags flags = 0;
};

struct Vertex {

    glm::vec3 position;
    glm::vec3 normal;
    glm::vec3 color;
    glm::vec2 uv;
    static VertexInputDescription get_vertex_description();
};

struct BoneTransformData {
    glm::mat4 boneMatrix;
};

struct AnimatedVertex {

    glm::vec3 position;
    glm::vec3 normal;
    glm::vec3 color;
    glm::vec2 uv;
    glm::ivec4 BoneIDs;
    glm::vec4 Weights;
    static VertexInputDescription get_vertex_description();
};

struct Mesh {
    std::vector<Vertex> _vertices;

    AllocatedBuffer _vertexBuffer;
    bool load_from_obj(const char* filename);
};


struct BoneData {
    glm::mat4 boneOffset;
    glm::mat4 FinalTransformation;
};

class AnimatedMesh {
public:
    
    const aiScene* scene;
    aiAnimation* anim;
    int numBones{ 0 };
    std::vector<BoneTransformData> boneTransforms;
    std::map<std::string, int> boneMapping;
    std::vector<BoneData> boneInfo;
    std::vector<AnimatedVertex> _vertices;
    AllocatedBuffer _vertexBuffer;
    std::vector<Index> _indices;
    AllocatedBuffer _indicesBuffer;
    bool load_from_file(const char* filename);
    void update_transforms(float animationTime, aiNode* node, glm::mat4& parentTransform);
    void update_anim();
private:
    void add_bone_data(int boneIndex, float weight, int vertexId);
    aiNodeAnim* find_anim(std::string nodeName);
};