#version 450 core

#ifdef UVE_VULKAN
// Vulkan's frame/material values live in one std140 block so the renderer's existing named
// SetUniform* calls update one dynamic-UBO shadow for both stages. The block deliberately keeps
// the light array and cascade arrays: the Vulkan reflection layer flattens their members to the
// same names used by the OpenGL path (uLights[0].type, uLightSpaceMatrices[0], ...).
struct LightUVE {
    int type; // 0 = Directional, 1 = Point, 2 = Spot
    vec3 position;
    vec3 direction;
    vec3 color;
    float intensity;
    float range;
    float spotAngleDegrees;
};

layout(std140, set = 0, binding = 3) uniform UveLitShadowedFrameParameters {
    mat4 uViewProjection;
    mat4 uLightSpaceMatrix;
    mat4 uLightSpaceMatrices[3];
    LightUVE uLights[4];
    vec3 uAmbientColor;
    vec3 uViewPosition;
    vec3 uAlbedoColor;
    float uMetallic;
    float uRoughness;
    vec3 uEmissiveColor;
    int uShadowPcfKernelRadius;
    float uShadowCascadeSplits[3];
    int uShadowCascadeCount;
    float uShadowCascadeBlendRatio;
} uveFrameParameters;
#define uViewProjection uveFrameParameters.uViewProjection
#define uLightSpaceMatrix uveFrameParameters.uLightSpaceMatrix
#define uLightSpaceMatrices uveFrameParameters.uLightSpaceMatrices
#define uLights uveFrameParameters.uLights
#define uAmbientColor uveFrameParameters.uAmbientColor
#define uViewPosition uveFrameParameters.uViewPosition
#define uAlbedoColor uveFrameParameters.uAlbedoColor
#define uMetallic uveFrameParameters.uMetallic
#define uRoughness uveFrameParameters.uRoughness
#define uEmissiveColor uveFrameParameters.uEmissiveColor
#define uShadowPcfKernelRadius uveFrameParameters.uShadowPcfKernelRadius
#define uShadowCascadeSplits uveFrameParameters.uShadowCascadeSplits
#define uShadowCascadeCount uveFrameParameters.uShadowCascadeCount
#define uShadowCascadeBlendRatio uveFrameParameters.uShadowCascadeBlendRatio

#ifdef UVE_INSTANCED
// The instance arrays are separate from the frame block so the same shader supports both the
// per-object fallback and the batched path. Vulkan's contiguous reflected storage slots are
// 0=model, 1=normal matrix, 2=base index; this matches renderer BindStorageBufferUVE calls.
layout(std430, set = 0, binding = 0) readonly buffer InstanceTransformBlock {
    mat4 instanceModels[];
};
layout(std430, set = 0, binding = 1) readonly buffer InstanceNormalTransformBlock {
    mat4 instanceNormalModels[];
};
layout(std430, set = 0, binding = 2) readonly buffer InstanceBaseBlock {
    int uInstanceBaseIndex;
};
#define UVE_LIT_INSTANCE_ID gl_InstanceIndex
#else
layout(std140, set = 0, binding = 0) uniform UveLitShadowedObjectParameters {
    mat4 uModel;
    mat4 uNormalMatrix;
} uveObjectParameters;
#define uModel uveObjectParameters.uModel
#define uNormalMatrix uveObjectParameters.uNormalMatrix
#endif

