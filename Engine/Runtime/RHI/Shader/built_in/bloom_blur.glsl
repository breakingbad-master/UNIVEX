#version 450 core

#ifdef UVE_VULKAN
layout(push_constant) uniform UveBloomBlurParameters {
    float uBlurDirectionX;
    float uBlurDirectionY;
    float uTexelSizeX;
    float uTexelSizeY;
} uveParameters;
#define uBlurDirectionX uveParameters.uBlurDirectionX
#define uBlurDirectionY uveParameters.uBlurDirectionY
#define uTexelSizeX uveParameters.uTexelSizeX
#define uTexelSizeY uveParameters.uTexelSizeY
#else
uniform float uBlurDirectionX;
uniform float uBlurDirectionY;
uniform float uTexelSizeX;
uniform float uTexelSizeY;
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
    vec2 position = vec2((UVE_FULLSCREEN_VERTEX_ID << 1) & 2, UVE_FULLSCREEN_VERTEX_ID & 2);
    vTexCoord = position * 0.5;
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
// (1,0) for a horizontal pass, (0,1) for a vertical pass - the same shader serves both halves of
// the separable Gaussian blur, one draw each, ping-ponging between two same-sized targets.
// Individual floats, not a vec2: ShaderProgramUVE currently only exposes Float/Int/Bool/Vec3/Mat4
// uniform setters (see shader_program_uve.h), so this avoids adding a new uniform-value type for
// a single consumer.

void main() {
    // 9-tap Gaussian, weights normalized to sum to 1 (sigma ~= 2 texels).
    const float weights[5] = float[](0.227027, 0.1945946, 0.1216216, 0.054054, 0.016216);
    vec2 direction = vec2(uBlurDirectionX, uBlurDirectionY);
    vec2 texelSize = vec2(uTexelSizeX, uTexelSizeY);
    vec3 result = texture(uSourceTexture, vTexCoord).rgb * weights[0];
    for (int tap = 1; tap < 5; ++tap) {
        vec2 offset = direction * texelSize * float(tap);
        result += texture(uSourceTexture, vTexCoord + offset).rgb * weights[tap];
        result += texture(uSourceTexture, vTexCoord - offset).rgb * weights[tap];
    }
    FragColor = vec4(result, 1.0);
}
#endif
