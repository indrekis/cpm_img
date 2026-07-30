# Release packaging

`CMakeLists.txt` is the single source of the CPMimg version. The same version is
used in:

- the Windows `VERSIONINFO` resource;
- generated `pluginst.inf`;
- runtime and symbols archive names;
- tag validation in `tools/make_release.py`.

## Local release build

Create both build trees, install them into one staging directory, and create
the release archives:

```powershell
python tools\make_release.py --clean --vcpkg-root C:\vcpkg
```

Generated files:

```text
dist/
  cpmimg-wcx-<version>.zip
  cpmimg-wcx-<version>.zip.sha256
  cpmimg-wcx-<version>-symbols.zip
  cpmimg-wcx-<version>-symbols.zip.sha256
  stage/
```

The runtime package contains both `cpmimg.wcx` and `cpmimg.wcx64`. Vendored LibDsk, zlib and bzip2 are linked statically, so no architecture-specific `libdsk.dll`, `bz2.dll` or `zlib1.dll` files are required. PDB files are included only in the symbols package.

A pushed tag such as `v0.99` runs `.github/workflows/release.yml`. The tag
version must match the version declared in `CMakeLists.txt`.


## Why compression libraries are static

The Total Commander plugin archive installs both architectures into one directory. Dynamic x86 and x64 builds of `bz2.dll` and `zlib1.dll` use identical file names and therefore cannot coexist there. Static linkage avoids that collision and makes the combined package self-contained.

## Dependency handling

Release builds use the static packages already installed under the selected vcpkg root:

```text
<vcpkg-root>/installed/x86-windows-static
<vcpkg-root>/installed/x64-windows-static
```

Before configuring CMake, `tools/make_release.py` verifies that each triplet contains the zlib and bzip2 headers and static libraries. It then passes the triplet directory through `CMAKE_PREFIX_PATH`, `ZLIB_ROOT` and `BZip2_ROOT`, and validates `CMakeCache.txt` after configuration. The build is rejected if CMake selects MSYS2/MinGW or another dependency tree.

Dependency versions are intentionally not pinned by a vcpkg baseline. The versions selected by CMake are recorded in the build log.
