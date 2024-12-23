cmake_minimum_required (VERSION 3.8)
include(CMakePrintHelpers)





find_package(SDL2 REQUIRED)
find_package(Vulkan REQUIRED FATAL_ERROR)



file(GLOB Sources "*.cpp" "*.h")




if (MSVC AND WIN32 AND NOT MSVC_VERSION VERSION_LESS 142)



    # option 2: apply the option on a per-target basis
    target_link_options(${PROJECT_NAME} PRIVATE $<$<CONFIG:Debug>:/INCREMENTAL>)
    target_compile_options(${PROJECT_NAME} PRIVATE $<$<CONFIG:Debug>:/ZI>)
    target_compile_options(${PROJECT_NAME} PRIVATE $<$<CONFIG:Debug>:/Gm>)

endif()


add_library(SRC STATIC ${Sources})
target_include_directories(SRC PUBLIC ${VULKAN_INCLUDE_DIRS} ${SDL2_INCLUDE_DIRS})
target_include_directories(SRC PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(SRC ${SDL2_LIBRARIES} ${VULKAN_LIB_LIST} vulkan vk-bootstrap::vk-bootstrap vma glm::glm tinyobjloader)