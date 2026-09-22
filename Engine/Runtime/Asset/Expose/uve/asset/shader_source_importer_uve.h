// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <string>

#include "uve/asset/i_asset_importer_uve.h"

namespace UVE::Asset {

/// Optional settings for raw one-stage shader imports. `virtualFilePath` is the canonical VFS path
/// of the authoring source that will be mounted alongside (or represented by) the cooked artifact
/// tree. Leaving it empty preserves the source-only import behavior used by older projects; setting
/// it is what lets a generic imported material select a manifest-verified backend artifact at runtime.
/// The derived/explicit cooked key keeps same-named files in separate source directories from
/// colliding in one artifact mount.
struct ShaderImportSettingsUVE final : AssetImportSettingsUVE {
    std::string virtualFilePath;
    /// Optional override for the relative cooked-artifact directory. When empty and
    /// `virtualFilePath` is set, the importer derives it by removing the source extension, e.g.
    /// `materials/stone.vert` becomes `materials/stone`.
    std::string cookedArtifactKey;

    [[nodiscard]] std::string GetCacheVersionUVE() const override;
};

/// Registers bounded raw shader-source importers for the unambiguous one-stage `.vert`, `.frag`,
/// and `.comp` extensions. Each source is copied into a validated `.uveshader` envelope with the
/// stage inferred from its extension; combined `.glsl` files remain outside this bridge because the
/// project convention uses them for dual-stage built-in programs. When `ShaderImportSettingsUVE`
/// carries a path, that path is serialized as shader metadata rather than being treated as a host
/// filesystem dependency. The importer owns no shader compilation, include resolution, GPU resource,
/// cooked-artifact generation, or asset-database state.
void RegisterShaderSourceImporterUVE(IAssetImporterUVE& importer);

} // namespace UVE::Asset
