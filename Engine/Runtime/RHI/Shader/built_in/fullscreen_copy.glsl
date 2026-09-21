#version 450 core

#ifdef VERTEX_SHADER
// Fullscreen triangle via the vertex-ID trick: no vertex buffer is required.
layout(location = 0) out vec2 vTexCoord;

#ifdef UVE_VULKAN
#define UVE_FULLSCREEN_VERTEX_ID gl_VertexIndex
#else
#define UVE_FULLSCREEN_VERTEX_ID gl_VertexID
#endif

void main() {
    vec2 position = vec2((UVE_FULLSCREEN_VERTEX_ID << 1) & 2, UVE_FULLSCREEN_VERTEX_ID & 2);
    vTexCoord = position * 0.5;
    gl_Position = vec4(position * 2.0 - 1.0, 0.0, 1.0);
}
#endif

#ifdef FRAGMENT_SHADER
layout(location = 0) in vec2 vTexCoord;
layout(location = 0) out vec4 FragColor;

// Unmodified passthrough: the actual effect is the pipeline's blend mode this shader is used
// with, not anything computed here - Additive to composite the blurred bloom texture onto the
// HDR scene color, Multiply to composite the SSAO occlusion term onto it.
#ifdef UVE_VULKAN
layout(set = 0, binding = 0) uniform sampler2D uSourceTexture;
#else
uniform sampler2D uSourceTexture;
#endif

void main() {
    FragColor = texture(uSourceTexture, vTexCoord);
}
#endif
