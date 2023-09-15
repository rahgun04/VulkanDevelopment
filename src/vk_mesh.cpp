#include <vk_mesh.h>



//make sure that you are including the library
#include <tiny_obj_loader.h>
#include <iostream>
#include <glm/gtx/quaternion.hpp>






class AssimpGLMHelpers
{
public:

	static inline glm::mat4 ConvertMatrixToGLMFormat(const aiMatrix4x4& from)
	{
		glm::mat4 to;
		//the a,b,c,d in assimp is the row ; the 1,2,3,4 is the column
		to[0][0] = from.a1; to[1][0] = from.a2; to[2][0] = from.a3; to[3][0] = from.a4;
		to[0][1] = from.b1; to[1][1] = from.b2; to[2][1] = from.b3; to[3][1] = from.b4;
		to[0][2] = from.c1; to[1][2] = from.c2; to[2][2] = from.c3; to[3][2] = from.c4;
		to[0][3] = from.d1; to[1][3] = from.d2; to[2][3] = from.d3; to[3][3] = from.d4;
		return to;
	}

	static inline glm::vec3 GetGLMVec(const aiVector3D& vec)
	{
		return glm::vec3(vec.x, vec.y, vec.z);
	}

	static inline glm::quat GetGLMQuat(const aiQuaternion& pOrientation)
	{
		return glm::quat(pOrientation.w, pOrientation.x, pOrientation.y, pOrientation.z);
	}
};
/*
void AnimatedMesh::add_bone_data(int boneIndex, float weight, int vertexId) {
	//Find and replace smallest
	float smallest = 1.5f;
	int smallestPos = 0;
	for (int i = 0; i < 4; i++) {
		if (_vertices[vertexId].Weights[i] < smallest) {
			smallest = _vertices[vertexId].Weights[i];
			smallestPos = i;
		}
	}
	_vertices[vertexId].BoneIDs[smallestPos] = boneIndex;
	_vertices[vertexId].Weights[smallestPos] = weight;

}
*/

void AnimatedMesh::add_bone_data(int boneIndex, float weight, int vertexId) {
	if (weight == 0.0f) {
		return;
	}
	for (uint16_t i = 0; i < 4; i++) {
		if (_vertices[vertexId].Weights[i] == 0.0) {
			_vertices[vertexId].BoneIDs[i] = boneIndex;
			_vertices[vertexId].Weights[i] = weight;
			return;
		}
		else {
			if (_vertices[vertexId].BoneIDs[i] == boneIndex) {
				return;
			}
		}
	}

	// should never get here - more bones than we have space for
	//assert(0);

}

bool AnimatedMesh::load_from_file(const char* filename)
{
	Assimp::Importer importer;
	importer.ReadFile(filename, aiProcess_Triangulate | aiProcess_GenSmoothNormals |
		aiProcess_FlipUVs);
	scene = importer.GetOrphanedScene();
	if (scene == nullptr) {
		return false;
	}
	const aiMesh* mesh = scene->mMeshes[0];
	anim = scene->mAnimations[0];
	_vertices.resize(mesh->mNumVertices);
	
	for (int v = 0; v < mesh->mNumVertices; v++) {
		_vertices[v].position = AssimpGLMHelpers::GetGLMVec(mesh->mVertices[v]);
		_vertices[v].uv = glm::vec2(mesh->mTextureCoords[0][v].x, mesh->mTextureCoords[0][v].y);
		_vertices[v].normal = AssimpGLMHelpers::GetGLMVec(mesh->mNormals[v]);
	}
	for (int f = 0; f < mesh->mNumFaces; f++) {
		if (mesh->mFaces[f].mNumIndices != 3) {
			assert(0);
		}
		for (int i = 0; i < mesh->mFaces[f].mNumIndices; i++) {
			_indices.push_back(mesh->mFaces[f].mIndices[i]);
		}
	}

	for (int i = 0; i < mesh->mNumBones; i++) {
		int BoneIndex = 0;
		std::string BoneName(mesh->mBones[i]->mName.data);
		if (boneMapping.find(BoneName) == boneMapping.end()) {
			BoneIndex = numBones;
			numBones++;
			BoneData bd;
			boneInfo.push_back(bd);

		}
		else {
			BoneIndex = boneMapping[BoneName];
		}

		boneMapping[BoneName] = BoneIndex;
		boneInfo[BoneIndex].boneOffset = AssimpGLMHelpers::ConvertMatrixToGLMFormat(mesh->mBones[i]->mOffsetMatrix);

		for (int j = 0; j < mesh->mBones[i]->mNumWeights; j++) {
			int VertexID =  mesh->mBones[i]->mWeights[j].mVertexId;
			float Weight = mesh->mBones[i]->mWeights[j].mWeight;
			add_bone_data(BoneIndex, Weight, VertexID);
		}

	}
	return true;
}


