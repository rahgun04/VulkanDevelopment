# CMakeList.txt : Top-level CMake project file, do global configuration
# and include sub-projects here.
#
cmake_minimum_required (VERSION 3.8)

project ("VulkanDevelopment")


#option(VK_BOOTSTRAP_VULKAN_HEADER_DIR "HELP: vulkan Path" D:\\VulkanSDK\\1.3.216.0\\Include\\vulkan)
set(VK_BOOTSTRAP_VULKAN_HEADER_DIR  D:\\VulkanSDK\\1.3.216.0\\Include\\vulkan)
# Include sub-projects.
add_subdirectory ("VulkanDevelopment")
add_subdirectory ("src")
add_subdirectory(vk-bootstrap)


#Shader Compilation
set(GLSL_VALIDATOR "D:/VulkanSDK/1.3.216.0/Bin/glslangValidator.exe")
file(GLOB_RECURSE GLSL_SOURCE_FILES
    "${PROJECT_SOURCE_DIR}/shaders/*.frag"  
    "${PROJECT_SOURCE_DIR}/shaders/*.vert"
    "${PROJECT_SOURCE_DIR}/shaders/*.comp"
    )

foreach(GLSL ${GLSL_SOURCE_FILES})
  message(STATUS "BUILDING SHADER")
  get_filename_component(FILE_NAME ${GLSL} NAME)
  set(SPIRV "${PROJECT_SOURCE_DIR}/shaders/${FILE_NAME}.spv")
  message(STATUS ${GLSL})
  add_custom_command(
    OUTPUT ${SPIRV}
    COMMAND ${GLSL_VALIDATOR} -V ${GLSL} -o ${SPIRV}
    DEPENDS ${GLSL})
  list(APPEND SPIRV_BINARY_FILES ${SPIRV})
endforeach(GLSL)

add_custom_target(
    Shaders 
    DEPENDS ${SPIRV_BINARY_FILES}
	SOURCES ${GLSL_SOURCE_FILES}
    )