// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#pragma once

#include <string>
#include <utility>
#include <vector>

#include "uve/rhi/render_resource_descs_uve.h"

namespace UVE::Render::Shader {

/// Describes one shader stage to compile via IShaderManagerUVE::CreateSourceUVE().
/// `virtualFilePath` and `embeddedFallbackSourceCode` are both always provided (not an
/// either/or): ShaderManagerUVE first tries a matching mounted cooked artifact when the backend
/// has one, otherwise it tries the virtual authoring file (enabling hot-reload tracking) and
/// transparently falls back to the embedded string if IFileSystemUVE::HasFileUVE() is false for
/// `virtualFilePath` — see BuiltIn::kBasic3DSource for the convention every built-in follows.
struct ShaderSourceCompileDescUVE {
    ShaderStageUVE stage = ShaderStageUVE::Vertex;
    std::string virtualFilePath;

    /// Optional relative identity below ShaderManagerConfigUVE::cookedArtifactMountPrefixUVE.
    /// When empty, the manager retains the legacy source filename stem. Imported assets should
    /// persist a path-derived key such as `materials/stone` so same-named shaders in different
    /// directories cannot collide in the cooked tree.
    std::string cookedArtifactKeyUVE;

    std::string embeddedFallbackSourceCode;

    /// Extra `#define NAME VALUE` pairs injected after the engine's own baseline block
    /// (UVE_DEBUG/UVE_MOBILE/UVE_BACKEND_GL and UVE_VULKAN for Vulkan source fallback) — see
    /// Detail::ApplyPreprocessorUVE()'s doc comment.
    std::vector<std::pair<std::string, std::string>> extraDefines;

    std::string entryPointName = "main";

    /// Whether ShaderManagerUVE::UpdateUVE() polls this source's #include dependency closure for
    /// on-disk changes and recompiles it automatically. Ignored (never tracked) when resolved via
    /// `embeddedFallbackSourceCode` — there is no file to poll.
    bool hotReloadEnabledUVE = true;

    /// Purely for log/diagnostic messages — never affects compilation.
    std::string debugNameUVE;
};

} // namespace UVE::Render::Shader
