#!/usr/bin/env python3
# Build and package combined x86/x64 CPMimg releases.
#
# By default this script:
#   1. configures build-release-x86 and build-release-x64;
#   2. builds RelWithDebInfo binaries;
#   3. installs both architectures into dist/stage;
#   4. creates the Total Commander installation ZIP;
#   5. creates a separate PDB symbols ZIP;
#   6. writes SHA-256 sidecar files.
#
# CMakeLists.txt is the single source of the version.

from __future__ import annotations

import argparse
import hashlib
import os
import re
import shutil
import struct
import subprocess
import sys
import zipfile
from pathlib import Path
from typing import Iterable


EXPECTED_RUNTIME_FILES = {
    "cpmimg.wcx",
    "cpmimg.wcx64",
    "diskdefs",
    "pluginst.inf",
    "ReadMe.md",
    "LICENSE.txt",
    "THIRD_PARTY_NOTICES.md",
    "licenses/zlib-LICENSE.txt",
    "licenses/bzip2-LICENSE.txt",
    "licenses/cpmtools-COPYING.txt",
    "licenses/libdsk-COPYING.txt",
}

PE_I386 = 0x014C
PE_AMD64 = 0x8664


class ReleaseError(RuntimeError):
    pass


def run(command: list[str], cwd: Path | None = None) -> None:
    print("+", subprocess.list2cmdline(command), flush=True)
    result = subprocess.run(command, cwd=cwd, check=False)
    if result.returncode:
        raise ReleaseError(
            f"command failed with exit code {result.returncode}: "
            f"{subprocess.list2cmdline(command)}"
        )


def read_project_version(cmake_file: Path) -> str:
    text = cmake_file.read_text(encoding="utf-8")
    match = re.search(
        r"project\s*\(\s*CPMimage\b"
        r"(?:(?!\)).)*?\bVERSION\s+"
        r"([0-9]+\.[0-9]+(?:\.[0-9]+){0,2})",
        text,
        re.IGNORECASE | re.DOTALL,
    )
    if not match:
        raise ReleaseError(
            "cannot read CPMimage VERSION from CMakeLists.txt"
        )
    return match.group(1)


def normalized_version(value: str) -> str:
    value = value.strip()
    if value.lower().startswith("v"):
        value = value[1:]
    if not re.fullmatch(r"[0-9]+\.[0-9]+(?:\.[0-9]+){0,2}", value):
        raise ReleaseError(f"invalid version: {value!r}")
    return value


def parse_pluginst(path: Path) -> dict[str, str]:
    section = ""
    values: dict[str, str] = {}
    for raw_line in path.read_text(encoding="utf-8-sig").splitlines():
        line = raw_line.strip()
        if not line or line.startswith((";", "#")):
            continue
        if line.startswith("[") and line.endswith("]"):
            section = line[1:-1].strip().lower()
            continue
        if section != "plugininstall":
            continue
        if "=" not in line:
            raise ReleaseError(f"invalid pluginst.inf line: {raw_line!r}")
        key, value = line.split("=", 1)
        values[key.strip().lower()] = value.strip()
    return values


def validate_pluginst(path: Path, version: str) -> None:
    values = parse_pluginst(path)
    expected = {
        "description": "CP/M image R/W plugin for Total Commander",
        "descriptionukr":
            "Плагін для роботи із образами CP/M для Total Commander",
        "version": version,
        "type": "wcx",
        "file": "cpmimg.wcx",
        "defaultdir": r"plugin\wcx\cpmimg",
        "defaultextension": "IMD,TD0",
    }
    for key, expected_value in expected.items():
        actual = values.get(key)
        if actual != expected_value:
            raise ReleaseError(
                f"pluginst.inf: {key}={actual!r}, expected "
                f"{expected_value!r}"
            )


