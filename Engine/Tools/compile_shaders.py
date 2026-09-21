#!/usr/bin/env python3
"""Build one canonical GLSL shader source into backend-specific artifacts.

The engine keeps source compilation out of the runtime.  This tool uses the Khronos GLSL
front-end to produce target-semantics SPIR-V variants, then uses the SPIR-V cross compiler to
emit the textual artifacts required by the other native backends. Vulkan consumes the Vulkan
variant directly; desktop OpenGL and GLES consume artifacts generated from OpenGL-semantics SPIR-V;
D3D12 consumes HLSL; Metal and iOS consume MSL. The source hash, target defines, and tool versions
are written to a manifest so stale or non-reproducible artifacts are diagnosable instead of silently
accepted.

The tools are deliberately discovered at invocation time.  A developer machine or CI runner
that does not target a platform does not need that platform's SDK, while a release build can
make each requested target mandatory.  This script is a build-time tool only: no compiler is
loaded by the shipped runtime.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
from typing import Iterable


TARGETS = ("vulkan", "android-vulkan", "opengl", "gles", "d3d12", "metal", "ios")
STAGES = ("vert", "frag", "comp", "geom", "tesc", "tese")
_OPENGL_TARGETS = frozenset(("opengl", "gles"))


def _tool_or_error(name: str, override: str | None) -> str:
    path = override or shutil.which(name)
    if path is None:
        raise RuntimeError(
            f"required shader tool '{name}' was not found; install the pinned shader toolchain "
            "or pass its path explicitly"
        )
    return path


def _run(command: list[str]) -> str:
    try:
        completed = subprocess.run(command, check=True, text=True, capture_output=True)
    except FileNotFoundError as exc:
        raise RuntimeError(f"could not execute '{command[0]}'") from exc
    except subprocess.CalledProcessError as exc:
        details = (exc.stderr or exc.stdout or "").strip()
        raise RuntimeError(
            f"shader command failed with exit code {exc.returncode}: {' '.join(command)}\n{details}"
        ) from exc
    return (completed.stdout or completed.stderr or "").strip()


def _version(command: str) -> str:
    try:
        return _run([command, "--version"]) or "unknown"
    except RuntimeError:
        # Some SDK tools write a version only for -v or do not expose one at all.  The manifest
        # still records the executable path; a failed version probe must not hide a valid build.
        return "unreported"


def _safe_output_path(output_dir: Path, relative_name: str) -> Path:
    candidate = (output_dir / relative_name).resolve()
    root = output_dir.resolve()
    if candidate != root and root not in candidate.parents:
        raise RuntimeError(f"refusing to write shader output outside '{root}': '{candidate}'")
    candidate.parent.mkdir(parents=True, exist_ok=True)
    return candidate


def _target_command(
    target: str,
    stage: str,
    spirv_cross: str,
    spirv_path: Path,
    output_path: Path,
) -> list[str] | None:
    if target in ("vulkan", "android-vulkan"):
        shutil.copyfile(spirv_path, output_path)
        return None
    if target == "opengl":
        return [spirv_cross, str(spirv_path), "--version", "450", "--output", str(output_path)]
    if target == "gles":
        # The Android fallback context is ES 3.0. Compute artifacts still request ES 3.1
        # because the source uses compute-only features, but graphics stages stay consumable by
        # the fixed ES 3.0 baseline used by the current Android window backend.
        gles_version = "310" if stage in ("comp", "geom", "tesc", "tese") else "300"
        return [
            spirv_cross,
            str(spirv_path),
            "--es",
            "--version",
            gles_version,
            "--output",
            str(output_path),
        ]
    if target == "d3d12":
        return [
            spirv_cross,
            str(spirv_path),
            "--hlsl",
            "--shader-model",
            "60",
            "--output",
            str(output_path),
        ]
    if target in ("metal", "ios"):
        return [
            spirv_cross,
            str(spirv_path),
            "--msl",
            "--msl-version",
            "20300",
            "--output",
            str(output_path),
        ]
    raise AssertionError(f"unknown target: {target}")


def _artifact_name(source: Path, target: str) -> str:
    suffix = {
        "vulkan": ".spv",
        "android-vulkan": ".spv",
        "opengl": ".glsl",
        "gles": ".glsl",
        "d3d12": ".hlsl",
        "metal": ".metal",
        "ios": ".metal",
    }[target]
    return f"{source.stem}.{target}{suffix}"


def _compile_target_spirv(
    source: Path,
    stage: str,
    entry: str,
    target: str,
    output_path: Path,
    glslang: str,
    include_dirs: tuple[Path, ...],
    defines: tuple[str, ...],
) -> None:
    # OpenGL and Vulkan are different SPIR-V source environments.  The former keeps the legacy
    # default-block uniforms that the GL runtime updates with glUniform; the latter selects the
    # explicit push-constant/descriptor layout used by native APIs.  Cross-compiling both from
    # the same authoring file is why target-specific defines are part of the artifact manifest.
    semantics = "-G" if target in _OPENGL_TARGETS else "-V"
    compile_command = [
        glslang,
        semantics,
        "-S",
        stage,
        "-e",
        entry,
        "-o",
        str(output_path),
    ]
    if target in _OPENGL_TARGETS:
        # OpenGL SPIR-V requires explicit locations for non-opaque default-block uniforms.  The
        # source remains directly consumable by the runtime compiler without those generated
        # locations; glslang assigns them only for this offline GL artifact variant.
        compile_command.append("--auto-map-locations")
    for include_dir in include_dirs:
        compile_command.append(f"-I{include_dir}")
    for define in defines:
        compile_command.append(f"-D{define}")
    compile_command.append(str(source))
    _run(compile_command)


def compile_shader(
    source: Path,
    stage: str,
    entry: str,
    targets: Iterable[str],
    output_dir: Path,
    glslang: str,
    spirv_cross: str,
    include_dirs: Iterable[Path],
    defines: Iterable[str],
    target_defines: dict[str, tuple[str, ...]],
) -> dict[str, object]:
    source_bytes = source.read_bytes()
    source_hash = hashlib.sha256(source_bytes).hexdigest()
    define_list = tuple(defines)
    include_list = tuple(include_dirs)
    target_list = tuple(dict.fromkeys(targets))
    output_dir.mkdir(parents=True, exist_ok=True)

    # With no target-specific policy (the historical compute path), keep the stable intermediate
    # filename.  Graphics migrations opt into per-target variants so a GL default-uniform module
    # can never accidentally be handed to Vulkan, or vice versa.
    use_per_target_intermediates = bool(target_defines)
    spirv_by_target: dict[str, Path] = {}
    if use_per_target_intermediates:
        for target in target_list:
            intermediate_name = f"{source.stem}.intermediate.{target}.spv"
            spirv_path = _safe_output_path(output_dir, intermediate_name)
            target_define_list = define_list + tuple(target_defines.get(target, ()))
            _compile_target_spirv(
                source,
                stage,
                entry,
                target,
                spirv_path,
                glslang,
                include_list,
                target_define_list,
            )
            spirv_by_target[target] = spirv_path
    else:
        # Legacy/all-targets compute artifacts intentionally have one Vulkan SPIR-V module.
        # There is no source conditional in that path, so compiling it once is both faster and
        # preserves the historical `.intermediate.spv` artifact name.
        target = target_list[0]
        spirv_path = _safe_output_path(output_dir, f"{source.stem}.intermediate.spv")
        _compile_target_spirv(
            source,
            stage,
            entry,
            target,
            spirv_path,
            glslang,
            include_list,
            define_list,
        )
        spirv_by_target = {target_name: spirv_path for target_name in target_list}

    artifacts: dict[str, str] = {}
    for target in target_list:
        artifact_path = _safe_output_path(output_dir, _artifact_name(source, target))
        command = _target_command(target, stage, spirv_cross, spirv_by_target[target], artifact_path)
        if command is not None:
            _run(command)
        artifacts[target] = str(artifact_path)

    return {
        "source": str(source),
        "source_sha256": source_hash,
        "stage": stage,
        "entry_point": entry,
        "defines": list(define_list),
        "target_defines": {target: list(target_defines.get(target, ())) for target in target_list},
        "artifacts": artifacts,
    }


def _parse_target_defines(values: Iterable[str]) -> dict[str, tuple[str, ...]]:
    parsed: dict[str, list[str]] = {}
    for value in values:
        target, separator, define = value.partition("=")
        if not separator or target not in TARGETS or not define:
            raise RuntimeError(
                f"invalid --target-define '{value}'; expected TARGET=DEFINE for one of {TARGETS}"
            )
        parsed.setdefault(target, []).append(define)
    return {target: tuple(defines) for target, defines in parsed.items()}


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path, help="canonical GLSL source file")
    parser.add_argument("--stage", choices=STAGES, required=True, help="shader stage")
    parser.add_argument("--entry", default="main", help="entry point (default: main)")
    parser.add_argument(
        "--target",
        dest="targets",
        action="append",
        choices=TARGETS,
        help="target backend; repeat to emit several targets (default: all targets)",
    )
    parser.add_argument("--out-dir", type=Path, required=True, help="artifact output directory")
    parser.add_argument("--glslang", help="path to glslangValidator")
    parser.add_argument("--spirv-cross", help="path to spirv-cross")
    parser.add_argument("-I", "--include-dir", dest="include_dirs", action="append", type=Path, default=[])
    parser.add_argument("-D", "--define", dest="defines", action="append", default=[])
    parser.add_argument(
        "--target-define",
        dest="target_defines",
        action="append",
        default=[],
        metavar="TARGET=DEFINE",
        help="define only while compiling one target's source variant; repeat as needed",
    )
    parser.add_argument(
        "--manifest",
        type=Path,
        help="manifest path (default: <out-dir>/shader_manifest.json)",
    )
    parser.add_argument(
        "--dry-run",
        action="store_true",
        help="validate arguments and print the planned toolchain without executing it",
    )
    return parser.parse_args(argv)


def main(argv: list[str]) -> int:
    args = parse_args(argv)
    source = args.source.resolve()
    if not source.is_file():
        raise RuntimeError(f"shader source does not exist: {source}")

    targets = tuple(dict.fromkeys(args.targets or TARGETS))
    target_defines = _parse_target_defines(args.target_defines)
    glslang = args.glslang or "glslangValidator"
    spirv_cross = args.spirv_cross or "spirv-cross"
    manifest_path = (args.manifest or args.out_dir / "shader_manifest.json").resolve()

    if args.dry_run:
        print(json.dumps({
            "source": str(source),
            "stage": args.stage,
            "entry_point": args.entry,
            "targets": targets,
            "defines": args.defines,
            "target_defines": {target: list(defines) for target, defines in target_defines.items()},
            "glslang": glslang,
            "spirv_cross": spirv_cross,
            "out_dir": str(args.out_dir.resolve()),
        }, indent=2))
        return 0

    glslang = _tool_or_error("glslangValidator", args.glslang)
    spirv_cross = _tool_or_error("spirv-cross", args.spirv_cross)
    artifact = compile_shader(
        source,
        args.stage,
        args.entry,
        targets,
        args.out_dir.resolve(),
        glslang,
        spirv_cross,
        (path.resolve() for path in args.include_dirs),
        args.defines,
        target_defines,
    )
    manifest = {
        "format": 2,
        "toolchain": {
            "glslang_validator": {"path": glslang, "version": _version(glslang)},
            "spirv_cross": {"path": spirv_cross, "version": _version(spirv_cross)},
        },
        "artifacts": [artifact],
    }
    manifest_path.parent.mkdir(parents=True, exist_ok=True)
    manifest_path.write_text(json.dumps(manifest, indent=2) + os.linesep, encoding="utf-8")
    print(json.dumps(manifest, indent=2))
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main(sys.argv[1:]))
    except RuntimeError as exc:
        print(f"compile_shaders.py: error: {exc}", file=sys.stderr)
        raise SystemExit(2) from exc
