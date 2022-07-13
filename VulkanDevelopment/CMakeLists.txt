# CMakeList.txt : CMake project for VulkanDevelopment, include source and define
# project specific logic here.
#
cmake_minimum_required (VERSION 3.8)

# Add source to this project's executable.
#add_executable (VulkanDevelopment "VulkanDevelopment.cpp" "VulkanDevelopment.h")
file(GLOB Sources "*.cpp" ".h")
add_executable(VulkanDevelopment ${Sources})




if (MSVC AND WIN32 AND NOT MSVC_VERSION VERSION_LESS 142)



    # option 2: apply the option on a per-target basis
    target_link_options(${PROJECT_NAME} PRIVATE $<$<CONFIG:Debug>:/INCREMENTAL>)
    target_compile_options(${PROJECT_NAME} PRIVATE $<$<CONFIG:Debug>:/ZI>)
    target_compile_options(${PROJECT_NAME} PRIVATE $<$<CONFIG:Debug>:/Gm>)

endif()

target_link_libraries(VulkanDevelopment SRC)




# TODO: Add tests and install targets if needed.