// Material texture slots 0..2 and shadow slots 3..5 map to descriptor bindings 4..9. The
// Vulkan path uses individual shadow samplers because this RHI's bounded tuple descriptor
// fallback intentionally supports one descriptor per reflected binding, not descriptor arrays.
layout(set = 0, binding = 4) uniform sampler2D uAlbedoTexture;
layout(set = 0, binding = 5) uniform sampler2D uNormalTexture;
layout(set = 0, binding = 6) uniform sampler2D uAOTexture;
layout(set = 0, binding = 7) uniform sampler2D uShadowMapTexture0;
layout(set = 0, binding = 8) uniform sampler2D uShadowMapTexture1;
layout(set = 0, binding = 9) uniform sampler2D uShadowMapTexture2;
#define uShadowMapTexture uShadowMapTexture0
#else
#ifdef VERTEX_SHADER
#ifdef UVE_INSTANCED
// The instanced variant reads its per-object transforms from a storage buffer indexed by
// gl_InstanceID instead of from a uniform set once per draw. That is the entire difference
// between the two variants, and it is why this is a #define rather than a second shader file:
// the lighting below is shared verbatim, so an instanced object and a non-instanced one cannot
// drift apart in how they are lit.
//
// Matrices arrive TRANSPOSED from the host (Matrix4x4UVE is row-major; std430 mat4 is
// column-major), exactly as in mesh_skin.glsl - the same convention, deliberately, so there is
// one rule to remember rather than two.
layout(std430, binding = 0) readonly buffer InstanceTransformBlock {
    mat4 instanceModels[];
};
layout(std430, binding = 1) readonly buffer InstanceNormalTransformBlock {
    mat4 instanceNormalModels[];
};
layout(std430, binding = 2) readonly buffer InstanceBaseBlock {
    int uInstanceBaseIndex;
};
#define UVE_LIT_INSTANCE_ID gl_InstanceID
#else
uniform mat4 uModel;
// Transpose(inverse(uModel)): correctly transforms normals under non-uniform scale, unlike
// uModel itself (which only preserves normal direction for uniform scale / rigid transforms).
// Tangents are still transformed with uModel directly - that IS the correct convention for a
// surface-parameterization vector, unlike a normal.
uniform mat4 uNormalMatrix;
#endif
uniform mat4 uViewProjection;
uniform mat4 uLightSpaceMatrix;
uniform mat4 uLightSpaceMatrices[3];
#else
struct LightUVE {
    int type; // 0 = Directional, 1 = Point, 2 = Spot
    vec3 position;
    vec3 direction;
    vec3 color;
    float intensity;
    float range;
    float spotAngleDegrees;
};

uniform LightUVE uLights[4];
uniform vec3 uAmbientColor;
uniform vec3 uViewPosition;
uniform vec3 uAlbedoColor;
uniform float uMetallic;
uniform float uRoughness;
uniform vec3 uEmissiveColor;
// Legacy Increment 27 pair retained for project-authored shaders and direct single-map tests.
uniform sampler2D uShadowMapTexture;
uniform mat4 uLightSpaceMatrix;
uniform int uShadowPcfKernelRadius;
// Increment 30 fixed three-cascade directional shadow contract.
uniform sampler2D uShadowMapTextures[3];
uniform float uShadowCascadeSplits[3];
uniform int uShadowCascadeCount;
// Increment 31: fraction of each non-final cascade depth interval used to cross-fade into the next.
uniform float uShadowCascadeBlendRatio;
uniform sampler2D uAlbedoTexture;
uniform sampler2D uNormalTexture;
uniform sampler2D uAOTexture;
#endif
#endif

#ifdef VERTEX_SHADER
layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aTexCoord;
layout(location = 3) in vec4 aTangent;

layout(location = 0) out vec3 vWorldPosition;
layout(location = 1) out vec3 vWorldNormal;
layout(location = 2) out vec3 vWorldTangent;
layout(location = 3) out float vTangentHandedness;
layout(location = 4) out vec2 vTexCoord;
layout(location = 5) out vec4 vLightSpacePosition;
layout(location = 6) out vec4 vLightSpacePositions[3];

