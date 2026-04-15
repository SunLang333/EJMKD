cmake_minimum_required(VERSION 3.21)
project(EJMKD LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(EJMKD_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)

find_package(Vulkan REQUIRED)

find_program(GLSLC_EXECUTABLE NAMES glslc HINTS ENV VULKAN_SDK PATH_SUFFIXES Bin)
if(NOT GLSLC_EXECUTABLE AND DEFINED ENV{VULKAN_SDK})
    set(_glslc_candidate "$ENV{VULKAN_SDK}/Bin/glslc.exe")
    if(EXISTS "${_glslc_candidate}")
        set(GLSLC_EXECUTABLE "${_glslc_candidate}")
    endif()
endif()

if(NOT GLSLC_EXECUTABLE)
    message(FATAL_ERROR "glslc was not found. Install the Vulkan SDK or add glslc to PATH.")
endif()

set(GENERATED_DIR "${CMAKE_BINARY_DIR}/generated")
file(MAKE_DIRECTORY "${GENERATED_DIR}")

set(SHADER_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/src/shaders/ejmdk.comp")
set(SHADER_SPV "${GENERATED_DIR}/ejmdk.comp.spv")
set(SHADER_HEADER "${GENERATED_DIR}/embedded_ejmdk_shader.h")

add_custom_command(
    OUTPUT "${SHADER_SPV}"
    COMMAND "${GLSLC_EXECUTABLE}" --target-env=vulkan1.2 -O "${SHADER_SOURCE}" -o "${SHADER_SPV}"
    DEPENDS "${SHADER_SOURCE}"
    VERBATIM
)

add_custom_command(
    OUTPUT "${SHADER_HEADER}"
    COMMAND "${CMAKE_COMMAND}"
            "-DINPUT_FILE=${SHADER_SPV}"
            "-DOUTPUT_FILE=${SHADER_HEADER}"
            "-DVARIABLE_NAME=kEjmdkShaderSpv"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/tools/embed_binary.cmake"
    DEPENDS "${SHADER_SPV}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/embed_binary.cmake"
    VERBATIM
)

add_custom_target(ejmdk_shader DEPENDS "${SHADER_HEADER}")

add_executable(ejmdk
    src/main.cpp
    src/media_foundation_video.cpp
    src/quadtree_analyzer.cpp
    src/vulkan_ejmdk.cpp
)

add_dependencies(ejmdk ejmdk_shader)

target_include_directories(ejmdk
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
        "${GENERATED_DIR}"
)

target_compile_definitions(ejmdk PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN UNICODE _UNICODE)

if(MSVC)
    target_compile_options(ejmdk PRIVATE /W4 /EHsc /permissive- /utf-8 /Zc:__cplusplus)
    if(EJMKD_WARNINGS_AS_ERRORS)
        target_compile_options(ejmdk PRIVATE /WX)
    endif()
else()
    target_compile_options(ejmdk PRIVATE -Wall -Wextra -Wpedantic)
endif()

target_link_libraries(ejmdk
    PRIVATE
        Vulkan::Vulkan
        mf
        mfplat
        mfreadwrite
        mfuuid
        ole32
        shlwapi
)