def pe_machine(path: Path) -> int:
    data = path.read_bytes()
    if len(data) < 0x40 or data[:2] != b"MZ":
        raise ReleaseError(f"{path}: not a PE file")
    pe_offset = struct.unpack_from("<I", data, 0x3C)[0]
    if pe_offset + 6 > len(data) or data[pe_offset:pe_offset + 4] != b"PE\0\0":
        raise ReleaseError(f"{path}: invalid PE header")
    return struct.unpack_from("<H", data, pe_offset + 4)[0]


def validate_pe(path: Path, expected_machine: int) -> None:
    actual = pe_machine(path)
    if actual != expected_machine:
        raise ReleaseError(
            f"{path}: PE machine 0x{actual:04x}, expected "
            f"0x{expected_machine:04x}"
        )





def require_static_triplet(vcpkg_root: Path, triplet: str) -> Path:
    triplet_root = vcpkg_root / "installed" / triplet
    required = (
        triplet_root / "include" / "zlib.h",
        triplet_root / "include" / "bzlib.h",
        triplet_root / "lib" / "zs.lib",
        triplet_root / "lib" / "bz2.lib",
    )
    missing = [path for path in required if not path.is_file()]
    if missing:
        formatted = "\n  ".join(str(path) for path in missing)
        raise ReleaseError(
            f"vcpkg triplet {triplet} is incomplete. Missing:\n  "
            f"{formatted}\n"
            f"Install it with:\n"
            f"  {vcpkg_root / 'vcpkg.exe'} install "
            f"zlib:{triplet} bzip2:{triplet}"
        )
    return triplet_root

def validate_dependency_cache(
    cache_file: Path,
    triplet_root: Path,
    triplet: str,
) -> None:
    if not cache_file.is_file():
        raise ReleaseError(f"CMake cache not found: {cache_file}")

    lines = cache_file.read_text(
        encoding="utf-8",
        errors="replace",
    ).splitlines()

    relevant = [
        line
        for line in lines
        if line.startswith(("ZLIB_", "BZIP2_", "BZip2_"))
        and "=" in line
    ]

    joined = "\n".join(relevant)
    normalized = joined.replace("\\", "/").lower()
    expected = str(triplet_root).replace("\\", "/").lower()

    if "msys2" in normalized or "/mingw" in normalized:
        raise ReleaseError(
            f"{triplet}: CMake selected an MSYS2/MinGW dependency:\n"
            f"{joined}"
        )

    zlib_entries = "\n".join(
        line
        for line in relevant
        if line.upper().startswith("ZLIB_")
    ).replace("\\", "/").lower()

    bzip2_entries = "\n".join(
        line
        for line in relevant
        if line.upper().startswith("BZIP2_")
    ).replace("\\", "/").lower()

    if expected not in zlib_entries:
        raise ReleaseError(
            f"{triplet}: Zlib was not selected from {triplet_root}.\n"
            f"Cache entries:\n{zlib_entries or '<none>'}"
        )

    if expected not in bzip2_entries:
        raise ReleaseError(
            f"{triplet}: BZip2 was not selected from {triplet_root}.\n"
            f"Cache entries:\n{bzip2_entries or '<none>'}"
        )

def configure_and_build(
    *,
    cmake: str,
    root: Path,
    build_dir: Path,
    generator: str,
    architecture: str,
    triplet: str,
    triplet_root: Path,
    configuration: str,
    vcpkg_root: Path | None,
    jobs: int,
) -> None:
    configure = [
        cmake,
        "-S", str(root),
        "-B", str(build_dir),
        "-G", generator,
        "-A", architecture,
        f"-DVCPKG_TARGET_TRIPLET={triplet}",
        f"-DCMAKE_PREFIX_PATH={triplet_root}",
        f"-DZLIB_ROOT={triplet_root}",
        f"-DBZip2_ROOT={triplet_root}",
    ]
    if vcpkg_root is not None:
        toolchain = (
            vcpkg_root / "scripts" / "buildsystems" / "vcpkg.cmake"
        )
        if not toolchain.is_file():
            raise ReleaseError(f"vcpkg toolchain not found: {toolchain}")
        configure.append(f"-DCMAKE_TOOLCHAIN_FILE={toolchain}")

    run(configure)
    validate_dependency_cache(
        build_dir / "CMakeCache.txt",
        triplet_root,
        triplet,
    )

    build = [
        cmake,
        "--build", str(build_dir),
        "--config", configuration,
        "--target", "cpmimg_wcx",
    ]
    if jobs > 0:
        build.extend(["--parallel", str(jobs)])
    run(build)


