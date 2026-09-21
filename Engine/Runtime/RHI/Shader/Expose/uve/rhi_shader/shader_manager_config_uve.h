// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#pragma once

#include <filesystem>
#include <string>

namespace UVE::Render::Shader {

/// Construction-time configuration for ShaderManagerUVE, built from EngineConfigUVE by
/// EngineCoreUVE::Init() (mirrors ShaderManagerConfigUVE's role to WindowDescUVE's for
/// WindowManagerUVE).
struct ShaderManagerConfigUVE {
    /// Directory the on-disk program-binary cache is stored under (a per-platform subdirectory
    /// is appended automatically — see Detail's shader_binary_cache_uve.h).
    std::filesystem::path cachePath = "shader_cache/";

    /// Whether ShaderManagerUVE::UpdateUVE() polls hot-reload-tracked programs' dependency
    /// closures for on-disk changes at all.
    bool hotReloadEnabledUVE = true;

    /// Poll interval, in seconds, between hot-reload mtime checks.
    double hotReloadPollIntervalSecondsUVE = 1.0;

    /// Forwarded into every compile's injected `#define UVE_DEBUG 0|1` block.
    bool injectDebugDefineUVE = true;

    /// When true, ShaderManagerUVE looks for an offline, backend-native artifact before it
    /// preprocesses the authoring GLSL source. Missing artifacts are not fatal: the manager falls
    /// back to the existing source/embedded path, which keeps Null/OpenGL development builds
    /// usable when the optional artifact target was not built; Vulkan production builds should
    /// ship their validated SPIR-V artifacts. The artifact mount is expected to
    /// contain `<shader-stem>/<stage>/<stem>.<target>.<extension>` plus a per-stage manifest.
    bool preferCookedArtifactsUVE = true;

    /// VFS prefix containing the cooked shader-artifact tree. EngineCoreUVE mounts the configured
    /// real artifact directory at this prefix before constructing the manager.
    std::string cookedArtifactMountPrefixUVE = "shaders/cooked";
};

} // namespace UVE::Render::Shader
