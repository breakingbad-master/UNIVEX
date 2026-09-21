# UniVex shader artifact pipeline

`compile_shaders.py` is the build-time bridge from one canonical GLSL source to the native
backends targeted by the RHI:

| Target | Artifact |
|---|---|
| Vulkan | SPIR-V (`.spv`) |
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

Current limitations are explicit: the existing shader manager still consumes its current
backend-specific source/binary fields, and CMake has not yet made the artifact generation a
mandatory default. Those are the next integration slice; this tool is already usable as a
reproducible, target-aware artifact generator.
