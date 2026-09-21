// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstdint>

namespace UVE::Render {

/// The native graphics family behind an IRenderDeviceUVE.  The value is intentionally
/// backend-neutral: callers use it for capability selection and diagnostics, never to reach
/// through the RHI and issue native calls.
enum class RenderBackendUVE : std::uint8_t {
    Unknown,
    Null,
    OpenGL,
    Vulkan,
    Direct3D12,
    Metal,
};

/// Coarse device tier used by renderer policy.  It is derived from the individual capability
/// bits below, so a backend can expose a truthful low-to-high profile without making callers
/// guess from an API name or a driver version.
enum class RenderFeatureTierUVE : std::uint8_t {
    Low,
    Baseline,
    Enhanced,
    High,
};

/// A snapshot of what one render device can actually do.  This is deliberately a value type:
/// it can be copied into diagnostics, editor overlays, and test reports without keeping a
/// backend object alive.  A capability is true only when the backend has both the API support
/// and the required device feature enabled; merely detecting a newer driver is not enough.
struct RenderDeviceCapabilitiesUVE {
    RenderBackendUVE backend = RenderBackendUVE::Unknown;
    RenderFeatureTierUVE tier = RenderFeatureTierUVE::Low;

    std::uint32_t apiMajor = 0;
    std::uint32_t apiMinor = 0;

    bool supportsGraphics = false;
    bool supportsComputeShaders = false;
    bool supportsStorageBuffers = false;
    bool supportsStorageImages = false;
    bool supportsIndirectDraw = false;
    bool supportsDynamicRendering = false;
    bool supportsMultiThreadedRecording = false;

    /// True when the backend exposes a real descriptor/resource-indexing path.  A backend may
    /// still be fully usable when this is false: callers must use the bounded binding fallback.
    bool supportsBindlessResources = false;
    bool supportsDescriptorIndexing = false;

    /// The limits used by the logical binding fallback and by bindless allocation policy.  Zero
    /// means the backend did not expose a meaningful limit (for example the Null backend).
    std::uint32_t maxSampledTextures = 0;
    std::uint32_t maxStorageBuffers = 0;
    std::uint32_t maxStorageImages = 0;
};

/// Computes the policy tier from the actual capabilities.  This intentionally does not use the
/// backend enum: a Vulkan device without compute or a Metal device without resource indexing must
/// degrade to the same lower tier as any other device with those limits.
[[nodiscard]] constexpr RenderFeatureTierUVE ComputeRenderFeatureTierUVE(
    const RenderDeviceCapabilitiesUVE& capabilities) noexcept {
    if (!capabilities.supportsGraphics) {
        return RenderFeatureTierUVE::Low;
    }
    if (capabilities.supportsBindlessResources && capabilities.supportsDescriptorIndexing &&
        capabilities.supportsComputeShaders && capabilities.supportsStorageBuffers &&
        capabilities.supportsIndirectDraw) {
        return RenderFeatureTierUVE::High;
    }
    if (capabilities.supportsComputeShaders && capabilities.supportsStorageBuffers &&
        capabilities.supportsIndirectDraw) {
        return RenderFeatureTierUVE::Enhanced;
    }
    return RenderFeatureTierUVE::Baseline;
}

[[nodiscard]] constexpr bool IsRenderFeatureTierAtLeastUVE(const RenderFeatureTierUVE actual,
                                                            const RenderFeatureTierUVE required) noexcept {
    return static_cast<std::uint8_t>(actual) >= static_cast<std::uint8_t>(required);
}

} // namespace UVE::Render
