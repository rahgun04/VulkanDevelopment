# CMakeList.txt : CMake project for VulkanDevelopment, include source and define
# project specific logic here.
#
cmake_minimum_required (VERSION 3.8)

# Add source to this project's executable.
#add_executable (VulkanDevelopment "VulkanDevelopment.cpp" "VulkanDevelopment.h")
file(GLOB Sources "*.cpp" ".h")
add_executable(VulkanDevelopment ${Sources})

if (CMAKE_VERSION VERSION_GREATER 3.12)
  set_property(TARGET VulkanDevelopment PROPERTY CXX_STANDARD 20)
endif()



target_link_libraries(VulkanDevelopment SRC)




# TODO: Add tests and install targets if needed.
