#version 450 core

#ifdef UVE_VULKAN
#ifdef UVE_INSTANCED
// Vulkan uses contiguous reflected storage slots: renderer slot 0 is the model array and
// renderer slot 1 is the base-index scalar. The OpenGL fallback retains its historical sparse
// declarations below; its RHI maps the same logical slots to the reflected physical binding
// points before glBindBufferBase.
layout(std430, set = 0, binding = 0) readonly buffer InstanceTransformBlock {
    mat4 instanceModels[];
};
layout(std430, set = 0, binding = 1) readonly buffer InstanceBaseBlock {
    int uInstanceBaseIndex;
};
layout(std140, set = 0, binding = 3) uniform UveShadowDepthFrameParameters {
    mat4 uLightSpaceMatrix;
} uveFrameParameters;
#define uLightSpaceMatrix uveFrameParameters.uLightSpaceMatrix
#define UVE_SHADOW_INSTANCE_ID gl_InstanceIndex
#else
layout(std140, set = 0, binding = 0) uniform UveShadowDepthParameters {
    mat4 uModel;
    mat4 uLightSpaceMatrix;
} uveParameters;
#define uModel uveParameters.uModel
#define uLightSpaceMatrix uveParameters.uLightSpaceMatrix
#endif
#else
#ifdef UVE_INSTANCED
// The instanced shadow variant, mirroring lit_shadowed_3d.glsl's arrangement: same define name,
// same binding 0, same transposed upload convention, same uInstanceBaseIndex + gl_InstanceID
// indexing. One rule across both shaders rather than two.
//
// Only the model matrix is needed here - a depth-only pass has no normals to transform - so this
// binds one buffer where the lit shader binds three. The base-index buffer is still required
// because gl_InstanceID restarts at zero for every draw while the frame shares one upload.
layout(std430, binding = 0) readonly buffer InstanceTransformBlock {
    mat4 instanceModels[];
};
layout(std430, binding = 2) readonly buffer InstanceBaseBlock {
    int uInstanceBaseIndex;
};
#define UVE_SHADOW_INSTANCE_ID gl_InstanceID
#else
uniform mat4 uModel;
#endif
uniform mat4 uLightSpaceMatrix;
#endif

#ifdef VERTEX_SHADER
layout(location = 0) in vec3 aPosition;

void main() {
#ifdef UVE_INSTANCED
    mat4 model = instanceModels[uInstanceBaseIndex + UVE_SHADOW_INSTANCE_ID];
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
