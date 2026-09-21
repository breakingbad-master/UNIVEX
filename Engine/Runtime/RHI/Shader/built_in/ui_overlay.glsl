#version 450 core

#ifdef UVE_VULKAN
layout(push_constant) uniform UveUiOverlayParameters {
    mat4 uProjection;
} uveParameters;
#define uProjection uveParameters.uProjection
#else
uniform mat4 uProjection;
#endif

#ifdef VERTEX_SHADER
layout(location = 0) in vec2 aPosition;
layout(location = 1) in vec2 aTexCoord;
layout(location = 2) in vec4 aColor;

layout(location = 0) out vec2 vTexCoord;
layout(location = 1) out vec4 vColor;


void main() {
    vTexCoord = aTexCoord;
    vColor = aColor;
    gl_Position = uProjection * vec4(aPosition, 0.0, 1.0);
}
#endif

#ifdef FRAGMENT_SHADER
layout(location = 0) in vec2 vTexCoord;
layout(location = 1) in vec4 vColor;

layout(location = 0) out vec4 FragColor;

#ifdef UVE_VULKAN
layout(set = 0, binding = 0) uniform sampler2D uSourceTexture;
#else
uniform sampler2D uSourceTexture;
#endif

void main() {
    FragColor = texture(uSourceTexture, vTexCoord) * vColor;
}
#endif
