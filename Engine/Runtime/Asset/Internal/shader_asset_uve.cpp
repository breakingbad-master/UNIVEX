// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "uve/asset/shader_asset_uve.h"

#include <nlohmann/json.hpp>

#include "uve/asset/uve_file_envelope_uve.h"
#include "uve/logging/logging_macros_uve.h"

namespace UVE::Asset {
namespace {

[[nodiscard]] bool IsValidShaderStageValueUVE(const std::uint8_t value) noexcept {
    return value == static_cast<std::uint8_t>(ShaderStageKindUVE::Vertex) ||
           value == static_cast<std::uint8_t>(ShaderStageKindUVE::Fragment) ||
           value == static_cast<std::uint8_t>(ShaderStageKindUVE::Compute);
}

[[nodiscard]] bool IsValidShaderStageUVE(const ShaderStageKindUVE stage) noexcept {
    return IsValidShaderStageValueUVE(static_cast<std::uint8_t>(stage));
}

} // namespace

bool IsValidShaderVirtualFilePathUVE(const std::string_view virtualFilePath) noexcept {
    if (virtualFilePath.empty()) {
        return true;
    }
    if (virtualFilePath.front() == '/' || virtualFilePath.back() == '/' ||
        virtualFilePath.find('\\') != std::string_view::npos ||
        virtualFilePath.find('\0') != std::string_view::npos ||
        virtualFilePath.find(':') != std::string_view::npos) {
        return false;
    }

    std::size_t segmentStart = 0U;
    while (segmentStart < virtualFilePath.size()) {
        const std::size_t separator = virtualFilePath.find('/', segmentStart);
        const std::size_t segmentEnd = separator == std::string_view::npos ? virtualFilePath.size() : separator;
        const std::string_view segment = virtualFilePath.substr(segmentStart, segmentEnd - segmentStart);
        if (segment.empty() || segment == "." || segment == "..") {
            return false;
        }
        if (separator == std::string_view::npos) {
            break;
        }
        segmentStart = separator + 1U;
    }
    return true;
}

bool LoadShaderAssetUVE(const std::filesystem::path& path, ShaderAssetUVE& outShader) {
    const std::optional<std::pair<UveFileHeaderUVE, std::vector<std::byte>>> file = ReadUveFileUVE(path);
    if (!file.has_value()) {
        return false; // ReadUveFileUVE already logged the specific reason.
    }
    if (file->first.assetType != AssetKindUVE::Shader) {
        UVE_ERROR("ShaderAssetUVE: \"{}\" is not a shader file (asset type {})", path.string(),
                   static_cast<std::uint32_t>(file->first.assetType));
        return false;
    }

    const std::vector<std::byte>& payloadBuffer = file->second;
    const std::string payloadText(reinterpret_cast<const char*>(payloadBuffer.data()), payloadBuffer.size());

    nlohmann::json payload;
    try {
        payload = nlohmann::json::parse(payloadText);
    } catch (const nlohmann::json::parse_error& parseError) {
        UVE_ERROR("ShaderAssetUVE: failed to parse \"{}\": {}", path.string(), parseError.what());
        return false;
    }

    ShaderAssetUVE shader;
    std::uint8_t stageValue = 0U;
    try {
        stageValue = payload.at("stage").get<std::uint8_t>();
        shader.sourceCode = payload.at("sourceCode").get<std::string>();
        shader.entryPointName = payload.value("entryPointName", std::string("main"));
        shader.virtualFilePath = payload.value("virtualFilePath", std::string{});
        shader.cookedArtifactKey = payload.value("cookedArtifactKey", std::string{});
    } catch (const nlohmann::json::exception& fieldError) {
        UVE_ERROR("ShaderAssetUVE: \"{}\" is missing an expected field: {}", path.string(), fieldError.what());
        return false;
    }

    if (!IsValidShaderStageValueUVE(stageValue)) {
        UVE_ERROR("ShaderAssetUVE: \"{}\" has an unknown shader stage", path.string());
        return false;
    }
    shader.stage = static_cast<ShaderStageKindUVE>(stageValue);
    if (shader.sourceCode.empty()) {
        UVE_ERROR("ShaderAssetUVE: \"{}\" has empty source code", path.string());
        return false;
    }
    if (!IsValidShaderVirtualFilePathUVE(shader.virtualFilePath) ||
        !IsValidShaderVirtualFilePathUVE(shader.cookedArtifactKey)) {
        UVE_ERROR("ShaderAssetUVE: \"{}\" has an invalid virtual authoring path or cooked artifact key",
                  path.string());
        return false;
    }

    outShader = std::move(shader);
    return true;
}

bool SaveShaderAssetUVE(const ShaderAssetUVE& shader, const std::filesystem::path& path) {
    if (!IsValidShaderStageUVE(shader.stage)) {
        UVE_ERROR("ShaderAssetUVE: refusing to save an unknown shader stage to {}", path.string());
        return false;
    }
    if (!IsValidShaderVirtualFilePathUVE(shader.virtualFilePath) ||
        !IsValidShaderVirtualFilePathUVE(shader.cookedArtifactKey)) {
        UVE_ERROR("ShaderAssetUVE: refusing to save invalid virtual authoring path or cooked artifact key to {}",
                  path.string());
        return false;
    }
    nlohmann::json payload;
    payload["stage"] = static_cast<std::uint8_t>(shader.stage);
    payload["sourceCode"] = shader.sourceCode;
    payload["entryPointName"] = shader.entryPointName;
    payload["virtualFilePath"] = shader.virtualFilePath;
    payload["cookedArtifactKey"] = shader.cookedArtifactKey;

    const std::string payloadText = payload.dump();
    const auto* const payloadBytes = reinterpret_cast<const std::byte*>(payloadText.data());
    const std::vector<std::byte> payloadBuffer(payloadBytes, payloadBytes + payloadText.size());

    return WriteUveFileUVE(path, AssetKindUVE::Shader, payloadBuffer);
}

} // namespace UVE::Asset
