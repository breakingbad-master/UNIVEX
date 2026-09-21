#version 450 core

// One authoring contract, two API layouts: the runtime OpenGL fallback keeps ordinary uniforms,
// while offline Vulkan-family targets define UVE_VULKAN and bind the same values as push constants.
// The member order is intentionally identical across both stages for one portable pipeline range.
#ifdef UVE_VULKAN
layout(push_constant) uniform UveBasic2DParameters {
    mat4 uModel;
    mat4 uProjection;
    vec4 uColor;
} uveParameters;
#define uModel uveParameters.uModel
#define uProjection uveParameters.uProjection
#define uColor uveParameters.uColor
#else
uniform mat4 uModel;
uniform mat4 uProjection;
uniform vec4 uColor;
#endif

#ifdef VERTEX_SHADER
layout(location = 0) in vec2 aPosition;
layout(location = 1) in vec2 aTexCoord;

layout(location = 0) out vec2 vTexCoord;

void main() {
    vTexCoord = aTexCoord;
    gl_Position = uProjection * uModel * vec4(aPosition, 0.0, 1.0);
}
#endif

#ifdef FRAGMENT_SHADER
layout(location = 0) in vec2 vTexCoord;
layout(location = 0) out vec4 FragColor;

void main() {
    FragColor = uColor;
}
#endif
