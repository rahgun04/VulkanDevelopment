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

find_library(
        LIB_OXR_FILE 
        openxr_loaderd
        PATHS 
            D:\\OpenXR-SDK\\OpenXR-SDK\\out\\build\\x64-Debug\\src\\loader)

find_library(
        LIB_ASSIMP_FILE 
        assimp-vc143-mtd
        PATHS 
            D:\\assimp\\out\\build\\x64-Debug\\lib)



file(GLOB Sources "*.cpp" "*.h")






add_library(SRC STATIC ${Sources})




 




link_directories(SRC D:/VulkanSDK/1.3.216.0/Lib)
link_directories(SRC D:\\OpenXR-SDK\\OpenXR-SDK\\out\\build\\x64-Debug\\src\\loader)
target_include_directories(SRC PUBLIC D:\\VulkanSDK\\1.3.216.0\\Include\\vulkan D:\\SDL2-2.0.22\\include)
target_include_directories(SRC PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_include_directories(SRC PUBLIC D:\\OpenXR-SDK\\OpenXR-SDK\\include)
target_include_directories(SRC PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../ovr_sdk_win_32.0.0/LibOVR/Include)
target_include_directories(SRC PUBLIC D:\\assimp\\include)
target_include_directories(SRC PUBLIC D:\\assimp\\out\\build\\x64-Debug\\include)
cmake_print_variables(LIB_ASSIMP_FILE)
target_include_directories(SRC PUBLIC D:\\Users\\rahul\\Source\\repos\\VulkanDevelopment\\spdlog\\include)
target_link_libraries(SRC ${LIB_SDL_FILE} ${LIB_VULK_FILE} ${LIB_ASSIMP_FILE} vk-bootstrap::vk-bootstrap vma glm::glm tinyobjloader LibOVR spdlog::spdlog EnTT::EnTT ${LIB_OXR_FILE} stb imgui AssetSystem)

  