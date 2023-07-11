# CMakeList.txt : CMake project for EEE(C++), include source and define
# project specific logic here.
#
cmake_minimum_required (VERSION 3.8)

# Add source to this project's executable.
#add_executable (vectors "Vectors.cpp")

file(GLOB SOURCE_FILES *.c *.cpp)


add_link_options($<$<CONFIG:Debug>:/INCREMENTAL>)
add_compile_options($<$<CONFIG:Debug>:/ZI>)


find_library(
        LIB_ASSIMP_FILE 
        assimp-vc143-mtd
        PATHS 
            D:\\assimp\\out\\build\\x64-Debug\\lib)


foreach(SOURCE_PATH ${SOURCE_FILES})

    get_filename_component(EXECUTABLE_NAME ${SOURCE_PATH} NAME_WE)

    add_executable(${EXECUTABLE_NAME} ${SOURCE_PATH})
    target_include_directories(${EXECUTABLE_NAME} PUBLIC D:\\assimp\\include)
    target_include_directories(${EXECUTABLE_NAME} PUBLIC D:\\assimp\\out\\build\\x64-Debug\\include)
    target_link_libraries(${EXECUTABLE_NAME} ${LIB_ASSIMP_FILE})
endforeach(SOURCE_PATH ${SOURCE_FILES})






# TODO: Add tests and install targets if needed.
