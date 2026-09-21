#version 450 core

// B1 shader-contract fixture. This is intentionally small and deterministic: the optional
// artifact target compiles it for every backend so descriptor set 1 / fixed-array reflection is
// exercised independently of a platform's final native shader compiler. It is not a production
// material; the renderer may use it for capability/validation smoke tests.
#extension GL_EXT_nonuniform_qualifier : require

layout(local_size_x = 1, local_size_y = 1, local_size_z = 1) in;

layout(set = 1, binding = 0) uniform sampler2D uveSampledTextures[256];
layout(set = 1, binding = 1, rgba8) uniform image2D uveStorageTextures[256];
layout(set = 1, binding = 2) buffer UveStorageBuffer {
    uint values[];
} uveStorageBuffers[256];

layout(push_constant) uniform UveBindlessProbeParameters {
    uint sampledTextureSlot;
    uint storageTextureSlot;
    uint storageBufferSlot;
} uveProbe;

void main() {
    const uint sampledSlot = nonuniformEXT(uveProbe.sampledTextureSlot);
    const uint storageTextureSlot = nonuniformEXT(uveProbe.storageTextureSlot);
    const uint storageBufferSlot = nonuniformEXT(uveProbe.storageBufferSlot);
    const vec4 sampled = texture(uveSampledTextures[sampledSlot], vec2(0.5));
    imageStore(uveStorageTextures[storageTextureSlot], ivec2(0, 0), sampled);
    uveStorageBuffers[storageBufferSlot].values[0] = floatBitsToUint(sampled.r);
}