bool Mesh::load_from_obj(const char* filename)
{
	//attrib will contain the vertex arrays of the file
	tinyobj::attrib_t attrib;
	//shapes contains the info for each separate object in the file
	std::vector<tinyobj::shape_t> shapes;
	//materials contains the information about the material of each shape, but we won't use it.
	std::vector<tinyobj::material_t> materials;

	//error and warning output from the load function
	std::string warn;
	std::string err;

	//load the OBJ file
	tinyobj::LoadObj(&attrib, &shapes, &materials, &warn, &err, filename, nullptr);
	//make sure to output the warnings to the console, in case there are issues with the file
	if (!warn.empty()) {
		std::cout << "WARN: " << warn << std::endl;
	}
	//if we have any error, print it to the console, and break the mesh loading.
	//This happens if the file can't be found or is malformed
	if (!err.empty()) {
		std::cerr << err << std::endl;
		return false;
	}

	// Loop over shapes
	for (size_t s = 0; s < shapes.size(); s++) {
		
		// Loop over faces(polygon)
		size_t index_offset = 0;
		for (size_t f = 0; f < shapes[s].mesh.num_face_vertices.size(); f++) {

			//hardcode loading to triangles
			int fv = 3;

			// Loop over vertices in the face.
			for (size_t v = 0; v < fv; v++) {
				// access to vertex
				tinyobj::index_t idx = shapes[s].mesh.indices[index_offset + v];

				//vertex position
				tinyobj::real_t vx = attrib.vertices[3 * idx.vertex_index + 0];
				tinyobj::real_t vy = attrib.vertices[3 * idx.vertex_index + 1];
				tinyobj::real_t vz = attrib.vertices[3 * idx.vertex_index + 2];
				//vertex normal
				tinyobj::real_t nx = attrib.normals[3 * idx.normal_index + 0];
				tinyobj::real_t ny = attrib.normals[3 * idx.normal_index + 1];
				tinyobj::real_t nz = attrib.normals[3 * idx.normal_index + 2];

				//vertex uv

				tinyobj::real_t ux, uy;
				if (idx.texcoord_index == -1) {
					ux = attrib.texcoords[0];
					uy = attrib.texcoords[1];
				}
				else {
					ux = attrib.texcoords[2 * idx.texcoord_index + 0];
					uy = attrib.texcoords[2 * idx.texcoord_index + 1];
				}



				//copy it into our vertex
				Vertex new_vert;
				new_vert.position.x = vx;
				new_vert.position.y = vy;
				new_vert.position.z = vz;

				new_vert.normal.x = nx;
				new_vert.normal.y = ny;
				new_vert.normal.z = nz;

				new_vert.uv.x = ux;
				new_vert.uv.y = 1 - uy;

				//we are setting the vertex color as the vertex normal. This is just for display purposes
				new_vert.color = new_vert.normal;


				_vertices.push_back(new_vert);
			}
			index_offset += fv;
		}
	}

	return true;
}

VertexInputDescription Vertex::get_vertex_description()
{
	VertexInputDescription description;

	//we will have just 1 vertex buffer binding, with a per-vertex rate
	VkVertexInputBindingDescription mainBinding = {};
	mainBinding.binding = 0;
	mainBinding.stride = sizeof(Vertex);
	mainBinding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

	description.bindings.push_back(mainBinding);

	//Position will be stored at Location 0
	VkVertexInputAttributeDescription positionAttribute = {};
	positionAttribute.binding = 0;
	positionAttribute.location = 0;
	positionAttribute.format = VK_FORMAT_R32G32B32_SFLOAT;
	positionAttribute.offset = offsetof(Vertex, position);

	//Normal will be stored at Location 1
	VkVertexInputAttributeDescription normalAttribute = {};
	normalAttribute.binding = 0;
	normalAttribute.location = 1;
	normalAttribute.format = VK_FORMAT_R32G32B32_SFLOAT;
	normalAttribute.offset = offsetof(Vertex, normal);

	//Color will be stored at Location 2
	VkVertexInputAttributeDescription colorAttribute = {};
	colorAttribute.binding = 0;
	colorAttribute.location = 2;
	colorAttribute.format = VK_FORMAT_R32G32B32_SFLOAT;
	colorAttribute.offset = offsetof(Vertex, color);

	//UV will be stored at Location 3
	VkVertexInputAttributeDescription uvAttribute = {};
	uvAttribute.binding = 0;
	uvAttribute.location = 3;
	uvAttribute.format = VK_FORMAT_R32G32_SFLOAT;
	uvAttribute.offset = offsetof(Vertex, uv);

	description.attributes.push_back(positionAttribute);
	description.attributes.push_back(normalAttribute);
	description.attributes.push_back(colorAttribute);
	description.attributes.push_back(uvAttribute);
	return description;
}

