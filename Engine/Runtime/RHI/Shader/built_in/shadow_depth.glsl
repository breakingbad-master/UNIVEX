#version 450 core

#ifdef VERTEX_SHADER
layout(location = 0) in vec3 aPosition;

#ifdef UVE_VULKAN
layout(std140, set = 0, binding = 3) uniform UveShadowDepthParameters {
#ifdef UVE_INSTANCED
    mat4 uLightSpaceMatrix;
#else
    mat4 uModel;
    mat4 uLightSpaceMatrix;
#endif
} uveParameters;
#define uLightSpaceMatrix uveParameters.uLightSpaceMatrix
#ifndef UVE_INSTANCED
#define uModel uveParameters.uModel
#endif
#else
#ifndef UVE_INSTANCED
uniform mat4 uModel;
#endif
uniform mat4 uLightSpaceMatrix;
#endif

#ifdef UVE_INSTANCED
// The instanced shadow variant, mirroring lit_shadowed_3d.glsl's arrangement: same define name,
// same transposed upload convention, same uInstanceBaseIndex + instance-index semantics. One rule
// across both shaders rather than two. The shadow program has two storage bindings (0 and 2), so
// the host binds the base-index buffer to logical storage slot 2; the descriptor binding is
// intentionally sparse so it cannot collide with the uniform block at binding 3.
#ifdef UVE_VULKAN
layout(std430, set = 0, binding = 0) readonly buffer InstanceTransformBlock {
#else
layout(std430, binding = 0) readonly buffer InstanceTransformBlock {
#endif
    mat4 instanceModels[];
};
#ifdef UVE_VULKAN
layout(std430, set = 0, binding = 2) readonly buffer InstanceBaseBlock {
#else
layout(std430, binding = 2) readonly buffer InstanceBaseBlock {
#endif
    int uInstanceBaseIndex;
};

#endif

void main() {
#ifdef UVE_INSTANCED
#ifdef UVE_VULKAN
    mat4 model = instanceModels[uInstanceBaseIndex + gl_InstanceIndex];
#else
    mat4 model = instanceModels[uInstanceBaseIndex + gl_InstanceID];
#endif
#else
    mat4 model = uModel;
#endif
    gl_Position = uLightSpaceMatrix * model * vec4(aPosition, 1.0);
}
#endif

#ifdef FRAGMENT_SHADER
void main() {
    // Depth-only pass: no color attachment bound, nothing to write.
}
#endif
