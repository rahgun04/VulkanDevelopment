#include "xr_linear.h"
#include <glm/glm.hpp>

class Converter {
public:
	static glm::mat4 xrMat4x4_to_glmMat4x4(XrMatrix4x4f in);
};