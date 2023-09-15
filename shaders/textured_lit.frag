//glsl version 4.5
#version 450
#extension GL_EXT_debug_printf : enable

//shader input
layout (location = 0) in vec3 inColor;
layout (location = 1) in vec2 texCoord;
layout (location = 2) in vec3 FragPos;
layout (location = 3) in vec3 Normal;
layout (location = 4) in vec4 FragPosLightSpace;


//output write
layout (location = 0) out vec4 outFragColor;

layout(set = 0, binding = 1) uniform  SceneData{
    vec4 fogColor; // w is for exponent
	vec4 fogDistances; //x for min, y for max, zw unused.
	vec4 ambientColor;
	vec4 sunlightDirection; //w for sun power
	vec4 sunlightColor;
    vec3 sunPosition;
    vec3 camPosition;
} sceneData;

layout(set = 2, binding = 0) uniform sampler2D tex1;
layout(set = 3, binding = 0) uniform sampler2D tex2;



float ShadowCalculation(vec4 fragPosLightSpace)
{
    // perform perspective divide
    vec3 projCoords = fragPosLightSpace.xyz / fragPosLightSpace.w;
    // transform to [0,1] range
    projCoords = vec3(projCoords.xy * 0.5 + 0.5, projCoords.z);

    //debugPrintfEXT("FragPosLightSpace: %v4f", fragPosLightSpace);
    // get closest depth value from light's perspective (using [0,1] range fragPosLight as coords)
    float closestDepth = texture(tex2, projCoords.xy).r; 
    // get depth of current fragment from light's perspective
    float currentDepth = projCoords.z;
    // check whether current frag pos is in shadow
    float bias = 0.005;
    float shadow = currentDepth - bias > closestDepth  ? 1.0 : 0.0;  
    debugPrintfEXT("FragPosLightSpace: %v4f, Shadow: %f", fragPosLightSpace, shadow);
    return shadow;
} 


void main()
{

	vec3 color = texture(tex1, texCoord).rgb;
    vec3 normal = normalize(Normal);
    vec3 lightColor = vec3(1.0);
    // ambient
    vec3 ambient = 0.15 * lightColor;
    // diffuse
    vec3 lightDir = normalize(sceneData.sunPosition - vec3(FragPos));
    float diff = max(dot(lightDir, normal), 0.0);

    vec3 diffuse = diff * lightColor;
    // specular
    vec3 viewDir = normalize(sceneData.camPosition - FragPos);
    float spec = 0.0;
    vec3 halfwayDir = normalize(lightDir + viewDir);  
    spec = pow(max(dot(normal, halfwayDir), 0.0), 64.0);
    vec3 specular = spec * lightColor;    
    // calculate shadow
    float shadow = ShadowCalculation(FragPosLightSpace);       
    vec3 lighting = (ambient + (1.0 - shadow) * (diffuse + specular)) * color; 
    //vec3 lighting = (ambient + (1.0 - shadow) * (diffuse)) * color; 



	outFragColor = vec4(lighting,1.0f);
}