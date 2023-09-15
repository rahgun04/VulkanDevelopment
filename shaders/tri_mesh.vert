#version 460
#extension GL_EXT_debug_printf : enable
layout (location = 0) in vec3 vPosition;
layout (location = 1) in vec3 vNormal;
layout (location = 2) in vec3 vColor;
layout (location = 3) in vec2 vTexCoord;

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

//push constants block
layout( push_constant ) uniform constants
{
 vec4 data;
 mat4 cameraVP;
 mat4 lightSpaceMatrix;
} PushConstants;

void main()
{
	
	mat4 modelMatrix = objectBuffer.objects[gl_BaseInstance].model;


	FragPos = vec3(modelMatrix * vec4(vPosition, 1.0));
	Normal = transpose(inverse(mat3(modelMatrix))) * vNormal;
	FragPosLightSpace = PushConstants.lightSpaceMatrix * vec4(FragPos, 1.0);
	
	
	gl_Position = PushConstants.cameraVP * vec4(FragPos, 1.0f);
	texCoord = vTexCoord; 
	outColor = vColor;
}