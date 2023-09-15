#version 460
#extension GL_EXT_debug_printf : enable
layout (location = 0) in vec3 vPosition;
layout (location = 1) in vec3 vNormal;
layout (location = 2) in vec3 vColor;
layout (location = 3) in vec2 vTexCoord;
layout (location = 4) in ivec4 boneIds;
layout (location = 5) in vec4 weights;

layout (location = 0) out vec3 outColor;
layout (location = 1) out vec2 texCoord;
layout (location = 2) out vec3 FragPos;
layout (location = 3) out vec3 Normal;
layout (location = 4) out vec4 FragPosLightSpace;

layout(set = 0, binding = 0) uniform  CameraBuffer{
	mat4 view;
	mat4 proj;
	mat4 viewproj;
} cameraData;

struct ObjectData{
	mat4 model;
};

//all object matrices
layout(std140,set = 1, binding = 0) readonly buffer ObjectBuffer{

	ObjectData objects[];
} objectBuffer;


layout(std140,set = 4, binding = 0) readonly buffer AnimBuffer{

	ObjectData objects[];
} animBuffer;



//push constants block
layout( push_constant ) uniform constants
{
 vec4 data;
 mat4 cameraVP;
 mat4 lightSpaceMatrix;
} PushConstants;

void main()
{
	
	mat4 boneTransform = animBuffer.objects[boneIds[0]].model * weights[0];
	boneTransform += animBuffer.objects[boneIds[1]].model * weights[1];
	boneTransform += animBuffer.objects[boneIds[2]].model * weights[2];
	boneTransform += animBuffer.objects[boneIds[3]].model * weights[3];

	FragPos = vec3(boneTransform * vec4(vPosition, 1.0));
	Normal = transpose(inverse(mat3(boneTransform))) * vNormal;
	FragPosLightSpace = PushConstants.lightSpaceMatrix * vec4(FragPos, 1.0);
	
	
	gl_Position = PushConstants.cameraVP * vec4(FragPos, 1.0f);
	//gl_Position = PushConstants.cameraVP * vec4(vPosition, 1.0f);
	texCoord = vTexCoord; 
	outColor = vColor;
}