void main() {
#ifdef UVE_INSTANCED
#ifdef UVE_VULKAN
    int instanceSlot = uInstanceBaseIndex + gl_InstanceIndex;
#else
    int instanceSlot = uInstanceBaseIndex + gl_InstanceID;
#endif
    mat4 model = instanceModels[instanceSlot];
    mat4 normalMatrix = instanceNormalModels[instanceSlot];
#else
    mat4 model = uModel;
    mat4 normalMatrix = uNormalMatrix;
#endif
    vec4 worldPosition = model * vec4(aPosition, 1.0);
    vWorldPosition = worldPosition.xyz;
    vWorldNormal = mat3(normalMatrix) * aNormal;
    vWorldTangent = mat3(model) * aTangent.xyz;
    vTangentHandedness = aTangent.w;
    vTexCoord = aTexCoord;
    vLightSpacePosition = uLightSpaceMatrix * worldPosition;
    for (int cascadeIndex = 0; cascadeIndex < 3; ++cascadeIndex) {
        vLightSpacePositions[cascadeIndex] = uLightSpaceMatrices[cascadeIndex] * worldPosition;
    }
    gl_Position = uViewProjection * worldPosition;
}
#endif

#ifdef FRAGMENT_SHADER
layout(location = 0) in vec3 vWorldPosition;
layout(location = 1) in vec3 vWorldNormal;
layout(location = 2) in vec3 vWorldTangent;
layout(location = 3) in float vTangentHandedness;
layout(location = 4) in vec2 vTexCoord;
layout(location = 5) in vec4 vLightSpacePosition;
layout(location = 6) in vec4 vLightSpacePositions[3];
layout(location = 0) out vec4 FragColor;

#ifndef UVE_VULKAN
// Vulkan declares the three cascade samplers individually above. The OpenGL fallback keeps the
// historical array uniform and its runtime texture-unit contract unchanged.
#endif

const float kPiUVE = 3.14159265359;
const float kBrdfEpsilonUVE = 0.0001;
/// The spot cone's bright inner region as a fraction of its authored outer half-angle; the band
/// between the two is where the light falls off. Derived rather than authored so a material's
/// existing single spotAngleDegrees keeps meaning exactly what it meant.
const float kSpotInnerConeRatioUVE = 0.85;

vec3 SafeNormalizeUVE(vec3 value) {
    return value / max(length(value), kBrdfEpsilonUVE);
}

float DistributionGgxUVE(float normalDotHalf, float roughness) {
    float alpha = roughness * roughness;
    float alphaSquared = alpha * alpha;
    float normalDotHalfSquared = normalDotHalf * normalDotHalf;
    float denominator = normalDotHalfSquared * (alphaSquared - 1.0) + 1.0;
    return alphaSquared / max(kPiUVE * denominator * denominator, kBrdfEpsilonUVE);
}

float GeometrySchlickGgxUVE(float normalDotDirection, float roughness) {
    float alpha = roughness * roughness;
    float k = ((alpha + 1.0) * (alpha + 1.0)) * 0.125;
    return normalDotDirection / max(normalDotDirection * (1.0 - k) + k, kBrdfEpsilonUVE);
}

float GeometrySmithUVE(float normalDotView, float normalDotLight, float roughness) {
    return GeometrySchlickGgxUVE(normalDotView, roughness) *
           GeometrySchlickGgxUVE(normalDotLight, roughness);
}

vec3 FresnelSchlickUVE(float halfDotView, vec3 baseReflectance) {
    return baseReflectance + (vec3(1.0) - baseReflectance) * pow(1.0 - halfDotView, 5.0);
}

float SampleCascadeDepthUVE(int cascadeIndex, vec2 texCoord) {
#ifdef UVE_VULKAN
    if (cascadeIndex == 0) {
        return texture(uShadowMapTexture0, texCoord).r;
    }
    if (cascadeIndex == 1) {
        return texture(uShadowMapTexture1, texCoord).r;
    }
    return texture(uShadowMapTexture2, texCoord).r;
#else
    if (cascadeIndex == 0) {
        return texture(uShadowMapTextures[0], texCoord).r;
    }
    if (cascadeIndex == 1) {
        return texture(uShadowMapTextures[1], texCoord).r;
    }
    return texture(uShadowMapTextures[2], texCoord).r;
#endif
}

