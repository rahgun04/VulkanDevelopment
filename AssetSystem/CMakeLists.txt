


file(GLOB Sources "*.cpp" "*.h")
add_executable(AssetSystemTool ${Sources})
add_library(AssetSystem ${Sources})





set_property(TARGET AssetSystemTool PROPERTY CXX_STANDARD 17)
set_property(TARGET AssetSystemTool PROPERTY CXX_STANDARD_REQUIRED ON)
set_property(TARGET AssetSystem PROPERTY CXX_STANDARD 17)
set_property(TARGET AssetSystem PROPERTY CXX_STANDARD_REQUIRED ON)

find_library(
    LZ4_FILE 
    liblz4
    PATHS 
        D:\\lz4-1.9.4\\build\\VS2017\\bin\\x64_Debug)



target_link_libraries(AssetSystemTool PRIVATE ${LZ4_FILE}  nlohmann_json::nlohmann_json stb tinyobjloader)
target_include_directories(AssetSystemTool PUBLIC D:\\lz4-1.9.4\\lib)
target_include_directories(AssetSystemTool PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(AssetSystem PRIVATE ${LZ4_FILE}  nlohmann_json::nlohmann_json stb tinyobjloader)
target_include_directories(AssetSystem PUBLIC D:\\lz4-1.9.4\\lib)
target_include_directories(AssetSystem PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})