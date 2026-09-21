// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <vector>

#include "uve/rhi/buffer_handle_uve.h"
#include "uve/rhi/texture_handle_uve.h"

namespace UVE::Render {

inline constexpr std::uint32_t kInvalidBindlessResourceSlotUVE = 0xFFFFFFFFU;

// Native shader contract shared by every descriptor-indexing backend.  Set 0 remains available
// for backend-neutral uniforms/legacy tuple bindings; set 1 is reserved for these fixed arrays.
// Fixed arrays keep the optional tier usable without requiring runtimeDescriptorArray on Vulkan
// or an unbounded heap declaration on D3D12/Metal. A backend may expose a smaller effective
// capacity only by declining the native tier and using the bounded fallback.
inline constexpr std::uint32_t kNativeBindlessDescriptorSetUVE = 1U;
inline constexpr std::uint32_t kNativeBindlessSampledTextureBindingUVE = 0U;
inline constexpr std::uint32_t kNativeBindlessStorageTextureBindingUVE = 1U;
inline constexpr std::uint32_t kNativeBindlessStorageBufferBindingUVE = 2U;
inline constexpr std::uint32_t kNativeBindlessResourceArrayCapacityUVE = 256U;

/// A stable logical resource reference passed to material/compute code.  The generation is
/// checked on every resolve, so a slot recycled after destruction cannot turn a stale shader
/// parameter into an unrelated texture or storage buffer.
struct BindlessResourceHandleUVE {
    std::uint32_t slot = kInvalidBindlessResourceSlotUVE;
    std::uint32_t generation = 0U;
};

[[nodiscard]] constexpr bool operator==(const BindlessResourceHandleUVE& lhs,
                                        const BindlessResourceHandleUVE& rhs) noexcept {
    return lhs.slot == rhs.slot && lhs.generation == rhs.generation;
}

[[nodiscard]] constexpr bool operator!=(const BindlessResourceHandleUVE& lhs,
                                        const BindlessResourceHandleUVE& rhs) noexcept {
    return !(lhs == rhs);
}

enum class BindlessResourceKindUVE : std::uint8_t {
    SampledTexture,
    StorageTexture,
    StorageBuffer,
};

/// Capacity of each logical descriptor array.  Native descriptor-indexing backends may use
/// these values as their descriptor-array limits; fallback backends use the same bounded table so
/// shader/material code observes identical allocation, stale-handle, and exhaustion behavior.
struct BindlessResourceTableDescUVE {
    std::uint32_t maxSampledTextures = 4096U;
    std::uint32_t maxStorageTextures = 1024U;
    std::uint32_t maxStorageBuffers = 1024U;
};

/// Backend-neutral lifetime/slot contract for bindless resources.  This class intentionally does
/// not issue native API calls: a Vulkan/D3D12/Metal implementation mirrors these slots into a
/// native descriptor heap, while OpenGL/low-tier devices use the same handles to select the
/// existing bounded binding fallback. Keeping generation validation here prevents each backend
/// from inventing a subtly different use-after-free policy.
class BindlessResourceTableUVE final {
public:
    explicit BindlessResourceTableUVE(BindlessResourceTableDescUVE desc = {});
    ~BindlessResourceTableUVE();

    BindlessResourceTableUVE(const BindlessResourceTableUVE&) = delete;
    BindlessResourceTableUVE& operator=(const BindlessResourceTableUVE&) = delete;
    BindlessResourceTableUVE(BindlessResourceTableUVE&&) noexcept;
    BindlessResourceTableUVE& operator=(BindlessResourceTableUVE&&) noexcept;

    [[nodiscard]] BindlessResourceHandleUVE RegisterSampledTextureUVE(TextureHandleUVE texture);
    [[nodiscard]] BindlessResourceHandleUVE RegisterStorageTextureUVE(TextureHandleUVE texture);
    [[nodiscard]] BindlessResourceHandleUVE RegisterStorageBufferUVE(BufferHandleUVE buffer);

