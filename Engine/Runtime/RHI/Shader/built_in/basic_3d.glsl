#version 450 core

// One authoring contract, two API layouts: the runtime OpenGL fallback keeps ordinary uniforms,
// while offline Vulkan-family targets define UVE_VULKAN and bind the same values as push constants.
// The member order is intentionally identical across both stages for one portable pipeline range.
#ifdef UVE_VULKAN
layout(push_constant) uniform UveBasic3DParameters {
    mat4 uModel;
    mat4 uViewProjection;
    vec3 uColor;
} uveParameters;
#define uModel uveParameters.uModel
#define uViewProjection uveParameters.uViewProjection
#define uColor uveParameters.uColor
#else
uniform mat4 uModel;
uniform mat4 uViewProjection;
uniform vec3 uColor;
#endif

#ifdef VERTEX_SHADER
layout(location = 0) in vec3 aPosition;

void main() {
    gl_Position = uViewProjection * uModel * vec4(aPosition, 1.0);
}
#endif

#ifdef FRAGMENT_SHADER
layout(location = 0) out vec4 FragColor;

void main() {
    FragColor = vec4(uColor, 1.0);
}
#endif
