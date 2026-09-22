// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

namespace UVE::Asset {

/// Which programmable stage a ShaderAssetUVE belongs to. Deliberately a separate type from
/// `Render::ShaderStageUVE` (engine/render, Increment 10) — see `Asset::TextureFormatUVE`'s doc
/// comment for why: avoiding a dependency cycle, since engine/render already depends on
/// engine/asset. `Compute` is reserved for the future `ComputeSystemUVE` (Part 7.2) — unused by
/// anything built so far.
enum class ShaderStageKindUVE : std::uint8_t { Vertex, Fragment, Compute };

/// The CPU-side, engine-native representation of a `.uveshader` asset (Part 2's file-format
/// table): shader source text, stored and validated as-is. `virtualFilePath` is optional metadata
/// naming the canonical authoring source in the engine VFS (for example,
/// `"materials/stone.vert"`). When present, the renderer passes it to ShaderManagerUVE so a
/// packaged, manifest-verified backend artifact can be selected for this imported shader; the
/// embedded source remains the deterministic fallback when the source/artifact mount is absent.
/// It is a VFS path, not a host filesystem path, and is deliberately stored in the shader asset
/// rather than a scene/material path dependency. Older envelopes without the optional field load
/// with an empty path and retain the source-only behavior.
struct ShaderAssetUVE {
    ShaderStageKindUVE stage = ShaderStageKindUVE::Vertex;
    std::string sourceCode;
    std::string entryPointName = "main";
    std::string virtualFilePath;
    /// Optional relative key below ShaderManagerUVE's cooked-artifact mount. It defaults to the
    /// source filename stem for legacy descriptors; importers should derive it from the complete
    /// virtual path (for example, `materials/stone`) so two directories may contain the same
    /// filename without colliding in the artifact cache.
    std::string cookedArtifactKey;
};

/// Returns true for an empty path or a safe forward-slash-separated VFS path. Absolute paths,
/// backslashes, dot components, parent traversal, and embedded NUL bytes are rejected so shader
/// metadata cannot escape an asset mount or create an ambiguous cooked-artifact lookup.
[[nodiscard]] bool IsValidShaderVirtualFilePathUVE(std::string_view virtualFilePath) noexcept;

/// Loads `path` as a `.uve*` envelope with `AssetKindUVE::Shader`, filling `outShader`. Returns
/// false (logging the reason) if the file is missing/malformed, isn't actually a Shader asset, its
/// `stage` is not one of the known `ShaderStageKindUVE` values, its virtual metadata is unsafe, or
/// its `sourceCode` is empty (a shader asset with no source is meaningless). The reserved `Compute`
/// enum value remains a valid typed asset value even though runtime compilation is future work.
[[nodiscard]] bool LoadShaderAssetUVE(const std::filesystem::path& path, ShaderAssetUVE& outShader);

/// Writes `shader` to `path` as a `.uve*` envelope with `AssetKindUVE::Shader`. Returns false
/// (logging the reason) for an unknown `ShaderStageKindUVE` value, unsafe virtual metadata, or if
/// the file can't be written.
[[nodiscard]] bool SaveShaderAssetUVE(const ShaderAssetUVE& shader, const std::filesystem::path& path);

} // namespace UVE::Asset
