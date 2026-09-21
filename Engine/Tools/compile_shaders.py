#!/usr/bin/env python3
"""Build one canonical GLSL shader source into backend-specific artifacts.

The engine keeps source compilation out of the runtime.  This tool uses the Khronos GLSL
front-end to produce SPIR-V once, then uses the SPIR-V cross compiler to emit the textual
artifacts required by the other native backends.  Vulkan consumes the SPIR-V directly; desktop
OpenGL and GLES consume generated GLSL; D3D12 consumes generated HLSL; Metal and iOS consume
MSL.  The source hash and tool versions are written to a manifest so stale or non-reproducible
artifacts are diagnosable instead of silently accepted.

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


TARGETS = ("vulkan", "opengl", "gles", "d3d12", "metal", "ios")
STAGES = ("vert", "frag", "comp", "geom", "tesc", "tese")


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
    spirv_cross: str,
    spirv_path: Path,
    output_path: Path,
) -> list[str] | None:
    if target == "vulkan":
        shutil.copyfile(spirv_path, output_path)
        return None
    if target == "opengl":
        return [spirv_cross, str(spirv_path), "--version", "450", "--output", str(output_path)]
    if target == "gles":
        return [
            spirv_cross,
            str(spirv_path),
            "--es",
            "--version",
            "310",
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
        "opengl": ".glsl",
        "gles": ".glsl",
        "d3d12": ".hlsl",
        "metal": ".metal",
        "ios": ".metal",
    }[target]
    return f"{source.stem}.{target}{suffix}"


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
) -> dict[str, object]:
    source_bytes = source.read_bytes()
    source_hash = hashlib.sha256(source_bytes).hexdigest()
    output_dir.mkdir(parents=True, exist_ok=True)
    spirv_path = _safe_output_path(output_dir, f"{source.stem}.intermediate.spv")

    compile_command = [glslang, "-V", "-S", stage, "-e", entry, "-o", str(spirv_path)]
    for include_dir in include_dirs:
        compile_command.append(f"-I{include_dir}")
    for define in defines:
        compile_command.append(f"-D{define}")
    compile_command.append(str(source))
    _run(compile_command)

    artifacts: dict[str, str] = {}
    for target in targets:
        artifact_path = _safe_output_path(output_dir, _artifact_name(source, target))
        command = _target_command(target, spirv_cross, spirv_path, artifact_path)
        if command is not None:
            _run(command)
        artifacts[target] = str(artifact_path)

    return {
        "source": str(source),
        "source_sha256": source_hash,
        "stage": stage,
        "entry_point": entry,
        "artifacts": artifacts,
    }


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
    glslang = args.glslang or "glslangValidator"
    spirv_cross = args.spirv_cross or "spirv-cross"
    manifest_path = (args.manifest or args.out_dir / "shader_manifest.json").resolve()

    if args.dry_run:
        print(json.dumps({
            "source": str(source),
            "stage": args.stage,
            "entry_point": args.entry,
            "targets": targets,
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
    )
    manifest = {
        "format": 1,
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
