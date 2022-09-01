#include "converter.h"
#include <glm/gtc/type_ptr.hpp>




glm::mat4 Converter::xrMat4x4_to_glmMat4x4(XrMatrix4x4f in)
{
	glm::mat4 out = glm::mat4(1.0f);
	float* pglm = (float*)glm::value_ptr(out);
	memcpy(pglm, &in.m[0], sizeof(float) * 16);
	return out;
}