vec2 CascadeTexelSizeUVE(int cascadeIndex) {
#ifdef UVE_VULKAN
    if (cascadeIndex == 0) {
        return 1.0 / vec2(textureSize(uShadowMapTexture0, 0));
    }
    if (cascadeIndex == 1) {
        return 1.0 / vec2(textureSize(uShadowMapTexture1, 0));
    }
    return 1.0 / vec2(textureSize(uShadowMapTexture2, 0));
#else
    if (cascadeIndex == 0) {
        return 1.0 / vec2(textureSize(uShadowMapTextures[0], 0));
    }
    if (cascadeIndex == 1) {
        return 1.0 / vec2(textureSize(uShadowMapTextures[1], 0));
    }
    return 1.0 / vec2(textureSize(uShadowMapTextures[2], 0));
#endif
}

vec4 CascadeLightSpacePositionUVE(int cascadeIndex) {
    if (cascadeIndex == 0) {
        return vLightSpacePositions[0];
    }
    if (cascadeIndex == 1) {
        return vLightSpacePositions[1];
    }
    return vLightSpacePositions[2];
}

float ShadowFactorFromPositionUVE(vec4 lightSpacePosition, vec3 normal, vec3 lightDirection, int cascadeIndex) {
    if (abs(lightSpacePosition.w) <= 0.0001) {
        return 1.0;
    }

    vec3 projected = lightSpacePosition.xyz / lightSpacePosition.w;
    projected = projected * 0.5 + 0.5;
    if (projected.x <= 0.0 || projected.x >= 1.0 || projected.y <= 0.0 || projected.y >= 1.0 ||
        projected.z <= 0.0 || projected.z >= 1.0) {
        return 1.0;
    }

    int kernelRadius = clamp(uShadowPcfKernelRadius, 0, 2);
    vec2 texelSize = cascadeIndex < 0 ? 1.0 / vec2(textureSize(uShadowMapTexture, 0))
                                      : CascadeTexelSizeUVE(cascadeIndex);
    float currentDepth = projected.z;
    float bias = max(0.0025 * (1.0 - max(dot(normal, lightDirection), 0.0)), 0.0005);
    float visibleSamples = 0.0;
    int sampleCount = 0;

    for (int offsetY = -2; offsetY <= 2; ++offsetY) {
        for (int offsetX = -2; offsetX <= 2; ++offsetX) {
            if (abs(offsetX) > kernelRadius || abs(offsetY) > kernelRadius) {
                continue;
            }
            vec2 sampleCoord = projected.xy + vec2(offsetX, offsetY) * texelSize;
            float sampledDepth = cascadeIndex < 0 ? texture(uShadowMapTexture, sampleCoord).r
                                                  : SampleCascadeDepthUVE(cascadeIndex, sampleCoord);
            visibleSamples += currentDepth - bias > sampledDepth ? 0.0 : 1.0;
            ++sampleCount;
        }
    }

    return visibleSamples / float(sampleCount);
}

