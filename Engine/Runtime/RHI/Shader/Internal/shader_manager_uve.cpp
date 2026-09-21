// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "uve/rhi_shader/shader_manager_uve.h"

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <mutex>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_map>
#include <utility>
#include <vector>

#include "shader_binary_cache_uve.h"
#include "shader_diagnostics_parser_uve.h"
#include "shader_preprocessor_uve.h"
#include "uve/logging/logging_macros_uve.h"
#include "uve/rhi_shader/shader_events_uve.h"
#include "uve/threading/job_counter_uve.h"

namespace UVE::Render::Shader {

namespace {

struct CookedShaderArtifactUVE {
    std::string virtualPath;
    std::string manifestVirtualPath;
    std::vector<std::byte> bytes;
};

} // namespace

struct ShaderManagerUVE::ImplUVE {
    Threading::IThreadPoolUVE& threadPool;
    Events::IEventSystemUVE& eventSystem;
    IRenderDeviceUVE& renderDevice;
    Asset::IFileSystemUVE& fileSystem;
    ShaderManagerConfigUVE config;

    Threading::JobCounterUVE pendingJobs;
    mutable std::mutex mutex;
    std::size_t pendingJobCount = 0;

    struct SourceJobUVE {
        std::shared_ptr<ShaderSourceUVE> target;
        ShaderSourceCompileDescUVE desc;
        Detail::PreprocessResultUVE preprocess;
        std::optional<CookedShaderArtifactUVE> cookedArtifact;
    };
    std::vector<SourceJobUVE> completedSourceJobs; // guarded by mutex - written from worker threads

    /// Internal normalized representation shared by the legacy unified-source API and the
    /// Increment 34 separate-stage API. Source hot-reload flags are disabled here because linked
    /// program tracking owns the union of both stage dependency closures.
    struct ProgramRequestDescUVE {
        ShaderSourceCompileDescUVE vertexSource;
        ShaderSourceCompileDescUVE fragmentSource;
        std::vector<VertexAttributeUVE> vertexLayout;
        std::uint32_t vertexStride = 0;
        PrimitiveTopologyUVE topology = PrimitiveTopologyUVE::Triangles;
        bool depthTestEnabled = true;
        bool depthWriteEnabled = true;
        PipelineBlendModeUVE blendMode = PipelineBlendModeUVE::Opaque;
        bool hotReloadEnabledUVE = true;
        std::string debugNameUVE;
    };

    struct PendingProgramLinkUVE {
        std::shared_ptr<ShaderProgramUVE> program;
        std::shared_ptr<ShaderSourceUVE> vertexSource;
        std::shared_ptr<ShaderSourceUVE> fragmentSource;
        ProgramRequestDescUVE desc;
    };
    std::vector<PendingProgramLinkUVE> pendingProgramLinks; // main-thread only

    struct TrackedDependenciesUVE {
        std::vector<std::string> dependencyClosure;
        std::unordered_map<std::string, std::filesystem::file_time_type> lastKnownWriteTimes;
    };
    struct TrackedSourceUVE {
        std::weak_ptr<ShaderSourceUVE> target;
        ShaderSourceCompileDescUVE desc;
        TrackedDependenciesUVE dependencies;
    };
    struct TrackedProgramUVE {
        std::weak_ptr<ShaderProgramUVE> target;
        ProgramRequestDescUVE desc;
        TrackedDependenciesUVE dependencies;
    };
    std::vector<TrackedSourceUVE> trackedSources;   // main-thread only
    std::vector<TrackedProgramUVE> trackedPrograms; // main-thread only

    double hotReloadAccumulatorSeconds = 0.0;
    bool lastCompileUsedCache = false;

