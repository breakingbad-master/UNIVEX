# Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#
# Optional build-time shader artifact integration.  The function is intentionally opt-in: a
# developer building only the Null/OpenGL slice does not need every platform SDK, while a release
# job can make the requested toolchain mandatory and get reproducible target artifacts.

function(uve_add_shader_artifacts target_name)
    set(options)
    set(one_value_args SOURCE STAGE OUTPUT_DIRECTORY)
    set(multi_value_args TARGETS INCLUDE_DIRECTORIES DEFINES)
    cmake_parse_arguments(UVE_SHADER "${options}" "${one_value_args}" "${multi_value_args}" ${ARGN})

    if(NOT UVE_SHADER_SOURCE OR NOT UVE_SHADER_STAGE OR NOT UVE_SHADER_OUTPUT_DIRECTORY)
        message(FATAL_ERROR
            "uve_add_shader_artifacts(${target_name}) requires SOURCE, STAGE, and OUTPUT_DIRECTORY")
    endif()

    find_package(Python3 COMPONENTS Interpreter REQUIRED)
    find_program(UVE_GLSLANG_VALIDATOR glslangValidator)
    find_program(UVE_SPIRV_CROSS spirv-cross)
    if(NOT UVE_GLSLANG_VALIDATOR OR NOT UVE_SPIRV_CROSS)
        message(FATAL_ERROR
            "Shader artifacts requested for ${target_name}, but glslangValidator and/or "
            "spirv-cross was not found")
    endif()

    if(UVE_SHADER_TARGETS)
        set(shader_targets ${UVE_SHADER_TARGETS})
    else()
        set(shader_targets vulkan android-vulkan opengl gles d3d12 metal ios)
    endif()

    get_filename_component(source_stem "${UVE_SHADER_SOURCE}" NAME_WE)
    set(output_directory "${UVE_SHADER_OUTPUT_DIRECTORY}")
    set(outputs "${output_directory}/${source_stem}.intermediate.spv")
    foreach(shader_target IN LISTS shader_targets)
        if(shader_target STREQUAL "vulkan" OR shader_target STREQUAL "android-vulkan")
            list(APPEND outputs "${output_directory}/${source_stem}.${shader_target}.spv")
        elseif(shader_target STREQUAL "opengl" OR shader_target STREQUAL "gles")
            list(APPEND outputs "${output_directory}/${source_stem}.${shader_target}.glsl")
        elseif(shader_target STREQUAL "d3d12")
            list(APPEND outputs "${output_directory}/${source_stem}.d3d12.hlsl")
        elseif(shader_target STREQUAL "metal" OR shader_target STREQUAL "ios")
            list(APPEND outputs "${output_directory}/${source_stem}.${shader_target}.metal")
        else()
            message(FATAL_ERROR "Unknown UniVex shader target '${shader_target}'")
        endif()
    endforeach()
    list(APPEND outputs "${output_directory}/shader_manifest.json")

    set(include_args)
    foreach(include_directory IN LISTS UVE_SHADER_INCLUDE_DIRECTORIES)
        list(APPEND include_args "--include-dir" "${include_directory}")
    endforeach()
    set(define_args)
    foreach(shader_define IN LISTS UVE_SHADER_DEFINES)
        list(APPEND define_args "--define" "${shader_define}")
    endforeach()
    set(target_args)
    foreach(shader_target IN LISTS shader_targets)
        list(APPEND target_args "--target" "${shader_target}")
    endforeach()

    add_custom_command(
        OUTPUT ${outputs}
        COMMAND ${CMAKE_COMMAND} -E make_directory "${output_directory}"
        COMMAND ${Python3_EXECUTABLE} "${CMAKE_SOURCE_DIR}/Engine/Tools/compile_shaders.py"
                "${UVE_SHADER_SOURCE}"
                --stage "${UVE_SHADER_STAGE}"
                --out-dir "${output_directory}"
                --glslang "${UVE_GLSLANG_VALIDATOR}"
                --spirv-cross "${UVE_SPIRV_CROSS}"
                ${target_args} ${include_args} ${define_args}
        DEPENDS "${UVE_SHADER_SOURCE}" "${CMAKE_SOURCE_DIR}/Engine/Tools/compile_shaders.py"
        COMMENT "Compiling ${target_name} shader artifacts"
        VERBATIM
    )
    add_custom_target("${target_name}" DEPENDS ${outputs})
endfunction()
