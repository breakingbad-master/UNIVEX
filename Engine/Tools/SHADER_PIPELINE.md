# UniVex shader artifact pipeline

`compile_shaders.py` is the build-time bridge from one canonical GLSL source to the native
backends targeted by the RHI:

| Target | Artifact |
|---|---|
| Vulkan / Android Vulkan | SPIR-V (`.spv`) |
| Desktop OpenGL | generated GLSL 4.50 |
| OpenGL ES / Android fallback | generated ESSL 3.00 for ordinary graphics, 3.10 for compute/SSBO variants |
| D3D12 | generated HLSL Shader Model 6.0 |
| Metal / iOS | generated MSL |

The process is deliberately offline at runtime:

1. `glslangValidator` compiles the source to SPIR-V. Vulkan/Android Vulkan targets use Vulkan
   semantics (`-V`) and can select explicit push-constant/descriptor layouts through target-specific
   defines. OpenGL, GLES, D3D12, Metal, and iOS targets use OpenGL semantics (`-G`): default-block
   uniforms remain compatible with `glUniform`, and SPIRV-Cross can preserve nested constant-buffer
   layouts while translating the same source to HLSL/MSL.
2. `spirv-cross` emits the target-specific textual artifact, except Vulkan/Android Vulkan, which
   consume the Vulkan-semantics SPIR-V directly.
3. The tool writes a format-3 manifest containing the source SHA-256 plus a compact runtime-checked
   FNV-1a source fingerprint, global and target-specific defines, target artifacts, compiler paths,
   and compiler version strings. ShaderManagerUVE validates that metadata before consuming an
   artifact, so a package generated from a different source/stage/define policy falls back safely
   to authoring-source compilation instead of being silently accepted.

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
  --define COMPUTE_SHADER \
  --out-dir build/shaders/frustum_cull/comp
```

A source that has backend-specific layout policy can compile those variants from one file. The
`TARGET=DEFINE` form is repeated once per target policy and is recorded in the manifest:

```sh
python3 Engine/Tools/compile_shaders.py \
  Engine/Runtime/RHI/Shader/built_in/basic_3d.glsl \
  --stage vert \
  --target vulkan --target opengl \
  --define VERTEX_SHADER \
  --target-define vulkan=UVE_VULKAN \
  --out-dir build/shaders/basic_3d/vert
```

The cooked runtime layout mirrors the CMake output: `<mount>/<artifact-key>/<stage>/<stem>.<target>.<ext>`
for the base variant, and `<mount>/<artifact-key>/<variant>/<stage>/<stem>.<target>.<ext>` for named
variants such as `instanced`, `bindless`, and `instanced_bindless`. ShaderManagerUVE canonicalizes
those two material axes and tries the matching path for a backend-native artifact; an unknown define
set never reuses a different variant. Legacy descriptors leave `artifact-key` empty and retain the
filename-stem layout used by the built-in tree. Imported shader assets should persist a path-derived
key (for example, `materials/stone`) so two `stone.vert` files in different directories cannot
collide. It falls back to authoring source when the artifact is absent.
The instanced GLES artifact is explicitly ESSL 3.10 because SSBOs do not exist in ES 3.0; the
runtime must select the ordinary (non-instanced) fallback on an ES 3.0 device. `UVE_BINDLESS` is
currently selected only for a Vulkan device that reports the native descriptor-indexing tier; every
other backend/tier compiles the same material through the fixed-slot path.

The compiler executables are intentionally discovered when the tool is invoked. A platform SDK
is therefore required only for the targets that a developer or CI job requests. The shipped
runtime never depends on either executable.

## Imported material packaging

A raw `.vert`, `.frag`, or `.comp` import can carry the source identity needed by the runtime
artifact selector without putting a host filesystem path into a scene. Pass
`Asset::ShaderImportSettingsUVE::virtualFilePath` when importing the source; the importer stores
that VFS path in `ShaderAssetUVE::virtualFilePath` and derives its `cookedArtifactKey` by removing
the source extension. An explicit `cookedArtifactKey` is available when vertex and fragment files
need to share a directory or when a project has a different package naming policy. The path uses
forward slashes and must be relative; absolute paths, `..`, and backslashes are rejected before the
asset is published.

For example, two stages of one custom material can be packaged into one collision-safe tree:

```text
materials/stone.vert  -> virtualFilePath materials/stone.vert
materials/stone.frag  -> virtualFilePath materials/stone.frag
                               cookedArtifactKey materials/stone