    ImplUVE(Threading::IThreadPoolUVE& threadPoolIn, Events::IEventSystemUVE& eventSystemIn,
            IRenderDeviceUVE& renderDeviceIn, Asset::IFileSystemUVE& fileSystemIn, ShaderManagerConfigUVE configIn)
        : threadPool(threadPoolIn),
          eventSystem(eventSystemIn),
          renderDevice(renderDeviceIn),
          fileSystem(fileSystemIn),
          config(std::move(configIn)) {}
};

namespace {

[[nodiscard]] const char* ShaderStageDefineNameUVE(ShaderStageUVE stage) noexcept {
    switch (stage) {
        case ShaderStageUVE::Vertex:
            return "VERTEX_SHADER";
        case ShaderStageUVE::Fragment:
            return "FRAGMENT_SHADER";
        case ShaderStageUVE::Compute:
            return "COMPUTE_SHADER";
        case ShaderStageUVE::Geometry:
            return "GEOMETRY_SHADER";
    }
    return "";
}

[[nodiscard]] std::vector<std::pair<std::string, std::string>> BuildDefinesUVE(
    ShaderStageUVE stage, bool injectDebugDefine, const std::vector<std::pair<std::string, std::string>>& extraDefines) {
    std::vector<std::pair<std::string, std::string>> defines;
    defines.reserve(extraDefines.size() + 4);
    defines.emplace_back("UVE_DEBUG", injectDebugDefine ? "1" : "0");
    defines.emplace_back("UVE_MOBILE", "0"); // No mobile backend exists yet - reserved.
    defines.emplace_back("UVE_BACKEND_GL", "1"); // Reserved for a future non-GL backend to define its own instead.
    defines.emplace_back(ShaderStageDefineNameUVE(stage), "1");
    defines.insert(defines.end(), extraDefines.begin(), extraDefines.end());
    return defines;
}

[[nodiscard]] const char* ShaderArtifactStageDirectoryUVE(ShaderStageUVE stage) noexcept {
    switch (stage) {
        case ShaderStageUVE::Vertex:
            return "vert";
        case ShaderStageUVE::Fragment:
            return "frag";
        case ShaderStageUVE::Compute:
            return "comp";
        case ShaderStageUVE::Geometry:
            return "geom";
    }
    return "";
}

struct CookedArtifactFormatUVE {
    const char* target = nullptr;
    const char* extension = nullptr;
};

[[nodiscard]] std::optional<CookedArtifactFormatUVE> GetCookedArtifactFormatUVE(
    const IRenderDeviceUVE& renderDevice) noexcept {
    const std::string_view backend = renderDevice.GetBackendNameUVE();
    if (backend.starts_with("Vulkan")) {
#if defined(__ANDROID__)
        return CookedArtifactFormatUVE{"android-vulkan", ".spv"};
#else
        return CookedArtifactFormatUVE{"vulkan", ".spv"};
#endif
    }
#if defined(__ANDROID__)
    if (backend == "OpenGL") {
        return CookedArtifactFormatUVE{"gles", ".glsl"};
    }
#else
    if (backend == "OpenGL") {
        return CookedArtifactFormatUVE{"opengl", ".glsl"};
    }
#endif
    if (backend.starts_with("D3D12")) {
        return CookedArtifactFormatUVE{"d3d12", ".hlsl"};
    }
    if (backend.starts_with("Metal")) {
        return CookedArtifactFormatUVE{"metal", ".metal"};
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<std::string> GetCookedArtifactVariantUVE(
    const ShaderSourceCompileDescUVE& desc) {
    if (desc.extraDefines.empty()) {
        return std::string{};
    }
    // Built-in instancing is the first supported cooked variant. Unknown per-request defines
    // must never reuse the base artifact: a mismatched SPIR-V module is worse than a logged
    // source-compile failure. Additional variants get an explicit directory key here rather than
    // smuggling defines into a filename or relying on unordered map iteration.
    if (desc.extraDefines.size() == 1U && desc.extraDefines.front().first == "UVE_INSTANCED" &&
        (desc.extraDefines.front().second == "1" || desc.extraDefines.front().second == "true")) {
        return std::string{"instanced"};
    }
    return std::nullopt;
}

[[nodiscard]] std::vector<std::string> GetCookedArtifactDefinesUVE(
    ShaderStageUVE stage, const ShaderSourceCompileDescUVE& desc) {
    std::vector<std::string> defines;
    switch (stage) {
        case ShaderStageUVE::Vertex:
            defines.emplace_back("VERTEX_SHADER");
            break;
        case ShaderStageUVE::Fragment:
            defines.emplace_back("FRAGMENT_SHADER");
            break;
        case ShaderStageUVE::Compute:
            defines.emplace_back("COMPUTE_SHADER");
            break;
        case ShaderStageUVE::Geometry:
            defines.emplace_back("GEOMETRY_SHADER");
            break;
    }
    for (const auto& [name, value] : desc.extraDefines) {
        static_cast<void>(value);
        defines.push_back(name);
    }
    return defines;
}

[[nodiscard]] std::vector<std::string> GetCookedArtifactTargetDefinesUVE(std::string_view target) {
    if (target == "vulkan" || target == "android-vulkan") {
        return {"UVE_VULKAN"};
    }
    if (target == "gles") {
        return {"UVE_GLES"};
    }
    return {};
}

[[nodiscard]] std::string GetAuthoringSourceFingerprintUVE(
    ShaderManagerUVE::ImplUVE& impl, const ShaderSourceCompileDescUVE& desc) {
    std::string source = desc.embeddedFallbackSourceCode;
    if (!desc.virtualFilePath.empty() && impl.fileSystem.HasFileUVE(desc.virtualFilePath)) {
        const std::optional<std::vector<std::byte>> sourceBytes = impl.fileSystem.ReadFileUVE(desc.virtualFilePath);
        if (sourceBytes.has_value()) {
            source.assign(reinterpret_cast<const char*>(sourceBytes->data()), sourceBytes->size());
        }
    }
    const std::uint64_t hash = Detail::ComputeFnv1aHashUVE(source);
    constexpr char kHexDigits[] = "0123456789abcdef";
    std::string fingerprint(16U, '0');
    for (std::size_t index = 0U; index < fingerprint.size(); ++index) {
        const std::size_t shift = (fingerprint.size() - 1U - index) * 4U;
        fingerprint[index] = kHexDigits[(hash >> shift) & 0x0FU];
    }
    return fingerprint;
}

[[nodiscard]] bool ValidateCookedArtifactManifestUVE(
    ShaderManagerUVE::ImplUVE& impl, const ShaderSourceCompileDescUVE& desc,
    const CookedArtifactFormatUVE& format, std::string_view stageDirectory,
    std::string_view artifactVirtualPath) {
    const std::size_t lastSlash = artifactVirtualPath.rfind('/');
    if (lastSlash == std::string_view::npos) {
        return false;
    }
    const std::string manifestVirtualPath =
        std::string(artifactVirtualPath.substr(0, lastSlash)) + "/shader_manifest.json";
    if (!impl.fileSystem.HasFileUVE(manifestVirtualPath)) {
        return false;
    }
    const std::optional<std::vector<std::byte>> manifestBytes = impl.fileSystem.ReadFileUVE(manifestVirtualPath);
    if (!manifestBytes.has_value() || manifestBytes->empty()) {
        return false;
    }

    try {
        const std::string manifestText(reinterpret_cast<const char*>(manifestBytes->data()), manifestBytes->size());
        const nlohmann::json manifest = nlohmann::json::parse(manifestText);
        if (manifest.value("format", 0) != 3 || !manifest.contains("toolchain") ||
            !manifest.at("toolchain").is_object() || !manifest.contains("artifacts") ||
            !manifest.at("artifacts").is_array()) {
            return false;
        }
        const nlohmann::json& toolchain = manifest.at("toolchain");
        if (!toolchain.contains("glslang_validator") || !toolchain.contains("spirv_cross") ||
            !toolchain.at("glslang_validator").is_object() || !toolchain.at("spirv_cross").is_object()) {
            return false;
        }

        const std::vector<std::string> expectedDefines = GetCookedArtifactDefinesUVE(desc.stage, desc);
        const std::vector<std::string> expectedTargetDefines =
            GetCookedArtifactTargetDefinesUVE(format.target);
        const std::string expectedEntryPoint = desc.entryPointName.empty() ? "main" : desc.entryPointName;
        const std::string expectedSourceFingerprint = GetAuthoringSourceFingerprintUVE(impl, desc);
        const std::filesystem::path expectedArtifactName(artifactVirtualPath);

        for (const nlohmann::json& artifact : manifest.at("artifacts")) {
            if (!artifact.is_object() || artifact.value("stage", "") != stageDirectory ||
                artifact.value("entry_point", "") != expectedEntryPoint ||
                artifact.value("source_fnv1a64", "") != expectedSourceFingerprint ||
                !artifact.contains("source_sha256") || !artifact.at("source_sha256").is_string()) {
                continue;
            }
            if (artifact.value("defines", nlohmann::json::array()) != expectedDefines) {
                continue;
            }
            const nlohmann::json targetDefines =
                artifact.value("target_defines", nlohmann::json::object());
            if (!targetDefines.is_object() ||
                targetDefines.value(format.target, nlohmann::json::array()) != expectedTargetDefines) {
                continue;
            }
            const nlohmann::json artifacts = artifact.value("artifacts", nlohmann::json::object());
            if (!artifacts.is_object() || !artifacts.contains(format.target) ||
                !artifacts.at(format.target).is_string()) {
                continue;
            }
            const std::filesystem::path declaredArtifactName(artifacts.at(format.target).get<std::string>());
            if (declaredArtifactName.filename() != expectedArtifactName.filename()) {
                continue;
            }
            return true;
        }
    } catch (const nlohmann::json::exception&) {
        return false;
    }
    return false;
}

[[nodiscard]] std::optional<CookedShaderArtifactUVE> TryReadCookedArtifactUVE(
    ShaderManagerUVE::ImplUVE& impl, const ShaderSourceCompileDescUVE& desc) {
    if (!impl.config.preferCookedArtifactsUVE || impl.config.cookedArtifactMountPrefixUVE.empty() ||
        desc.virtualFilePath.empty()) {
        return std::nullopt;
    }
    const std::optional<std::string> variant = GetCookedArtifactVariantUVE(desc);
    if (!variant.has_value()) {
        return std::nullopt;
    }
    const std::optional<CookedArtifactFormatUVE> format = GetCookedArtifactFormatUVE(impl.renderDevice);
    if (variant->compare("instanced") == 0 &&
        !impl.renderDevice.GetCapabilitiesUVE().supportsStorageBuffers) {
        // The named shadow variant reads SSBOs. GLES 3.0 and the current Android fallback
        // deliberately report no storage-buffer capability, so selecting its ESSL 3.10 text
        // artifact would compile but bind nothing (BindStorageBufferUVE is a no-op there). Let
        // the caller's ordinary source/fallback path choose the non-instanced shadow program.
        return std::nullopt;
    }
    const char* const stageDirectory = ShaderArtifactStageDirectoryUVE(desc.stage);
    if (!format.has_value() || stageDirectory[0] == '\0') {
        return std::nullopt;
    }
    const std::filesystem::path sourcePath(desc.virtualFilePath);
    const std::string stem = sourcePath.stem().string();
    if (stem.empty() || stem == "." || stem == "..") {
        return std::nullopt;
    }
    std::string virtualPath = impl.config.cookedArtifactMountPrefixUVE;
    if (!virtualPath.empty() && virtualPath.back() != '/') {
        virtualPath += '/';
    }
    virtualPath += stem;
    virtualPath += '/';
    if (!variant->empty()) {
        virtualPath += *variant;
        virtualPath += '/';
    }
    virtualPath += stageDirectory;
    virtualPath += '/';
    virtualPath += stem;
    virtualPath += '.';
    virtualPath += format->target;
    virtualPath += format->extension;

    // HasFileUVE is intentional here: ReadFileUVE logs a hard error for a missing mount, but a
    // missing cooked tree is the normal source-only development fallback and must stay quiet.
    if (!impl.fileSystem.HasFileUVE(virtualPath) ||
        !ValidateCookedArtifactManifestUVE(impl, desc, *format, stageDirectory, virtualPath)) {
        // A present-but-stale or malformed artifact is treated exactly like a missing one. The
        // source path below remains the safe fallback, while the manifest prevents a build-tree
        // artifact from silently outliving its authoring source or target policy.
        return std::nullopt;
    }
    const std::optional<std::vector<std::byte>> bytes = impl.fileSystem.ReadFileUVE(virtualPath);
    if (!bytes.has_value() || bytes->empty()) {
        return std::nullopt;
    }
    const std::size_t lastSlash = virtualPath.rfind('/');
    const std::string manifestVirtualPath = lastSlash == std::string::npos
        ? std::string{}
        : virtualPath.substr(0, lastSlash) + "/shader_manifest.json";
    return CookedShaderArtifactUVE{std::move(virtualPath), manifestVirtualPath, *bytes};
}

[[nodiscard]] std::filesystem::file_time_type GetRealFileWriteTimeUVE(Asset::IFileSystemUVE& fileSystem,
                                                                       const std::string& virtualPath) {
    const std::filesystem::path realPath = fileSystem.ResolveRealPathUVE(virtualPath);
    std::error_code errorCode;
    const auto writeTime = std::filesystem::last_write_time(realPath, errorCode);
    return errorCode ? std::filesystem::file_time_type{} : writeTime;
}

[[nodiscard]] std::uint64_t ComputeSourceContentHashUVE(IRenderDeviceUVE& renderDevice, const std::string& resolvedSource,
                                                         ShaderStageUVE stage, const std::string& entryPoint) {
    std::string combined = resolvedSource;
    combined += '|';
    combined += ShaderStageDefineNameUVE(stage);
    combined += '|';
    combined += entryPoint;
    combined += '|';
    combined += std::string(renderDevice.GetBackendNameUVE());
    return Detail::ComputeFnv1aHashUVE(combined);
}

[[nodiscard]] std::uint64_t ComputeProgramContentHashUVE(const ShaderSourceUVE& vertexSource,
                                                          const ShaderSourceUVE& fragmentSource) {
    const std::string combined =
        std::to_string(vertexSource.GetContentHashUVE()) + "|" + std::to_string(fragmentSource.GetContentHashUVE());
    return Detail::ComputeFnv1aHashUVE(combined);
}

[[nodiscard]] ShaderSourceCompileDescUVE NormalizeProgramStageDescUVE(ShaderSourceCompileDescUVE desc,
                                                                        ShaderStageUVE stage,
                                                                        const std::string& programDebugName) {
    desc.stage = stage;
    // Dependency tracking happens at the linked-program level (the union of both stage closures),
    // so individual child sources never create competing tracking entries.
    desc.hotReloadEnabledUVE = false;
    if (desc.debugNameUVE.empty()) {
        desc.debugNameUVE = programDebugName;
    }
    desc.debugNameUVE += stage == ShaderStageUVE::Vertex ? " (vertex)" : " (fragment)";
    return desc;
}

[[nodiscard]] ShaderManagerUVE::ImplUVE::ProgramRequestDescUVE BuildProgramRequestDescUVE(
    const ShaderProgramDescUVE& desc) {
    ShaderManagerUVE::ImplUVE::ProgramRequestDescUVE request;
    request.debugNameUVE = desc.debugNameUVE;
    request.vertexSource = NormalizeProgramStageDescUVE(
        ShaderSourceCompileDescUVE{ShaderStageUVE::Vertex, desc.virtualFilePath, desc.embeddedFallbackSourceCode,
                                   desc.extraDefines, desc.entryPointName, false, {}},
        ShaderStageUVE::Vertex, request.debugNameUVE);
    request.fragmentSource = NormalizeProgramStageDescUVE(
        ShaderSourceCompileDescUVE{ShaderStageUVE::Fragment, desc.virtualFilePath, desc.embeddedFallbackSourceCode,
                                   desc.extraDefines, desc.entryPointName, false, {}},
        ShaderStageUVE::Fragment, request.debugNameUVE);
    request.vertexLayout = desc.vertexLayout;
    request.vertexStride = desc.vertexStride;
    request.topology = desc.topology;
    request.depthTestEnabled = desc.depthTestEnabled;
    request.depthWriteEnabled = desc.depthWriteEnabled;
    request.blendMode = desc.blendMode;
    request.hotReloadEnabledUVE = desc.hotReloadEnabledUVE;
    return request;
}

[[nodiscard]] ShaderManagerUVE::ImplUVE::ProgramRequestDescUVE BuildProgramRequestDescUVE(
    const ShaderProgramStagesDescUVE& desc) {
    ShaderManagerUVE::ImplUVE::ProgramRequestDescUVE request;
    request.debugNameUVE = desc.debugNameUVE;
    request.vertexSource = NormalizeProgramStageDescUVE(desc.vertexSource, ShaderStageUVE::Vertex, request.debugNameUVE);
    request.fragmentSource =
        NormalizeProgramStageDescUVE(desc.fragmentSource, ShaderStageUVE::Fragment, request.debugNameUVE);
    request.vertexLayout = desc.vertexLayout;
    request.vertexStride = desc.vertexStride;
    request.topology = desc.topology;
    request.depthTestEnabled = desc.depthTestEnabled;
    request.depthWriteEnabled = desc.depthWriteEnabled;
    request.hotReloadEnabledUVE = desc.hotReloadEnabledUVE;
    return request;
}

} // namespace

std::shared_ptr<ShaderSourceUVE> ShaderManagerUVE::MakeSourceUVE(IRenderDeviceUVE& renderDevice, ShaderStageUVE stage) {
    auto* const renderDevicePtr = &renderDevice;
    std::shared_ptr<ShaderSourceUVE> source(new ShaderSourceUVE(), [renderDevicePtr](ShaderSourceUVE* pointer) {
        if (pointer->m_valid) {
            renderDevicePtr->DestroyShaderUVE(pointer->m_handle);
        }
        delete pointer;
    });
    source->m_stage = stage;
    return source;
}

std::shared_ptr<ShaderProgramUVE> ShaderManagerUVE::MakeProgramUVE(IRenderDeviceUVE& renderDevice) {
    auto* const renderDevicePtr = &renderDevice;
    std::shared_ptr<ShaderProgramUVE> program(new ShaderProgramUVE(), [renderDevicePtr](ShaderProgramUVE* pointer) {
        if (pointer->m_valid) {
            renderDevicePtr->DestroyPipelineUVE(pointer->m_pipeline);
        }
        delete pointer;
    });
    return program;
}

void ShaderManagerUVE::SubmitSourceCompileJobUVE(ImplUVE& impl, const std::shared_ptr<ShaderSourceUVE>& target,
                                                  const ShaderSourceCompileDescUVE& desc) {
    {
        std::lock_guard<std::mutex> lock(impl.mutex);
        ++impl.pendingJobCount;
    }
    impl.threadPool.SubmitUVE(
        [&impl, target, desc]() {
            std::optional<CookedShaderArtifactUVE> cookedArtifact = TryReadCookedArtifactUVE(impl, desc);
            Detail::PreprocessResultUVE preprocess;
            if (cookedArtifact.has_value()) {
                // RHI ShaderDescUVE deliberately uses one byte-preserving string field for both
                // GL text and Vulkan SPIR-V. Keep the artifact bytes intact; an embedded NUL is
                // valid in the Vulkan path and GL artifacts are ordinary UTF-8 text.
                preprocess.success = true;
                preprocess.resolvedSource.assign(
                    reinterpret_cast<const char*>(cookedArtifact->bytes.data()), cookedArtifact->bytes.size());
                preprocess.dependencyClosure.push_back(cookedArtifact->virtualPath);
                preprocess.fileIndexTable.push_back(cookedArtifact->virtualPath);
                if (!cookedArtifact->manifestVirtualPath.empty()) {
                    preprocess.dependencyClosure.push_back(cookedArtifact->manifestVirtualPath);
                    preprocess.fileIndexTable.push_back(cookedArtifact->manifestVirtualPath);
                }
                if (!desc.virtualFilePath.empty() && impl.fileSystem.HasFileUVE(desc.virtualFilePath)) {
                    // Keep authoring-source changes visible in a development mount. A changed
                    // source invalidates the manifest fingerprint on the next job and safely
                    // selects source fallback until the cooked package is rebuilt.
                    preprocess.dependencyClosure.push_back(desc.virtualFilePath);
                    preprocess.fileIndexTable.push_back(desc.virtualFilePath);
                }
            } else {
                const std::vector<std::pair<std::string, std::string>> defines =
                    BuildDefinesUVE(desc.stage, impl.config.injectDebugDefineUVE, desc.extraDefines);
                preprocess = Detail::PreprocessShaderSourceUVE(
                    impl.fileSystem, desc.virtualFilePath, desc.embeddedFallbackSourceCode, defines);
            }

            std::lock_guard<std::mutex> lock(impl.mutex);
            impl.completedSourceJobs.push_back(
                ImplUVE::SourceJobUVE{target, desc, std::move(preprocess), std::move(cookedArtifact)});
        },
        impl.pendingJobs);
}

void ShaderManagerUVE::DrainCompletedSourceJobsUVE(ImplUVE& impl) {
    std::vector<ImplUVE::SourceJobUVE> completedJobs;
    {
        std::lock_guard<std::mutex> lock(impl.mutex);
        completedJobs.swap(impl.completedSourceJobs);
        impl.pendingJobCount -= completedJobs.size();
    }

    for (auto& job : completedJobs) {
        ShaderSourceUVE& source = *job.target;

        if (!job.preprocess.success) {
            source.m_ready = true;
            source.m_valid = false; // A failed hot-reload preprocess leaves any prior valid handle untouched.
            source.m_diagnostics = ShaderCompileDiagnosticsUVE{false, {}, job.preprocess.errorMessage};
            impl.eventSystem.QueueEvent(
                ShaderCompileFailedEventUVE{job.desc.debugNameUVE, job.desc.stage, source.m_diagnostics});
        } else {
            std::string infoLog;
            const ShaderHandleUVE newHandle = impl.renderDevice.CreateShaderUVE(
                ShaderDescUVE{job.desc.stage, job.preprocess.resolvedSource, job.desc.entryPointName}, &infoLog);
            const bool succeeded = (newHandle != kInvalidShaderHandleUVE);
            const std::vector<ShaderCompileErrorUVE> diagnosticsList =
                Detail::ParseGlInfoLogUVE(infoLog, job.preprocess.fileIndexTable);

            if (succeeded) {
                if (source.m_valid) {
                    // Hot-reload swap: the new shader compiled successfully, so it's safe to
                    // destroy the old one now - never before a replacement is confirmed live.
                    impl.renderDevice.DestroyShaderUVE(source.m_handle);
                }
                source.m_handle = newHandle;
                source.m_resolvedSource = job.preprocess.resolvedSource;
                source.m_usedCookedArtifact = job.cookedArtifact.has_value();
                source.m_cookedArtifactVirtualPath = job.cookedArtifact.has_value()
                    ? job.cookedArtifact->virtualPath
                    : std::string{};
                source.m_contentHash = ComputeSourceContentHashUVE(impl.renderDevice, job.preprocess.resolvedSource,
                                                                    job.desc.stage, job.desc.entryPointName);
                source.m_dependencyClosure = job.preprocess.dependencyClosure;
                source.m_valid = true;
            }
            // else: leave the prior handle/valid/resolvedSource untouched - a program using this
            // source keeps rendering its last-known-good state rather than going dark on a typo.

            source.m_ready = true;
            source.m_diagnostics = ShaderCompileDiagnosticsUVE{succeeded, diagnosticsList, infoLog};

            if (!succeeded) {
                impl.eventSystem.QueueEvent(
                    ShaderCompileFailedEventUVE{job.desc.debugNameUVE, job.desc.stage, source.m_diagnostics});
            }
        }

        if (job.desc.hotReloadEnabledUVE && !job.preprocess.dependencyClosure.empty()) {
            bool found = false;
            for (auto& tracked : impl.trackedSources) {
                if (tracked.target.lock() == job.target) {
                    tracked.desc = job.desc;
                    tracked.dependencies.dependencyClosure = job.preprocess.dependencyClosure;
                    for (const std::string& path : job.preprocess.dependencyClosure) {
                        if (!tracked.dependencies.lastKnownWriteTimes.contains(path)) {
                            tracked.dependencies.lastKnownWriteTimes[path] =
                                GetRealFileWriteTimeUVE(impl.fileSystem, path);
                        }
                    }
                    found = true;
                    break;
                }
            }
            if (!found) {
                ImplUVE::TrackedSourceUVE entry;
                entry.target = job.target;
                entry.desc = job.desc;
                entry.dependencies.dependencyClosure = job.preprocess.dependencyClosure;
                for (const std::string& path : job.preprocess.dependencyClosure) {
                    entry.dependencies.lastKnownWriteTimes[path] = GetRealFileWriteTimeUVE(impl.fileSystem, path);
                }
                impl.trackedSources.push_back(std::move(entry));
            }
        }
    }
}

void ShaderManagerUVE::ApplyPendingProgramLinksUVE(ImplUVE& impl) {
    std::vector<ImplUVE::PendingProgramLinkUVE> stillPendingLinks;
    for (auto& pending : impl.pendingProgramLinks) {
        if (!(pending.vertexSource->IsReadyUVE() && pending.fragmentSource->IsReadyUVE())) {
            stillPendingLinks.push_back(std::move(pending));
            continue;
        }

        ShaderProgramUVE& program = *pending.program;
        const bool stagesValid = pending.vertexSource->IsValidUVE() && pending.fragmentSource->IsValidUVE();

        bool linkSucceeded = false;
        std::vector<ShaderCompileErrorUVE> diagnosticsList;
        std::string infoLog;

        if (stagesValid) {
            const std::uint64_t programHash =
                ComputeProgramContentHashUVE(*pending.vertexSource, *pending.fragmentSource);
            const std::filesystem::path cacheFilePath =
                Detail::GetCacheFilePathUVE(impl.config.cachePath, programHash);
            const PipelineBinaryDescUVE binaryDesc{pending.desc.vertexLayout, pending.desc.vertexStride,
                                                    pending.desc.topology, pending.desc.depthTestEnabled,
                                                    pending.desc.depthWriteEnabled, pending.desc.blendMode};

            PipelineHandleUVE newPipeline = kInvalidPipelineHandleUVE;
            bool usedCache = false;

            const std::optional<Detail::CacheEntryUVE> cacheEntry = Detail::ReadCacheEntryUVE(cacheFilePath);
            if (cacheEntry.has_value()) {
                newPipeline = impl.renderDevice.CreatePipelineFromBinaryUVE(
                    cacheEntry->payload, cacheEntry->glBinaryFormat, binaryDesc);
                usedCache = (newPipeline != kInvalidPipelineHandleUVE);
            }

            if (newPipeline == kInvalidPipelineHandleUVE) {
                PipelineDescUVE pipelineDesc;
                pipelineDesc.vertexShader = pending.vertexSource->GetHandleUVE();
                pipelineDesc.fragmentShader = pending.fragmentSource->GetHandleUVE();
                pipelineDesc.vertexLayout = pending.desc.vertexLayout;
                pipelineDesc.topology = pending.desc.topology;
                pipelineDesc.depthTestEnabled = pending.desc.depthTestEnabled;
                pipelineDesc.depthWriteEnabled = pending.desc.depthWriteEnabled;
                pipelineDesc.blendMode = pending.desc.blendMode;
                pipelineDesc.vertexStride = pending.desc.vertexStride;
                newPipeline = impl.renderDevice.CreatePipelineUVE(pipelineDesc, &infoLog);

                if (newPipeline != kInvalidPipelineHandleUVE) {
                    std::vector<std::byte> binary;
                    std::uint32_t binaryFormat = 0;
                    if (impl.renderDevice.GetPipelineBinaryUVE(newPipeline, binary, binaryFormat)) {
                        // Best-effort: a cache write failure is never fatal, just logged by
                        // WriteCacheEntryUVE itself - the next startup simply recompiles from
                        // source.
                        static_cast<void>(Detail::WriteCacheEntryUVE(cacheFilePath, binaryFormat, binary));
                    }
                }
            }

            impl.lastCompileUsedCache = usedCache;
            diagnosticsList = Detail::ParseGlInfoLogUVE(infoLog, {});
            linkSucceeded = (newPipeline != kInvalidPipelineHandleUVE);

            if (linkSucceeded) {
                if (program.m_valid) {
                    impl.renderDevice.DestroyPipelineUVE(program.m_pipeline);
                }
                program.m_pipeline = newPipeline;
                program.m_uniforms = impl.renderDevice.GetPipelineUniformsUVE(newPipeline);
                program.m_contentHash = programHash;
                program.m_valid = true;
            }
        } else {
            impl.lastCompileUsedCache = false;
            diagnosticsList = pending.vertexSource->GetDiagnosticsUVE().diagnostics;
            const std::vector<ShaderCompileErrorUVE>& fragmentDiagnostics =
                pending.fragmentSource->GetDiagnosticsUVE().diagnostics;
            diagnosticsList.insert(diagnosticsList.end(), fragmentDiagnostics.begin(), fragmentDiagnostics.end());
            infoLog = pending.vertexSource->GetDiagnosticsUVE().rawInfoLog +
                      pending.fragmentSource->GetDiagnosticsUVE().rawInfoLog;
        }

        program.m_ready = true;
        program.m_diagnostics = ShaderCompileDiagnosticsUVE{linkSucceeded, diagnosticsList, infoLog};
        impl.eventSystem.QueueEvent(ShaderProgramReloadedEventUVE{pending.desc.debugNameUVE, linkSucceeded});

        if (pending.desc.hotReloadEnabledUVE) {
            std::vector<std::string> combinedClosure = pending.vertexSource->m_dependencyClosure;
            for (const std::string& path : pending.fragmentSource->m_dependencyClosure) {
                if (std::find(combinedClosure.begin(), combinedClosure.end(), path) == combinedClosure.end()) {
                    combinedClosure.push_back(path);
                }
            }
            if (!combinedClosure.empty()) {
                bool found = false;
                for (auto& tracked : impl.trackedPrograms) {
                    if (tracked.target.lock() == pending.program) {
                        tracked.desc = pending.desc;
                        tracked.dependencies.dependencyClosure = combinedClosure;
                        for (const std::string& path : combinedClosure) {
                            if (!tracked.dependencies.lastKnownWriteTimes.contains(path)) {
                                tracked.dependencies.lastKnownWriteTimes[path] =
                                    GetRealFileWriteTimeUVE(impl.fileSystem, path);
                            }
                        }
                        found = true;
                        break;
                    }
                }
                if (!found) {
                    ImplUVE::TrackedProgramUVE entry;
                    entry.target = pending.program;
                    entry.desc = pending.desc;
                    entry.dependencies.dependencyClosure = combinedClosure;
                    for (const std::string& path : combinedClosure) {
                        entry.dependencies.lastKnownWriteTimes[path] = GetRealFileWriteTimeUVE(impl.fileSystem, path);
                    }
                    impl.trackedPrograms.push_back(std::move(entry));
                }
            }
        }
    }
    impl.pendingProgramLinks = std::move(stillPendingLinks);
}

void ShaderManagerUVE::PollHotReloadUVE(ImplUVE& impl) {
    std::vector<ImplUVE::TrackedSourceUVE> stillTrackedSources;
    for (auto& tracked : impl.trackedSources) {
        std::shared_ptr<ShaderSourceUVE> target = tracked.target.lock();
        if (!target) {
            continue; // Expired - the caller released it; drop the tracking entry.
        }
        bool dirty = false;
        for (const std::string& path : tracked.dependencies.dependencyClosure) {
            const auto currentWriteTime = GetRealFileWriteTimeUVE(impl.fileSystem, path);
            std::filesystem::file_time_type& lastKnown = tracked.dependencies.lastKnownWriteTimes[path];
            if (currentWriteTime != std::filesystem::file_time_type{} && currentWriteTime != lastKnown) {
                dirty = true;
            }
            lastKnown = currentWriteTime;
        }
        if (dirty) {
            SubmitSourceCompileJobUVE(impl, target, tracked.desc);
        }
        stillTrackedSources.push_back(std::move(tracked));
    }
    impl.trackedSources = std::move(stillTrackedSources);

    std::vector<ImplUVE::TrackedProgramUVE> stillTrackedPrograms;
    for (auto& tracked : impl.trackedPrograms) {
        std::shared_ptr<ShaderProgramUVE> target = tracked.target.lock();
        if (!target) {
            continue;
        }
        bool dirty = false;
        for (const std::string& path : tracked.dependencies.dependencyClosure) {
            const auto currentWriteTime = GetRealFileWriteTimeUVE(impl.fileSystem, path);
            std::filesystem::file_time_type& lastKnown = tracked.dependencies.lastKnownWriteTimes[path];
            if (currentWriteTime != std::filesystem::file_time_type{} && currentWriteTime != lastKnown) {
                dirty = true;
            }
            lastKnown = currentWriteTime;
        }
        if (dirty) {
            std::shared_ptr<ShaderSourceUVE> vertexSource = MakeSourceUVE(impl.renderDevice, ShaderStageUVE::Vertex);
            std::shared_ptr<ShaderSourceUVE> fragmentSource =
                MakeSourceUVE(impl.renderDevice, ShaderStageUVE::Fragment);
            SubmitSourceCompileJobUVE(impl, vertexSource, tracked.desc.vertexSource);
            SubmitSourceCompileJobUVE(impl, fragmentSource, tracked.desc.fragmentSource);
            std::lock_guard<std::mutex> lock(impl.mutex);
            impl.pendingProgramLinks.push_back(
                ImplUVE::PendingProgramLinkUVE{target, vertexSource, fragmentSource, tracked.desc});
        }
        stillTrackedPrograms.push_back(std::move(tracked));
    }
    impl.trackedPrograms = std::move(stillTrackedPrograms);
}

ShaderManagerUVE::ShaderManagerUVE(Threading::IThreadPoolUVE& threadPool, Events::IEventSystemUVE& eventSystem,
                                    IRenderDeviceUVE& renderDevice, Asset::IFileSystemUVE& fileSystem,
                                    ShaderManagerConfigUVE config)
    : m_impl(std::make_unique<ImplUVE>(threadPool, eventSystem, renderDevice, fileSystem, std::move(config))) {}

ShaderManagerUVE::~ShaderManagerUVE() {
    // Drains every background job still referencing this ImplUVE, which must never outlive it.
    //
    // This also has to hold the shader deleters: MakeSourceUVE/MakeProgramUVE capture a raw
    // IRenderDeviceUVE*, so a ShaderSourceUVE released after the render device is gone calls a
    // virtual function on a destroyed object. Compile jobs capture their target shared_ptr, so
    // the last reference can be the worker's. ThreadPoolUVE::RunJobUVE therefore releases a
    // job's captured state before decrementing the counter this waits on - without that
    // ordering, this drain returns while a worker still owns a shader, and the deleter fires
    // into a dead device. An earlier revision of this comment claimed the deleters capturing
    // only IRenderDeviceUVE& made a late release safe; that was exactly backwards.
    m_impl->pendingJobs.WaitUVE();
}

std::shared_ptr<ShaderSourceUVE> ShaderManagerUVE::CreateSourceUVE(const ShaderSourceCompileDescUVE& desc) {
    std::shared_ptr<ShaderSourceUVE> target = MakeSourceUVE(m_impl->renderDevice, desc.stage);
    SubmitSourceCompileJobUVE(*m_impl, target, desc);
    return target;
}

std::shared_ptr<ShaderProgramUVE> ShaderManagerUVE::CreateProgramUVE(const ShaderProgramDescUVE& desc) {
    const ImplUVE::ProgramRequestDescUVE request = BuildProgramRequestDescUVE(desc);
    std::shared_ptr<ShaderProgramUVE> program = MakeProgramUVE(m_impl->renderDevice);
    std::shared_ptr<ShaderSourceUVE> vertexSource = MakeSourceUVE(m_impl->renderDevice, ShaderStageUVE::Vertex);
    std::shared_ptr<ShaderSourceUVE> fragmentSource = MakeSourceUVE(m_impl->renderDevice, ShaderStageUVE::Fragment);

    SubmitSourceCompileJobUVE(*m_impl, vertexSource, request.vertexSource);
    SubmitSourceCompileJobUVE(*m_impl, fragmentSource, request.fragmentSource);

    {
        std::lock_guard<std::mutex> lock(m_impl->mutex);
        m_impl->pendingProgramLinks.push_back(
            ImplUVE::PendingProgramLinkUVE{program, vertexSource, fragmentSource, request});
    }
    return program;
}

std::shared_ptr<ShaderProgramUVE> ShaderManagerUVE::CreateProgramFromStagesUVE(
    const ShaderProgramStagesDescUVE& desc) {
    const ImplUVE::ProgramRequestDescUVE request = BuildProgramRequestDescUVE(desc);
    std::shared_ptr<ShaderProgramUVE> program = MakeProgramUVE(m_impl->renderDevice);
    std::shared_ptr<ShaderSourceUVE> vertexSource = MakeSourceUVE(m_impl->renderDevice, ShaderStageUVE::Vertex);
    std::shared_ptr<ShaderSourceUVE> fragmentSource = MakeSourceUVE(m_impl->renderDevice, ShaderStageUVE::Fragment);

    SubmitSourceCompileJobUVE(*m_impl, vertexSource, request.vertexSource);
    SubmitSourceCompileJobUVE(*m_impl, fragmentSource, request.fragmentSource);

    {
        std::lock_guard<std::mutex> lock(m_impl->mutex);
        m_impl->pendingProgramLinks.push_back(
            ImplUVE::PendingProgramLinkUVE{program, vertexSource, fragmentSource, request});
    }
    return program;
}

void ShaderManagerUVE::UpdateUVE(double deltaTimeSeconds) {
    DrainCompletedSourceJobsUVE(*m_impl);
    ApplyPendingProgramLinksUVE(*m_impl);

    if (!m_impl->config.hotReloadEnabledUVE) {
        return;
    }
    m_impl->hotReloadAccumulatorSeconds += deltaTimeSeconds;
    if (m_impl->hotReloadAccumulatorSeconds < m_impl->config.hotReloadPollIntervalSecondsUVE) {
        return;
    }
    m_impl->hotReloadAccumulatorSeconds = 0.0;
    PollHotReloadUVE(*m_impl);
}

std::size_t ShaderManagerUVE::GetPendingJobCountUVE() const noexcept {
    std::lock_guard<std::mutex> lock(m_impl->mutex);
    return m_impl->pendingJobCount;
}

bool ShaderManagerUVE::GetLastCompileUsedCacheUVE() const noexcept {
    return m_impl->lastCompileUsedCache;
}

} // namespace UVE::Render::Shader
