#version 450 core

// A small, real-draw companion to bindless_probe.glsl. It is deliberately a material-shaped
// sampled-texture contract: Vulkan uses the production set-1 fixed array and a push-constant
// texture index, while text backends receive a bounded ordinary sampler fallback. The Vulkan
// artifact is consumed by the real-device pixel proof in Test/RHI/Vulkan; it is not an embedded
// runtime fallback and therefore cannot hide a broken descriptor layout.
#ifdef UVE_VULKAN
#extension GL_EXT_nonuniform_qualifier : require
layout(set = 1, binding = 0) uniform sampler2D uveMaterialTextures[256];
layout(push_constant) uniform UveBindlessMaterialProbeParameters {
    int uTextureIndex;
} uveProbeParameters;
#define UVE_PROBE_TEXTURE_INDEX uveProbeParameters.uTextureIndex
#define UVE_PROBE_SAMPLE(textureCoordinate) \
    texture(uveMaterialTextures[nonuniformEXT(uint(UVE_PROBE_TEXTURE_INDEX))], textureCoordinate)
#else
uniform sampler2D uMaterialTexture;
uniform int uTextureIndex;
#define UVE_PROBE_SAMPLE(textureCoordinate) texture(uMaterialTexture, textureCoordinate)
#endif

#ifdef VERTEX_SHADER
layout(location = 0) in vec2 aPosition;
layout(location = 1) in vec2 aTexCoord;
layout(location = 0) out vec2 vTexCoord;

void main() {
    vTexCoord = aTexCoord;
    gl_Position = vec4(aPosition, 0.0, 1.0);
}
#endif

#ifdef FRAGMENT_SHADER
layout(location = 0) in vec2 vTexCoord;
layout(location = 0) out vec4 FragColor;

void main() {
    FragColor = UVE_PROBE_SAMPLE(vTexCoord);
}
#endif