VertexInputDescription AnimatedVertex::get_vertex_description()
{
	VertexInputDescription description;

	//we will have just 1 vertex buffer binding, with a per-vertex rate
	VkVertexInputBindingDescription mainBinding = {};
	mainBinding.binding = 0;
	mainBinding.stride = sizeof(AnimatedVertex);
	mainBinding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

	description.bindings.push_back(mainBinding);

	//Position will be stored at Location 0
	VkVertexInputAttributeDescription positionAttribute = {};
	positionAttribute.binding = 0;
	positionAttribute.location = 0;
	positionAttribute.format = VK_FORMAT_R32G32B32_SFLOAT;
	positionAttribute.offset = offsetof(AnimatedVertex, position);

	//Normal will be stored at Location 1
	VkVertexInputAttributeDescription normalAttribute = {};
	normalAttribute.binding = 0;
	normalAttribute.location = 1;
	normalAttribute.format = VK_FORMAT_R32G32B32_SFLOAT;
	normalAttribute.offset = offsetof(AnimatedVertex, normal);

	//Color will be stored at Location 2
	VkVertexInputAttributeDescription colorAttribute = {};
	colorAttribute.binding = 0;
	colorAttribute.location = 2;
	colorAttribute.format = VK_FORMAT_R32G32B32_SFLOAT;
	colorAttribute.offset = offsetof(AnimatedVertex, color);

	//UV will be stored at Location 3
	VkVertexInputAttributeDescription uvAttribute = {};
	uvAttribute.binding = 0;
	uvAttribute.location = 3;
	uvAttribute.format = VK_FORMAT_R32G32_SFLOAT;
	uvAttribute.offset = offsetof(AnimatedVertex, uv);

	//BoneIds will be stored at Location 4
	VkVertexInputAttributeDescription boneIDAttribute = {};
	boneIDAttribute.binding = 0;
	boneIDAttribute.location = 4;
	boneIDAttribute.format = VK_FORMAT_R32G32B32A32_SINT;
	boneIDAttribute.offset = offsetof(AnimatedVertex, BoneIDs);

	//BoneWeights will be stored at Location 5
	VkVertexInputAttributeDescription boneWeightAttribute = {};
	boneWeightAttribute.binding = 0;
	boneWeightAttribute.location = 5;
	boneWeightAttribute.format = VK_FORMAT_R32G32B32A32_SFLOAT;
	boneWeightAttribute.offset = offsetof(AnimatedVertex, Weights);

	description.attributes.push_back(positionAttribute);
	description.attributes.push_back(normalAttribute);
	description.attributes.push_back(colorAttribute);
	description.attributes.push_back(uvAttribute);
	description.attributes.push_back(boneIDAttribute);
	description.attributes.push_back(boneWeightAttribute);
	return description;
}

aiNodeAnim* AnimatedMesh::find_anim(std::string nodeName) {
	for (int i = 0; i < anim->mNumChannels; i++) {
		if (strcmp(anim->mChannels[i]->mNodeName.C_Str(), nodeName.c_str()) == 0) {
			return anim->mChannels[i];
		}
	}
	return nullptr;
}

void AnimatedMesh::update_transforms(float animationTime, aiNode* node, glm::mat4& parentTransform) {
	std::string NodeName(node->mName.data);
	glm::mat4 NodeTransformation = AssimpGLMHelpers::ConvertMatrixToGLMFormat(node->mTransformation);

	const aiNodeAnim* nodeAnim = find_anim(NodeName);

	if (nodeAnim) {
		glm::mat4 TranslationM, RotationM, ScalingM;
		TranslationM = glm::translate(glm::mat4(1.0f), AssimpGLMHelpers::GetGLMVec(nodeAnim->mPositionKeys[0].mValue));
		RotationM = glm::toMat4(AssimpGLMHelpers::GetGLMQuat(nodeAnim->mRotationKeys[0].mValue));
		ScalingM = glm::scale(ScalingM, AssimpGLMHelpers::GetGLMVec(nodeAnim->mScalingKeys[0].mValue));
		NodeTransformation = TranslationM * RotationM * ScalingM;
		glm::mat4 GlobalTransformation = parentTransform * NodeTransformation;

		if (boneMapping.find(NodeName) != boneMapping.end()) {
			int BoneIndex = boneMapping[NodeName];
			boneInfo[BoneIndex].FinalTransformation = GlobalTransformation * boneInfo[BoneIndex].boneOffset;
		}
		else {
			assert(0);
		}

		for (int i = 0; i < node->mNumChildren; i++) {
			update_transforms(0.0f, node->mChildren[i], GlobalTransformation);
		}
	}

}

void AnimatedMesh::update_anim() {
	update_transforms(0.0f, scene->mRootNode, glm::mat4(1.0f));
	boneTransforms.resize(numBones);

	for (int i = 0; i < numBones; i++) {
		boneTransforms[i].boneMatrix = boneInfo[i].FinalTransformation;
	}
}

