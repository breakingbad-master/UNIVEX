#version 450 core

#ifdef UVE_VULKAN
layout(push_constant) uniform UveBasic3DTexturedParameters {
    mat4 uModel;
    mat4 uViewProjection;
} uveParameters;
#define uModel uveParameters.uModel
#define uViewProjection uveParameters.uViewProjection
#else
uniform mat4 uModel;
uniform mat4 uViewProjection;
#endif

#ifdef VERTEX_SHADER
layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec2 aTexCoord;

layout(location = 0) out vec2 vTexCoord;

void main() {
    vTexCoord = aTexCoord;
    gl_Position = uViewProjection * uModel * vec4(aPosition, 1.0);
}
#endif

#ifdef FRAGMENT_SHADER
layout(location = 0) in vec2 vTexCoord;
layout(location = 0) out vec4 FragColor;

// Placeholder only - no CreateTextureUVE-backed binding exists yet this increment
// (MaterialSystemUVE, a future increment, is what actually binds a real texture here).
#ifdef UVE_VULKAN
layout(set = 0, binding = 0) uniform sampler2D uTexture;
#else
uniform sampler2D uTexture;
#endif

void main() {
    FragColor = texture(uTexture, vTexCoord);
}
#endif