float DirectionalShadowFactorUVE(vec3 normal, vec3 lightDirection) {
    if (uShadowCascadeCount <= 0) {
        return ShadowFactorFromPositionUVE(vLightSpacePosition, normal, lightDirection, -1);
    }

    float viewDepth = length(vWorldPosition - uViewPosition);
    int cascadeIndex = clamp(uShadowCascadeCount - 1, 0, 2);
    for (int candidateIndex = 0; candidateIndex < 2; ++candidateIndex) {
        if (candidateIndex < uShadowCascadeCount && viewDepth <= uShadowCascadeSplits[candidateIndex]) {
            cascadeIndex = candidateIndex;
            break;
        }
    }
    float shadowFactor = ShadowFactorFromPositionUVE(CascadeLightSpacePositionUVE(cascadeIndex), normal,
                                                      lightDirection, cascadeIndex);
    int finalCascadeIndex = clamp(uShadowCascadeCount - 1, 0, 2);
    if (cascadeIndex >= finalCascadeIndex) {
        return shadowFactor;
    }

    float cascadeNearDepth = cascadeIndex == 0 ? 0.0 : uShadowCascadeSplits[cascadeIndex - 1];
    float cascadeFarDepth = uShadowCascadeSplits[cascadeIndex];
    float cascadeDepthRange = max(cascadeFarDepth - cascadeNearDepth, 0.0001);
    float blendWidth = cascadeDepthRange * clamp(uShadowCascadeBlendRatio, 0.0, 0.25);
    float blendStartDepth = cascadeFarDepth - blendWidth;
    if (blendWidth <= 0.0 || viewDepth <= blendStartDepth) {
        return shadowFactor;
    }

    float nextCascadeShadowFactor = ShadowFactorFromPositionUVE(
        CascadeLightSpacePositionUVE(cascadeIndex + 1), normal, lightDirection, cascadeIndex + 1);
    float blendWeight = smoothstep(blendStartDepth, cascadeFarDepth, viewDepth);
    return mix(shadowFactor, nextCascadeShadowFactor, blendWeight);
}

