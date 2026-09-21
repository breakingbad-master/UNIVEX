# UniVex shader artifact pipeline

`compile_shaders.py` is the build-time bridge from one canonical GLSL source to the native
backends targeted by the RHI:

| Target | Artifact |
|---|---|
| Vulkan / Android Vulkan | SPIR-V (`.spv`) |
| Desktop OpenGL | generated GLSL 4.50 |
| OpenGL ES / Android fallback | generated ESSL 3.10 |
| D3D12 | generated HLSL Shader Model 6.0 |
| Metal / iOS | generated MSL |

The process is deliberately offline at runtime:

1. `glslangValidator` compiles the source once to SPIR-V.
2. `spirv-cross` emits the target-specific textual artifact, except Vulkan, which consumes the
   intermediate SPIR-V directly.
3. The tool writes a manifest containing the source SHA-256, target list, compiler paths, and
   compiler version strings. Release builds can archive that manifest beside their cooked shader
   package and reject an artifact generated from a different source hash.

Example:

```sh
python3 Engine/Tools/compile_shaders.py \
  Engine/Runtime/RHI/Shader/built_in/frustum_cull.glsl \
  --stage comp \
  --target vulkan \
  --target opengl \
  --target gles \
  --target d3d12 \
  --target metal \
  --out-dir build/shaders/frustum_cull
```

The compiler executables are intentionally discovered when the tool is invoked. A platform SDK
is therefore required only for the targets that a developer or CI job requests. The shipped
runtime never depends on either executable.

## Optional native bindless convention

The backend-neutral contract reserves descriptor set 1 for a native bindless tier. Set 0 keeps
its existing uniform and bounded tuple/fallback bindings, which lets a material migrate without
requiring every device to expose descriptor indexing. A Vulkan shader that opts into the tier uses
fixed arrays of exactly 256 entries:

```glsl
#extension GL_EXT_nonuniform_qualifier : require

layout(set = 1, binding = 0) uniform sampler2D uveSampledTextures[256];
layout(set = 1, binding = 1, rgba8) uniform image2D uveStorageTextures[256];
layout(set = 1, binding = 2) buffer UveStorageBuffer {
    uint values[];
} uveStorageBuffers[256];

vec4 sampleMaterialTexture(uint slot, vec2 uv) {
    return texture(uveSampledTextures[nonuniformEXT(slot)], uv);
}
```

The native Vulkan device enables this set only when descriptor indexing, the three relevant
non-uniform array-indexing features, partially-bound descriptors, and the Vulkan descriptor-count
limits for the fixed arrays are all available. The
`rgba8` storage-image array accepts native `RGBA8Unorm` textures only; depth and RGBA16Float
textures intentionally return the invalid slot and use the bounded storage fallback because one
Vulkan descriptor array cannot mix shader image formats safely. Resource creation publishes a
slot through `IRenderDeviceUVE::GetBindless*SlotUVE`; a returned
`kInvalidBindlessResourceSlotUVE` is not a valid shader index and must select the bounded
`BindTextureUVE`/`BindStorageBufferUVE` path instead. The arrays are deliberately fixed-size: the
Vulkan implementation does not require `runtimeDescriptorArray` or update-after-bind, and
D3D12/Metal implementations can map the same source-level convention onto a bounded descriptor
heap or argument buffer. Native color images use GENERAL as their descriptor rest layout so a
storage-image index is valid without an untracked first-use transition. Destruction replaces
entries with deterministic white/black/zero sinks before recycling a slot.

The non-Vulkan textual artifacts are a portability aid, not proof that every target has a native
bindless implementation yet: generated HLSL/MSL still requires the target backend's final
compiler and resource-layout validation. The fallback binding path remains mandatory for low-tier
Vulkan, OpenGL/GLES, and any device whose native descriptor capacity is exhausted.

Current limitations are explicit: the existing shader manager still consumes its current
backend-specific source/binary fields, and the default CMake configure keeps artifact generation
off so a Null/OpenGL-only checkout does not require every compiler and SDK. Release/CI configs
can enable `UVE_BUILD_BUILTIN_SHADER_ARTIFACTS=ON` and build the aggregate
`uve_builtin_shader_artifacts` target; that target currently covers the four compute built-ins
used by the GPU workload proofs. Migration of the remaining built-in graphics shaders and
platform-native final compilation/validation remain open. The tool itself is already usable as a
reproducible, target-aware artifact generator.