def install_components(
    *,
    cmake: str,
    build_dir: Path,
    stage_dir: Path,
    configuration: str,
) -> None:
    for component in ("cpmimg_runtime", "cpmimg_symbols"):
        run([
            cmake,
            "--install", str(build_dir),
            "--config", configuration,
            "--prefix", str(stage_dir),
            "--component", component,
        ])


def iter_runtime_files(stage_dir: Path) -> Iterable[Path]:
    for path in sorted(stage_dir.rglob("*")):
        if not path.is_file():
            continue
        relative = path.relative_to(stage_dir)
        if relative.parts and relative.parts[0].lower() == "symbols":
            continue
        if path.suffix.lower() in {".pdb", ".ini", ".bak"}:
            continue
        yield path


def fixed_zip_write(
    archive: zipfile.ZipFile,
    source: Path,
    archive_name: str,
) -> None:
    info = zipfile.ZipInfo(
        filename=archive_name.replace("\\", "/"),
        date_time=(1980, 1, 1, 0, 0, 0),
    )
    info.compress_type = zipfile.ZIP_DEFLATED
    info.external_attr = 0o100644 << 16
    archive.writestr(info, source.read_bytes())


def make_runtime_zip(stage_dir: Path, output: Path) -> None:
    files = list(iter_runtime_files(stage_dir))
    names = {
        path.relative_to(stage_dir).as_posix()
        for path in files
    }
    missing = sorted(EXPECTED_RUNTIME_FILES - names)
    if missing:
        raise ReleaseError(
            "runtime staging tree is incomplete: " + ", ".join(missing)
        )
    forbidden = sorted(
        name for name in names
        if name.lower().endswith((".pdb", ".ini", ".bak"))
    )
    if forbidden:
        raise ReleaseError(
            "forbidden runtime files: " + ", ".join(forbidden)
        )

    with zipfile.ZipFile(output, "w") as archive:
        for source in files:
            fixed_zip_write(
                archive,
                source,
                source.relative_to(stage_dir).as_posix(),
            )


def make_symbols_zip(stage_dir: Path, output: Path) -> None:
    symbols = stage_dir / "symbols"
    required = {
        "x86/cpmimg.pdb",
        "x64/cpmimg.pdb",
    }
    files = sorted(path for path in symbols.rglob("*.pdb") if path.is_file())
    names = {
        path.relative_to(symbols).as_posix()
        for path in files
    }
    missing = sorted(required - names)
    if missing:
        raise ReleaseError(
            "symbols staging tree is incomplete: " + ", ".join(missing)
        )

    with zipfile.ZipFile(output, "w") as archive:
        for source in files:
            fixed_zip_write(
                archive,
                source,
                source.relative_to(symbols).as_posix(),
            )


def write_sha256(path: Path) -> Path:
    digest = hashlib.sha256(path.read_bytes()).hexdigest()
    sidecar = path.with_name(path.name + ".sha256")
    sidecar.write_text(
        f"{digest}  {path.name}\n",
        encoding="ascii",
        newline="\n",
    )
    return sidecar


