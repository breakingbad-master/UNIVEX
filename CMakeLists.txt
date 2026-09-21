# Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#
# This repository is being rebuilt module by module (see the restructuring plan) from the
# original UNIVEX-ENGINE codebase, ported into the Engine/Runtime taxonomy described in
# UNIVEX_Engine_Scratch_Rebuild_Architecture_Prompt. Modules are added below in dependency
# order as each one lands; add_subdirectory calls appear only for modules that already exist
# here - see the plan for what's ported so far and what's still pending.

cmake_minimum_required(VERSION 3.24)
project(UniVexEngine LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

option(UVE_BUILD_TESTS "Build UniVex test suites" ON)
enable_testing()

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/Engine/CMake")

include(UveCompilerWarnings)
include(UveBuildConfiguration)
include(UveFormatSupport)
include(UveShaderPipeline)

option(UVE_BUILD_BUILTIN_SHADER_ARTIFACTS
    "Expose the reproducible offline cross-backend built-in shader artifact target" OFF)
set(UVE_SHADER_ARTIFACT_DIRECTORY "${CMAKE_BINARY_DIR}/shaders" CACHE PATH
    "Directory for generated UniVex shader artifacts")
if(UVE_BUILD_BUILTIN_SHADER_ARTIFACTS)
    set(_uve_builtin_shader_targets
        vulkan
        android-vulkan
        opengl
        gles
        d3d12
        metal
        ios)
    set(_uve_builtin_shader_artifact_targets)
    foreach(_uve_shader IN ITEMS
        bindless_probe
        frustum_cull
        frustum_cull_indirect
        mesh_skin
        particle_simulate)
        uve_add_shader_artifacts(
            "uve_shader_artifact_${_uve_shader}"
            SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/Engine/Runtime/RHI/Shader/built_in/${_uve_shader}.glsl"
            STAGE comp
            TARGETS ${_uve_builtin_shader_targets}
            OUTPUT_DIRECTORY "${UVE_SHADER_ARTIFACT_DIRECTORY}/${_uve_shader}")
        list(APPEND _uve_builtin_shader_artifact_targets "uve_shader_artifact_${_uve_shader}")
    endforeach()

    # Graphics shader migration starts with the two basic materials and the particle material.
    # The canonical sources remain OpenGL-compatible for the runtime fallback, while the offline
    # Vulkan compile selects each shader's explicit push-constant layout. SPIRV-Cross then emits
    # the corresponding native form for every requested backend; each stage gets its own directory
    # so a generated artifact can never be mistaken for the other entry point.
    foreach(_uve_shader IN ITEMS basic_2d basic_3d particle)
        uve_add_shader_artifacts(
            "uve_shader_artifact_${_uve_shader}_vert"
            SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/Engine/Runtime/RHI/Shader/built_in/${_uve_shader}.glsl"
            STAGE vert
            TARGETS ${_uve_builtin_shader_targets}
            DEFINES VERTEX_SHADER
            TARGET_DEFINES
                vulkan=UVE_VULKAN
                android-vulkan=UVE_VULKAN
                d3d12=UVE_VULKAN
                metal=UVE_VULKAN
                ios=UVE_VULKAN
            OUTPUT_DIRECTORY "${UVE_SHADER_ARTIFACT_DIRECTORY}/${_uve_shader}/vert")
        uve_add_shader_artifacts(
            "uve_shader_artifact_${_uve_shader}_frag"
            SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/Engine/Runtime/RHI/Shader/built_in/${_uve_shader}.glsl"
            STAGE frag
            TARGETS ${_uve_builtin_shader_targets}
            DEFINES FRAGMENT_SHADER
            TARGET_DEFINES
                vulkan=UVE_VULKAN
                android-vulkan=UVE_VULKAN
                d3d12=UVE_VULKAN
                metal=UVE_VULKAN
                ios=UVE_VULKAN
            OUTPUT_DIRECTORY "${UVE_SHADER_ARTIFACT_DIRECTORY}/${_uve_shader}/frag")
        list(APPEND _uve_builtin_shader_artifact_targets
            "uve_shader_artifact_${_uve_shader}_vert"
            "uve_shader_artifact_${_uve_shader}_frag")
    endforeach()

    add_custom_target(uve_builtin_shader_artifacts)
    add_dependencies(uve_builtin_shader_artifacts ${_uve_builtin_shader_artifact_targets})
endif()

include(FetchContent)
set(JSON_BuildTests OFF CACHE INTERNAL "")
set(JSON_Install OFF CACHE INTERNAL "")
set(JSON_Examples OFF CACHE INTERNAL "")
FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.11.3
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(nlohmann_json)

add_subdirectory(Engine/Runtime/Platform)
add_subdirectory(Engine/Runtime/Object)
add_subdirectory(Engine/Runtime/Core/Logging)
add_subdirectory(Engine/Runtime/Core/Memory)
add_subdirectory(Engine/Runtime/Core/Threading)
add_subdirectory(Engine/Runtime/Core/Scheduling)
add_subdirectory(Engine/Runtime/Core/Math)
add_subdirectory(Engine/Runtime/Core/Utilities)
add_subdirectory(Engine/Runtime/Core/Diagnostics)
add_subdirectory(Engine/Runtime/Events)
add_subdirectory(Engine/Runtime/Input)
add_subdirectory(Engine/Runtime/Window)
add_subdirectory(Engine/Runtime/RHI/RHI)
add_subdirectory(Engine/Runtime/RHI/OpenGL)
add_subdirectory(Engine/Runtime/RHI/Vulkan)
add_subdirectory(Engine/Runtime/RHI/Null)
add_subdirectory(Engine/Runtime/Commandline)
add_subdirectory(Engine/Runtime/Config)
add_subdirectory(Engine/Runtime/Network)
add_subdirectory(Engine/Runtime/Asset)
add_subdirectory(Engine/Runtime/RHI/Shader)
add_subdirectory(Engine/Runtime/Plugins)
add_subdirectory(Engine/Runtime/Component)
add_subdirectory(Engine/Runtime/Nodes/3D)
add_subdirectory(Engine/Runtime/Nodes/CanvasLayer)
add_subdirectory(Engine/Runtime/Entity)
add_subdirectory(Engine/Runtime/Animation)
add_subdirectory(Engine/Runtime/Scene)
add_subdirectory(Engine/Runtime/UI)
add_subdirectory(Engine/Runtime/Scripting)
add_subdirectory(Engine/Runtime/Save)
add_subdirectory(Engine/Runtime/RHI/RenderSystems)
add_subdirectory(Engine/Runtime/Audio)
add_subdirectory(Engine/Runtime/Physics)
add_subdirectory(Engine/Runtime/Engine)
add_subdirectory(Engine/Runtime/World)
add_subdirectory(Engine/Runtime/ProjectCheck)
add_subdirectory(Engine/Runtime/Pack)
add_subdirectory(Engine/Editor/EditorCore)
add_subdirectory(Engine/Editor/Viewport)
add_subdirectory(Engine/Editor/EditorApp)
add_subdirectory(Engine/App)

if(UVE_BUILD_TESTS)
    add_subdirectory(Test)
endif()
