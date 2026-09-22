// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/rhi/bindless_resource_table_uve.h"

#include <limits>
#include <utility>

namespace UVE::Render {
namespace {

[[nodiscard]] constexpr bool IsValidTextureUVE(const TextureHandleUVE texture) noexcept {
    return texture != kInvalidTextureHandleUVE;
}

[[nodiscard]] constexpr bool IsValidBufferUVE(const BufferHandleUVE buffer) noexcept {
    return buffer != kInvalidBufferHandleUVE;
}

void AdvanceGenerationUVE(std::uint32_t& generation) noexcept {
    ++generation;
    // Generation zero is reserved for an invalid/default handle.  Wrapping is unavoidable for a
    // 32-bit value, but skipping zero keeps the default value from becoming accidentally live.
    if (generation == 0U) {
        generation = 1U;
    }
}

} // namespace

BindlessResourceTableUVE::BindlessResourceTableUVE(const BindlessResourceTableDescUVE desc)
    : m_sampledTextures(desc.maxSampledTextures),
      m_storageTextures(desc.maxStorageTextures),
      m_storageBuffers(desc.maxStorageBuffers) {}

BindlessResourceTableUVE::~BindlessResourceTableUVE() = default;

BindlessResourceTableUVE::BindlessResourceTableUVE(BindlessResourceTableUVE&&) noexcept = default;

BindlessResourceTableUVE& BindlessResourceTableUVE::operator=(BindlessResourceTableUVE&&) noexcept = default;

BindlessResourceHandleUVE BindlessResourceTableUVE::RegisterTextureUVE(
    std::vector<TextureSlotUVE>& slots, const TextureHandleUVE texture) {
    if (!IsValidTextureUVE(texture)) {
        return {};
    }
    for (std::uint32_t slot = 0U; slot < slots.size(); ++slot) {
        TextureSlotUVE& record = slots[slot];
        if (!record.live) {
            record.texture = texture;
            record.live = true;
            return BindlessResourceHandleUVE{slot, record.generation};
        }
    }
    return {};
}

BindlessResourceHandleUVE BindlessResourceTableUVE::RegisterBufferUVE(
    std::vector<BufferSlotUVE>& slots, const BufferHandleUVE buffer) {
    if (!IsValidBufferUVE(buffer)) {
        return {};
    }
    for (std::uint32_t slot = 0U; slot < slots.size(); ++slot) {
        BufferSlotUVE& record = slots[slot];
        if (!record.live) {
            record.buffer = buffer;
            record.live = true;
            return BindlessResourceHandleUVE{slot, record.generation};
        }
    }
    return {};
}

BindlessResourceHandleUVE BindlessResourceTableUVE::RegisterSampledTextureUVE(
    const TextureHandleUVE texture) {
    const BindlessResourceHandleUVE handle = RegisterTextureUVE(m_sampledTextures, texture);
    if (handle.slot != kInvalidBindlessResourceSlotUVE) {
        ++m_sampledTextureLiveCount;
    }
    return handle;
}

BindlessResourceHandleUVE BindlessResourceTableUVE::RegisterStorageTextureUVE(
    const TextureHandleUVE texture) {
    const BindlessResourceHandleUVE handle = RegisterTextureUVE(m_storageTextures, texture);
    if (handle.slot != kInvalidBindlessResourceSlotUVE) {
        ++m_storageTextureLiveCount;
    }
    return handle;
}

BindlessResourceHandleUVE BindlessResourceTableUVE::RegisterStorageBufferUVE(
    const BufferHandleUVE buffer) {
    const BindlessResourceHandleUVE handle = RegisterBufferUVE(m_storageBuffers, buffer);
    if (handle.slot != kInvalidBindlessResourceSlotUVE) {
        ++m_storageBufferLiveCount;
    }
    return handle;
}

bool BindlessResourceTableUVE::IsLiveTextureHandleUVE(
    const std::vector<TextureSlotUVE>& slots, const BindlessResourceHandleUVE handle) const noexcept {
    return handle.slot != kInvalidBindlessResourceSlotUVE && handle.slot < slots.size() &&
           handle.generation != 0U && slots[handle.slot].live &&
           slots[handle.slot].generation == handle.generation;
}

bool BindlessResourceTableUVE::IsLiveBufferHandleUVE(
    const std::vector<BufferSlotUVE>& slots, const BindlessResourceHandleUVE handle) const noexcept {
    return handle.slot != kInvalidBindlessResourceSlotUVE && handle.slot < slots.size() &&
           handle.generation != 0U && slots[handle.slot].live &&
           slots[handle.slot].generation == handle.generation;
}

bool BindlessResourceTableUVE::UpdateSampledTextureUVE(
    const BindlessResourceHandleUVE handle, const TextureHandleUVE texture) {
    if (!IsValidTextureUVE(texture) || !IsLiveTextureHandleUVE(m_sampledTextures, handle)) {
        return false;
    }
    m_sampledTextures[handle.slot].texture = texture;
    return true;
}

bool BindlessResourceTableUVE::UpdateStorageTextureUVE(
    const BindlessResourceHandleUVE handle, const TextureHandleUVE texture) {
    if (!IsValidTextureUVE(texture) || !IsLiveTextureHandleUVE(m_storageTextures, handle)) {
        return false;
    }
    m_storageTextures[handle.slot].texture = texture;
    return true;
}

bool BindlessResourceTableUVE::UpdateStorageBufferUVE(
    const BindlessResourceHandleUVE handle, const BufferHandleUVE buffer) {
    if (!IsValidBufferUVE(buffer) || !IsLiveBufferHandleUVE(m_storageBuffers, handle)) {
        return false;
    }
    m_storageBuffers[handle.slot].buffer = buffer;
    return true;
}

bool BindlessResourceTableUVE::ReleaseTextureUVE(
    std::vector<TextureSlotUVE>& slots, const BindlessResourceHandleUVE handle) {
    if (!IsLiveTextureHandleUVE(slots, handle)) {
        return false;
    }
    TextureSlotUVE& record = slots[handle.slot];
    record.texture = kInvalidTextureHandleUVE;
    record.live = false;
    AdvanceGenerationUVE(record.generation);
    return true;
}

bool BindlessResourceTableUVE::ReleaseBufferUVE(
    std::vector<BufferSlotUVE>& slots, const BindlessResourceHandleUVE handle) {
    if (!IsLiveBufferHandleUVE(slots, handle)) {
        return false;
    }
    BufferSlotUVE& record = slots[handle.slot];
    record.buffer = kInvalidBufferHandleUVE;
    record.live = false;
    AdvanceGenerationUVE(record.generation);
    return true;
}

bool BindlessResourceTableUVE::ReleaseUVE(const BindlessResourceKindUVE kind,
                                          const BindlessResourceHandleUVE handle) {
    switch (kind) {
        case BindlessResourceKindUVE::SampledTexture:
            if (ReleaseTextureUVE(m_sampledTextures, handle)) {
                --m_sampledTextureLiveCount;
                return true;
            }
            return false;
        case BindlessResourceKindUVE::StorageTexture:
            if (ReleaseTextureUVE(m_storageTextures, handle)) {
                --m_storageTextureLiveCount;
                return true;
            }
            return false;
        case BindlessResourceKindUVE::StorageBuffer:
            if (ReleaseBufferUVE(m_storageBuffers, handle)) {
                --m_storageBufferLiveCount;
                return true;
            }
            return false;
    }
    return false;
}

std::optional<TextureHandleUVE> BindlessResourceTableUVE::ResolveSampledTextureUVE(
    const BindlessResourceHandleUVE handle) const {
    if (!IsLiveTextureHandleUVE(m_sampledTextures, handle)) {
        return std::nullopt;
    }
    return m_sampledTextures[handle.slot].texture;
}

std::optional<TextureHandleUVE> BindlessResourceTableUVE::ResolveStorageTextureUVE(
    const BindlessResourceHandleUVE handle) const {
    if (!IsLiveTextureHandleUVE(m_storageTextures, handle)) {
        return std::nullopt;
    }
    return m_storageTextures[handle.slot].texture;
}

std::optional<BufferHandleUVE> BindlessResourceTableUVE::ResolveStorageBufferUVE(
    const BindlessResourceHandleUVE handle) const {
    if (!IsLiveBufferHandleUVE(m_storageBuffers, handle)) {
        return std::nullopt;
    }
    return m_storageBuffers[handle.slot].buffer;
}

std::uint32_t BindlessResourceTableUVE::ResolveSlotUVE(
    const BindlessResourceKindUVE kind, const BindlessResourceHandleUVE handle) const noexcept {
    switch (kind) {
        case BindlessResourceKindUVE::SampledTexture:
            return IsLiveTextureHandleUVE(m_sampledTextures, handle)
                       ? handle.slot
                       : kInvalidBindlessResourceSlotUVE;
        case BindlessResourceKindUVE::StorageTexture:
            return IsLiveTextureHandleUVE(m_storageTextures, handle)
                       ? handle.slot
                       : kInvalidBindlessResourceSlotUVE;
        case BindlessResourceKindUVE::StorageBuffer:
            return IsLiveBufferHandleUVE(m_storageBuffers, handle)
                       ? handle.slot
                       : kInvalidBindlessResourceSlotUVE;
    }
    return kInvalidBindlessResourceSlotUVE;
}

std::uint32_t BindlessResourceTableUVE::GetCapacityUVE(const BindlessResourceKindUVE kind) const noexcept {
    switch (kind) {
        case BindlessResourceKindUVE::SampledTexture: return static_cast<std::uint32_t>(m_sampledTextures.size());
        case BindlessResourceKindUVE::StorageTexture: return static_cast<std::uint32_t>(m_storageTextures.size());
        case BindlessResourceKindUVE::StorageBuffer: return static_cast<std::uint32_t>(m_storageBuffers.size());
    }
    return 0U;
}

std::uint32_t BindlessResourceTableUVE::GetLiveCountUVE(const BindlessResourceKindUVE kind) const noexcept {
    switch (kind) {
        case BindlessResourceKindUVE::SampledTexture: return m_sampledTextureLiveCount;
        case BindlessResourceKindUVE::StorageTexture: return m_storageTextureLiveCount;
        case BindlessResourceKindUVE::StorageBuffer: return m_storageBufferLiveCount;
    }
    return 0U;
}

} // namespace UVE::Render
