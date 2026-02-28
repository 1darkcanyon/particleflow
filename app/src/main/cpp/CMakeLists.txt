cmake_minimum_required(VERSION 3.22.1)
project("particleflow")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra")

# Find required libraries
find_library(vulkan-lib vulkan REQUIRED)
find_library(log-lib log REQUIRED)
find_library(android-lib android REQUIRED)

# Include directories
include_directories(${CMAKE_SOURCE_DIR})

# Source files - add them one by one as you create them
set(SOURCES
    VulkanHelpers.cpp
    VulkanParticleEngine.cpp
    VulkanRenderer_JNI.cpp
)

# Create shared library
add_library(particleflow SHARED ${SOURCES})

# Link libraries
target_link_libraries(particleflow
    ${vulkan-lib}
    ${log-lib}
    ${android-lib}
)

# Optional: Compile shaders at build time
# Requires glslc in PATH
find_program(GLSLC glslc HINTS $ENV{ANDROID_NDK}/shader-tools/*)

if(GLSLC)
    message(STATUS "Found glslc: ${GLSLC}")
    
    set(SHADER_DIR ${CMAKE_SOURCE_DIR}/../shaders)
    set(SHADER_OUTPUT_DIR ${CMAKE_SOURCE_DIR}/../assets/shaders)
    
    file(MAKE_DIRECTORY ${SHADER_OUTPUT_DIR})
    
    # Compile each shader
    set(SHADERS
        particle_init.comp
        particle_update.comp
        particle_render.vert
        particle_render.frag
    )
    
    foreach(SHADER ${SHADERS})
        get_filename_component(SHADER_NAME ${SHADER} NAME_WE)
        get_filename_component(SHADER_EXT ${SHADER} EXT)
        
        # Determine output name
        if(SHADER_EXT STREQUAL ".comp")
            set(OUTPUT_NAME "${SHADER_NAME}.spv")
        elseif(SHADER_EXT STREQUAL ".vert")
            set(OUTPUT_NAME "${SHADER_NAME}_vert.spv")
        elseif(SHADER_EXT STREQUAL ".frag")
            set(OUTPUT_NAME "${SHADER_NAME}_frag.spv")
        endif()
        
        add_custom_command(
            OUTPUT ${SHADER_OUTPUT_DIR}/${OUTPUT_NAME}
            COMMAND ${GLSLC} ${SHADER_DIR}/${SHADER} -o ${SHADER_OUTPUT_DIR}/${OUTPUT_NAME}
            DEPENDS ${SHADER_DIR}/${SHADER}
            COMMENT "Compiling shader: ${SHADER}"
        )
        
        list(APPEND SHADER_OUTPUTS ${SHADER_OUTPUT_DIR}/${OUTPUT_NAME})
    endforeach()
    
    # Create target for shader compilation
    add_custom_target(compile_shaders ALL DEPENDS ${SHADER_OUTPUTS})
    add_dependencies(particleflow compile_shaders)
else()
    message(WARNING "glslc not found. You'll need to compile shaders manually.")
    message(WARNING "Install Android NDK and ensure shader-tools are in your PATH")
endif()
