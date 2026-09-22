#version 450 core

#ifdef UVE_VULKAN
layout(push_constant) uniform UveBloomBrightParameters {
    float uBloomThreshold;
} uveParameters;
#define uBloomThreshold uveParameters.uBloomThreshold
#else
uniform float uBloomThreshold;
#endif

#ifdef VERTEX_SHADER
// Fullscreen triangle via the vertex-ID trick: no vertex buffer is required.
layout(location = 0) out vec2 vTexCoord;

#ifdef UVE_VULKAN
#define UVE_FULLSCREEN_VERTEX_ID gl_VertexIndex
#else
#define UVE_FULLSCREEN_VERTEX_ID gl_VertexID
#endif

void main() {
    // position is (0,0), (2,0), (0,2) - the oversized triangle that covers clip space once
    // gl_Position maps it with position*2-1. The texture coordinate must use the inverse of
    // that same mapping, (ndc+1)/2 == position, so the visible NDC range [-1,1] samples the
    // full [0,1] of the source. Halving it here would sample only the source's lower-left
    // quarter and magnify it across the whole target.
    vec2 position = vec2((UVE_FULLSCREEN_VERTEX_ID << 1) & 2, UVE_FULLSCREEN_VERTEX_ID & 2);
    vTexCoord = position;
    gl_Position = vec4(position * 2.0 - 1.0, 0.0, 1.0);
}
#endif

#ifdef FRAGMENT_SHADER
layout(location = 0) in vec2 vTexCoord;
layout(location = 0) out vec4 FragColor;

#ifdef UVE_VULKAN
layout(set = 0, binding = 0) uniform sampler2D uSourceTexture;
#else
uniform sampler2D uSourceTexture;
#endif

void main() {
    vec3 hdrColor = max(texture(uSourceTexture, vTexCoord).rgb, vec3(0.0));
    float luminance = dot(hdrColor, vec3(0.2126, 0.7152, 0.0722));
    float contribution = max(luminance - uBloomThreshold, 0.0) / max(luminance, 0.0001);
    FragColor = vec4(hdrColor * contribution, 1.0);
}
#endif