    [[nodiscard]] bool UpdateSampledTextureUVE(BindlessResourceHandleUVE handle,
                                               TextureHandleUVE texture);
    [[nodiscard]] bool UpdateStorageTextureUVE(BindlessResourceHandleUVE handle,
                                               TextureHandleUVE texture);
    [[nodiscard]] bool UpdateStorageBufferUVE(BindlessResourceHandleUVE handle,
                                              BufferHandleUVE buffer);

    /// Releases a slot.  The slot may be reused, but its generation is advanced first; all old
    /// handles therefore fail the resolve calls below.
    [[nodiscard]] bool ReleaseUVE(BindlessResourceKindUVE kind, BindlessResourceHandleUVE handle);

    [[nodiscard]] std::optional<TextureHandleUVE> ResolveSampledTextureUVE(
        BindlessResourceHandleUVE handle) const;
    [[nodiscard]] std::optional<TextureHandleUVE> ResolveStorageTextureUVE(
        BindlessResourceHandleUVE handle) const;
    [[nodiscard]] std::optional<BufferHandleUVE> ResolveStorageBufferUVE(
        BindlessResourceHandleUVE handle) const;

    /// Returns the backend-facing array index for a live handle, or the invalid sentinel for a
    /// stale/foreign handle.  Native descriptor-indexing backends use this as the shader index;
    /// fallback backends can translate it into their compact per-draw binding table.
    [[nodiscard]] std::uint32_t ResolveSlotUVE(BindlessResourceKindUVE kind,
                                                BindlessResourceHandleUVE handle) const noexcept;

    [[nodiscard]] std::uint32_t GetCapacityUVE(BindlessResourceKindUVE kind) const noexcept;
    [[nodiscard]] std::uint32_t GetLiveCountUVE(BindlessResourceKindUVE kind) const noexcept;

private:
    struct TextureSlotUVE {
        TextureHandleUVE texture{};
        std::uint32_t generation = 1U;
        bool live = false;
    };
    struct BufferSlotUVE {
        BufferHandleUVE buffer{};
        std::uint32_t generation = 1U;
        bool live = false;
    };

    [[nodiscard]] BindlessResourceHandleUVE RegisterTextureUVE(std::vector<TextureSlotUVE>& slots,
                                                                TextureHandleUVE texture);
    [[nodiscard]] BindlessResourceHandleUVE RegisterBufferUVE(std::vector<BufferSlotUVE>& slots,
                                                               BufferHandleUVE buffer);
    [[nodiscard]] bool IsLiveTextureHandleUVE(const std::vector<TextureSlotUVE>& slots,
                                               BindlessResourceHandleUVE handle) const noexcept;
    [[nodiscard]] bool IsLiveBufferHandleUVE(const std::vector<BufferSlotUVE>& slots,
                                              BindlessResourceHandleUVE handle) const noexcept;
    [[nodiscard]] bool ReleaseTextureUVE(std::vector<TextureSlotUVE>& slots,
                                         BindlessResourceHandleUVE handle);
    [[nodiscard]] bool ReleaseBufferUVE(std::vector<BufferSlotUVE>& slots,
                                        BindlessResourceHandleUVE handle);

    std::vector<TextureSlotUVE> m_sampledTextures;
    std::vector<TextureSlotUVE> m_storageTextures;
    std::vector<BufferSlotUVE> m_storageBuffers;
    std::uint32_t m_sampledTextureLiveCount = 0U;
    std::uint32_t m_storageTextureLiveCount = 0U;
    std::uint32_t m_storageBufferLiveCount = 0U;
};

} // namespace UVE::Render

namespace std {
template <>
struct hash<UVE::Render::BindlessResourceHandleUVE> {
    [[nodiscard]] std::size_t operator()(
        const UVE::Render::BindlessResourceHandleUVE& handle) const noexcept {
        return (std::hash<std::uint32_t>{}(handle.slot) * 16777619U) ^
               std::hash<std::uint32_t>{}(handle.generation);
    }
};
} // namespace std
