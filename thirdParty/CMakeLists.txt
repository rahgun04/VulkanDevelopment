cmake_minimum_required (VERSION 3.8)






add_library(vma INTERFACE)
target_include_directories(vma INTERFACE vma)


add_library(stb INTERFACE)
target_include_directories(stb INTERFACE stb)

#[[
find_library(
        LIB_VULK_FILE 
        vulkan-1
        PATHS 
            D:/VulkanSDK/1.3.216.0/Lib)




link_directories(vma D:/VulkanSDK/1.3.216.0/Lib)
target_include_directories(vma PUBLIC D:\\VulkanSDK\\1.3.216.0\\Include\\vulkan)
target_link_libraries(vma ${LIB_VULK_FILE})
]]
