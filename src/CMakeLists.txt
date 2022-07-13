cmake_minimum_required (VERSION 3.8)
include(CMakePrintHelpers)





find_library(
        LIB_SDL_FILE 
        SDL2
        PATHS 
            D:\\SDL2-2.0.22\\lib\\x64)

find_library(
        LIB_VULK_FILE 
        vulkan-1
        PATHS 
            D:/VulkanSDK/1.3.216.0/Lib)



file(GLOB Sources "*.cpp" "*.h")




if (MSVC AND WIN32 AND NOT MSVC_VERSION VERSION_LESS 142)



    # option 2: apply the option on a per-target basis
    target_link_options(${PROJECT_NAME} PRIVATE $<$<CONFIG:Debug>:/INCREMENTAL>)
    target_compile_options(${PROJECT_NAME} PRIVATE $<$<CONFIG:Debug>:/ZI>)
    target_compile_options(${PROJECT_NAME} PRIVATE $<$<CONFIG:Debug>:/Gm>)

endif()


add_library(SRC STATIC ${Sources})
link_directories(SRC D:/VulkanSDK/1.3.216.0/Lib)
target_include_directories(SRC PUBLIC D:\\VulkanSDK\\1.3.216.0\\Include\\vulkan D:\\SDL2-2.0.22\\include)
target_include_directories(SRC PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
cmake_print_variables(LIB_SDL_FILE)
target_link_libraries(SRC ${LIB_SDL_FILE} ${LIB_VULK_FILE} vk-bootstrap::vk-bootstrap vma glm::glm tinyobjloader)