```

The corresponding build steps use the same key as the output directory. They can be expressed as
`uve_add_shader_artifacts()` calls in a project's CMake file, or directly with the tool:

```sh
python3 Engine/Tools/compile_shaders.py materials/stone.vert \
  --stage vert --target vulkan --target android-vulkan --target opengl --target gles --target d3d12 --target metal --target ios \
  --define VERTEX_SHADER --target-define vulkan=UVE_VULKAN \
  --target-define android-vulkan=UVE_VULKAN --target-define gles=UVE_GLES \
  --out-dir build/shaders/materials/stone/vert

python3 Engine/Tools/compile_shaders.py materials/stone.frag \
  --stage frag --target vulkan --target android-vulkan --target opengl --target gles --target d3d12 --target metal --target ios \
  --define FRAGMENT_SHADER --target-define vulkan=UVE_VULKAN \
  --target-define android-vulkan=UVE_VULKAN --target-define gles=UVE_GLES \
  --out-dir build/shaders/materials/stone/frag
```

If the material opts into the native Vulkan material tier, both source stages must contain
`UVE_BINDLESS_MATERIAL_CONTRACT`, both artifact builds must add `--define UVE_BINDLESS`, and the
variant output goes below `build/shaders/materials/stone/bindless/<stage>`. Instanced material
stages similarly add `UVE_INSTANCED`; using both defines selects `instanced_bindless`. The material
asset does not need to know which backend is active: Renderer3DUVE passes each shader asset's
metadata to ShaderManagerUVE, which validates the source fingerprint and target policy before
selecting a package. Missing/stale packages use the embedded source where the active backend can
compile it, and unsupported Vulkan devices remain on the fixed-slot path. This is the generic
imported-material route; canonical built-in lit assets continue to receive their compatibility
`shaders/lit_shadowed_3d.glsl` identity automatically.

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

The production lit material contract uses `UVE_BINDLESS_MATERIAL_CONTRACT` as an explicit source
marker in both stages. When Vulkan capability negotiation succeeds, Renderer3DUVE injects
`UVE_BINDLESS` into both stage descriptors, writes three sampled-texture indices into the reflected
frame block (`uAlbedoTextureIndex`, `uNormalTextureIndex`, and `uAOTextureIndex`), and leaves the
shadow samplers in set 0. A bindless draw therefore binds only the three shadow textures in logical
set-0 slots 0..2 and samples material textures from set 1; a legacy or unsupported material keeps
material slots 0..2 and shadow slots 3..5. If a native texture table is exhausted, the renderer
resolves that material texture to the registered white/flat-normal fallback slot and continues
without changing the shader contract. A material is never compiled into a native variant on a
backend that cannot execute the corresponding descriptor layout.

The non-Vulkan textual artifacts are a portability aid, not proof that every target has a native
bindless implementation yet: generated HLSL/MSL still requires the target backend's final
compiler and resource-layout validation. `bindless_probe` keeps the 256-entry set-1 contract only
in its Vulkan-family variants; its OpenGL/GLES/text outputs are bounded compiler smoke fixtures,
with the GLES probe using a constant element so ES 3.1 does not require a vendor non-uniform-index
extension. The fallback binding path remains mandatory for low-tier Vulkan, OpenGL/GLES, and any
device whose native descriptor capacity is exhausted.

Current limitations are explicit: the default CMake configure keeps artifact generation off so a
Null/OpenGL-only checkout does not require every compiler and SDK. Release/CI configs can enable
`UVE_BUILD_BUILTIN_SHADER_ARTIFACTS=ON` and build the aggregate `uve_builtin_shader_artifacts`
target. ShaderManagerUVE now consumes the cooked artifact when the backend-specific file is mounted and
its format-3 manifest matches the source fingerprint, stage, entry point, and target defines
(and safely falls back when it is missing or stale); per-request extra defines still require a
separately cooked variant. The repository CI runs `spirv-val` against Vulkan-semantics modules and validates
OpenGL-semantics intermediates with the generic SPIR-V validator, plus SPIRV-Cross JSON reflection
on `bindless_probe.vulkan.spv`. The target covers the five compute built-ins, the deterministic B1 fixture, the basic/basic-textured/
particle graphics materials, shadow-depth instancing, lit shadowing, and the post-processing/UI
materials currently present in this repository. Remaining validation is target-native final
compiler validation on Windows/D3D12 and macOS/iOS/Metal, real-device coverage, and wiring any
future native D3D12/Metal runtime backends to the same cooked-artifact mount. The tool itself is
already usable as a reproducible, target-aware artifact generator.