def validate_stage(stage_dir: Path, version: str) -> None:
    validate_pluginst(stage_dir / "pluginst.inf", version)
    validate_pe(stage_dir / "cpmimg.wcx", PE_I386)
    validate_pe(stage_dir / "cpmimg.wcx64", PE_AMD64)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--root",
        type=Path,
        default=Path(__file__).resolve().parents[1],
    )
    parser.add_argument("--build-root", type=Path)
    parser.add_argument("--dist-dir", type=Path)
    parser.add_argument(
        "--configuration",
        default="RelWithDebInfo",
        choices=("Release", "RelWithDebInfo", "MinSizeRel"),
    )
    parser.add_argument(
        "--generator",
        default="Visual Studio 17 2022",
    )
    parser.add_argument("--vcpkg-root", type=Path)
    parser.add_argument("--cmake", default="cmake")
    parser.add_argument(
        "--jobs",
        type=int,
        default=max(1, os.cpu_count() or 1),
    )
    parser.add_argument(
        "--version",
        help="expected version, normally derived from a vX.Y.Z tag",
    )
    parser.add_argument(
        "--skip-build",
        action="store_true",
        help="package an already populated staging directory",
    )
    parser.add_argument(
        "--clean",
        action="store_true",
        help="remove release build and dist directories first",
    )
    args = parser.parse_args()

    try:
        root = args.root.resolve()
        cmake_file = root / "CMakeLists.txt"
        version = read_project_version(cmake_file)
        if args.version:
            expected = normalized_version(args.version)
            if version != expected:
                raise ReleaseError(
                    f"tag/requested version {expected} does not match "
                    f"CMake project version {version}"
                )

        build_root = (
            args.build_root.resolve()
            if args.build_root
            else root / "build-release"
        )
        dist_dir = (
            args.dist_dir.resolve()
            if args.dist_dir
            else root / "dist"
        )
        stage_dir = dist_dir / "stage"
        build_x86 = build_root.with_name(build_root.name + "-x86")
        build_x64 = build_root.with_name(build_root.name + "-x64")

        if args.clean:
            for path in (build_x86, build_x64, dist_dir):
                if path.exists():
                    shutil.rmtree(path)
        elif not args.skip_build and stage_dir.exists():
            shutil.rmtree(stage_dir)

        dist_dir.mkdir(parents=True, exist_ok=True)
        stage_dir.mkdir(parents=True, exist_ok=True)

        vcpkg_root = args.vcpkg_root
        if vcpkg_root is None:
            env_root = os.environ.get("VCPKG_ROOT")
            if env_root:
                vcpkg_root = Path(env_root)
        if vcpkg_root is not None:
            vcpkg_root = vcpkg_root.resolve()

        if not args.skip_build:
            builds = (
                (build_x86, "Win32", "x86-windows-static"),
                (build_x64, "x64", "x64-windows-static"),
            )
            for build_dir, architecture, triplet in builds:
                if vcpkg_root is None:
                    raise ReleaseError(
                        "--vcpkg-root or VCPKG_ROOT is required"
                    )
                triplet_root = require_static_triplet(
                    vcpkg_root,
                    triplet,
                )
                configure_and_build(
                    cmake=args.cmake,
                    root=root,
                    build_dir=build_dir,
                    generator=args.generator,
                    architecture=architecture,
                    triplet=triplet,
                    triplet_root=triplet_root,
                    configuration=args.configuration,
                    vcpkg_root=vcpkg_root,
                    jobs=args.jobs,
                )
                install_components(
                    cmake=args.cmake,
                    build_dir=build_dir,
                    stage_dir=stage_dir,
                    configuration=args.configuration,
                )

        validate_stage(stage_dir, version)

        runtime_zip = dist_dir / f"cpmimg-wcx-{version}.zip"
        symbols_zip = dist_dir / f"cpmimg-wcx-{version}-symbols.zip"
        for output in (
            runtime_zip,
            symbols_zip,
            runtime_zip.with_name(runtime_zip.name + ".sha256"),
            symbols_zip.with_name(symbols_zip.name + ".sha256"),
        ):
            if output.exists():
                output.unlink()

        make_runtime_zip(stage_dir, runtime_zip)
        make_symbols_zip(stage_dir, symbols_zip)
        runtime_hash = write_sha256(runtime_zip)
        symbols_hash = write_sha256(symbols_zip)

        print(f"Created: {runtime_zip}")
        print(f"Created: {runtime_hash}")
        print(f"Created: {symbols_zip}")
        print(f"Created: {symbols_hash}")
        return 0
    except (OSError, ReleaseError) as exc:
        print(f"Error: {exc}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
