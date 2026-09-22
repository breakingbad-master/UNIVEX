#version 450 core

// The particle material shares one authoring source across OpenGL and explicit APIs. OpenGL uses
// its existing default-block matrix; the Vulkan-family cooked variants use a push constant with
// the same member name and layout so ShaderProgramUVE::SetMatrix4x4UVE remains backend-neutral.
#ifdef UVE_VULKAN
layout(push_constant) uniform UveParticleParameters {
    mat4 uViewProjection;
} uveParameters;
#define uViewProjection uveParameters.uViewProjection
#else
uniform mat4 uViewProjection;
#endif

#ifdef VERTEX_SHADER
layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec4 aColor;

layout(location = 0) out vec4 vColor;

void main() {
    vColor = aColor;
    gl_Position = uViewProjection * vec4(aPosition, 1.0);
}
#endif

#ifdef FRAGMENT_SHADER
layout(location = 0) in vec4 vColor;

layout(location = 0) out vec4 FragColor;

void main() {
    FragColor = vColor;
}
#endif
