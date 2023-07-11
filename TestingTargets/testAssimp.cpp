#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include <iostream>







void main() {
	Assimp::Importer importer;
	const aiScene* scene = importer.ReadFile("D:\\Projects\\House-3D-Extraction\\Hall\\Hall.obj", aiProcess_Triangulate);
    if (!scene || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !scene->mRootNode)
    {
        std::cout << "ERROR::ASSIMP::" << importer.GetErrorString() << std::endl;
        return;
    }
    // process all the node's meshes (if any)
    for (unsigned int i = 0; i < scene->mRootNode->mNumMeshes; i++)
    {
        aiMesh* mesh = scene->mMeshes[scene->mRootNode->mMeshes[i]];
        std::cout << mesh->mName.C_Str() << std::endl;
    }
}