void main() {
    vec3 albedo = texture(uAlbedoTexture, vTexCoord).rgb * uAlbedoColor;
    float ambientOcclusion = texture(uAOTexture, vTexCoord).r;
    vec3 normal = SafeNormalizeUVE(vWorldNormal);
    vec3 tangent = vWorldTangent - normal * dot(normal, vWorldTangent);
    if (dot(tangent, tangent) <= 0.00000001) {
        vec3 fallbackAxis = abs(normal.y) < 0.999 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
        tangent = cross(fallbackAxis, normal);
    }
    tangent = SafeNormalizeUVE(tangent);
    vec3 bitangent = SafeNormalizeUVE(cross(normal, tangent));
    bitangent *= vTangentHandedness < 0.0 ? -1.0 : 1.0;
    vec3 tangentSpaceNormal = texture(uNormalTexture, vTexCoord).xyz * 2.0 - 1.0;
    normal = SafeNormalizeUVE(mat3(tangent, bitangent, normal) * tangentSpaceNormal);
    vec3 viewDirection = SafeNormalizeUVE(uViewPosition - vWorldPosition);
    float metallic = clamp(uMetallic, 0.0, 1.0);
    float roughness = clamp(uRoughness, 0.04, 1.0);
    // Ambient, split into diffuse and specular exactly as the direct term is.
    //
    // The old ambient was purely diffuse: albedo * ambient * ao. For a dielectric that is roughly
    // right, but a metal has NO diffuse response at all - its diffuseWeight is (1-F)(1-metallic),
    // which is zero at metallic 1 - so a metal lit only by ambient came out BLACK. That is the
    // single most visible way a correct BRDF still looks wrong, and it is why "the PBR looks
    // broken" usually means "there is no ambient specular".
    //
    // This is not image-based lighting: there is no environment cubemap here, so the ambient
    // colour stands in for the environment's average radiance. What it does buy is the right
    // ENERGY SPLIT - a metal reflects the ambient tinted by its own albedo, a dielectric reflects
    // about 4% of it untinted, and both lose energy to roughness. A real IBL probe replaces the
    // source of that radiance later without changing this structure.
    vec3 ambientBaseReflectance = mix(vec3(0.04), albedo, metallic);
    float normalDotViewAmbient = max(dot(normal, viewDirection), 0.0);
    // Roughness-aware Fresnel: the standard Schlick term goes to white at grazing angles, which on
    // a rough surface produces a bright rim that should not be there - the microfacets point in
    // too many directions to reflect coherently. Clamping the ceiling by (1 - roughness) is the
    // usual, cheap correction.
    vec3 ambientFresnel =
        ambientBaseReflectance +
        (max(vec3(1.0 - roughness), ambientBaseReflectance) - ambientBaseReflectance) *
            pow(1.0 - normalDotViewAmbient, 5.0);
    vec3 ambientDiffuseWeight = (vec3(1.0) - ambientFresnel) * (1.0 - metallic);
    vec3 ambientDiffuse = ambientDiffuseWeight * albedo * uAmbientColor;
    vec3 ambientSpecular = ambientFresnel * uAmbientColor;
    // AO occludes both terms. Applying it to the diffuse alone is a common shortcut, but a crevice
    // does not stop reflecting light in a way the sky can reach either.
    vec3 lighting = (ambientDiffuse + ambientSpecular) * ambientOcclusion + uEmissiveColor;

    for (int lightIndex = 0; lightIndex < 4; ++lightIndex) {
        LightUVE light = uLights[lightIndex];
        if (light.intensity <= 0.0) {
            continue;
        }

        vec3 lightDirection;
        float attenuation = 1.0;
        if (light.type == 0) {
            lightDirection = SafeNormalizeUVE(-light.direction);
        } else {
            vec3 toLight = light.position - vWorldPosition;
            float distanceToLight = max(length(toLight), 0.0001);
            lightDirection = toLight / distanceToLight;
            attenuation = 1.0 / max(distanceToLight * distanceToLight, 0.0001);
            if (light.range > 0.0 && distanceToLight > light.range) {
                attenuation = 0.0;
            }
            if (light.type == 2) {
                // Smooth cone falloff rather than a binary in/out test. A hard cutoff produces a
                // jagged, aliased cone edge that no amount of MSAA fixes, because the edge is in
                // the shading rather than in the geometry. The inner cone is derived from the
                // authored outer angle rather than adding a second uniform - one authored angle
                // stays one authored angle, and the material contract does not change.
                float cosOuter = cos(radians(light.spotAngleDegrees));
                float cosInner = cos(radians(light.spotAngleDegrees) * kSpotInnerConeRatioUVE);
                float coneAlignment = dot(-lightDirection, SafeNormalizeUVE(light.direction));
                // max() guards the degenerate case where the two cosines coincide (a zero-width
                // falloff band), which would otherwise divide by zero.
                float coneFalloff = clamp((coneAlignment - cosOuter) /
                                              max(cosInner - cosOuter, kBrdfEpsilonUVE),
                                          0.0, 1.0);
                // Squared so the falloff is smooth in perceived brightness rather than linear in
                // cosine, which reads as a visible ring at the transition.
                attenuation *= coneFalloff * coneFalloff;
            }
        }

        float normalDotLight = max(dot(normal, lightDirection), 0.0);
        float normalDotView = max(dot(normal, viewDirection), 0.0);
        if (normalDotLight <= 0.0 || normalDotView <= 0.0 || attenuation <= 0.0) {
            continue;
        }

        vec3 halfDirection = SafeNormalizeUVE(lightDirection + viewDirection);
        float normalDotHalf = max(dot(normal, halfDirection), 0.0);
        float halfDotView = max(dot(halfDirection, viewDirection), 0.0);
        vec3 baseReflectance = mix(vec3(0.04), albedo, metallic);
        vec3 fresnel = FresnelSchlickUVE(halfDotView, baseReflectance);
        float distribution = DistributionGgxUVE(normalDotHalf, roughness);
        float geometry = GeometrySmithUVE(normalDotView, normalDotLight, roughness);
        vec3 specular = (distribution * geometry * fresnel) /
                        max(4.0 * normalDotView * normalDotLight, kBrdfEpsilonUVE);
        vec3 diffuseWeight = (vec3(1.0) - fresnel) * (1.0 - metallic);
        vec3 diffuse = diffuseWeight * albedo / kPiUVE;
        vec3 radiance = light.color * light.intensity * attenuation;
        vec3 directContribution = (diffuse + specular) * radiance * normalDotLight;

        if (light.type == 0) {
            directContribution *= DirectionalShadowFactorUVE(normal, lightDirection);
        }
        lighting += directContribution;
    }

    FragColor = vec4(lighting, 1.0);
}
#endif
