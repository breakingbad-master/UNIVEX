#version 450 core

#ifdef UVE_VULKAN
#define UVE_BINDLESS_PROBE_CAPACITY 256
#else
// Non-Vulkan output is a compiler/capability smoke fixture, not the native Vulkan table. Keep
// it within the minimum ES 3.1 image-array limits so the generated GLES artifact remains valid
// on the low-end fallback tier.
#define UVE_BINDLESS_PROBE_CAPACITY 4
#endif

// B1 shader-contract fixture. This is intentionally small and deterministic: the optional
// artifact target compiles it for every backend so descriptor set 1 / fixed-array reflection is
// exercised independently of a platform's final native shader compiler. It is not a production
// material; the renderer may use it for capability/validation smoke tests.
#ifdef UVE_VULKAN
#extension GL_EXT_nonuniform_qualifier : require

layout(set = 1, binding = 0) uniform sampler2D uveSampledTextures[UVE_BINDLESS_PROBE_CAPACITY];
layout(set = 1, binding = 1, rgba8) uniform image2D uveStorageTextures[UVE_BINDLESS_PROBE_CAPACITY];
layout(set = 1, binding = 2) buffer UveStorageBuffer {
    uint values[];
} uveStorageBuffers[UVE_BINDLESS_PROBE_CAPACITY];

layout(push_constant) uniform UveBindlessProbeParameters {
    uint sampledTextureSlot;
    uint storageTextureSlot;
    uint storageBufferSlot;
} uveProbe;

#define UVE_BINDLESS_INDEX(index) nonuniformEXT(index)
#else
// OpenGL/GLES and text-backend artifacts use the same logical binding numbers without Vulkan's
// descriptor-set/push-constant syntax. The GLES 3.1 fallback compiler does not permit variable
// sampler-array indexing without a vendor extension, so its probe uses a deterministic element 0;
// the Vulkan branch remains the native non-uniform descriptor-indexing contract.
#ifdef UVE_GLES
#define UVE_BINDLESS_INDEX(index) 0u
#else
#define UVE_BINDLESS_INDEX(index) (index)
#endif
layout(binding = 0) uniform sampler2D uveSampledTextures[UVE_BINDLESS_PROBE_CAPACITY];
layout(binding = 1, rgba8) uniform image2D uveStorageTextures[UVE_BINDLESS_PROBE_CAPACITY];
layout(binding = 2) buffer UveStorageBuffer {
    uint values[];
} uveStorageBuffers[UVE_BINDLESS_PROBE_CAPACITY];

layout(std140, binding = 3) uniform UveBindlessProbeParameters {
    uint sampledTextureSlot;
    uint storageTextureSlot;
    uint storageBufferSlot;
} uveProbe;

#endif

layout(local_size_x = 1, local_size_y = 1, local_size_z = 1) in;

void main() {
    const uint sampledSlot = UVE_BINDLESS_INDEX(uveProbe.sampledTextureSlot);
    const uint storageTextureSlot = UVE_BINDLESS_INDEX(uveProbe.storageTextureSlot);
    const uint storageBufferSlot = UVE_BINDLESS_INDEX(uveProbe.storageBufferSlot);
    const vec4 sampled = texture(uveSampledTextures[sampledSlot], vec2(0.5));
    imageStore(uveStorageTextures[storageTextureSlot], ivec2(0, 0), sampled);
    uveStorageBuffers[storageBufferSlot].values[0] = floatBitsToUint(sampled.r);
}
