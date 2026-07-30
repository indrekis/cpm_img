# Third-party notices

CPMimg is distributed under GNU GPL v3. The release also contains or
statically links the third-party components listed below. Their own
license terms continue to apply.

## LibDsk

- Purpose: floppy-disk image access and format handling.
- Form: vendored source, statically linked into each WCX binary.
- License: see `licenses/libdsk-COPYING.txt` in the binary package and
  `libdsk_cmake/COPYING` in the source tree.
- Upstream: https://www.seasip.info/Unix/LibDsk/

## cpmtools-derived code

- Purpose: CP/M filesystem access.
- Form: vendored/derived source, statically linked into each WCX binary.
- License: see `licenses/cpmtools-COPYING.txt` in the binary package and
  `cpmtools/COPYING` in the source tree.
- Upstream: https://www.moria.de/~michael/cpmtools/

## zlib

- Version supplied by the selected vcpkg installation; see the release build log.
- Form: statically linked into LibDsk/CPMimg.
- License: zlib License; see `licenses/zlib-LICENSE.txt`.
- Upstream: https://zlib.net/

## bzip2 / libbzip2

- Version supplied by the selected vcpkg installation; see the release build log.
- Form: statically linked into LibDsk/CPMimg.
- License: BSD-style bzip2 license; see
  `licenses/bzip2-LICENSE.txt`.
- Upstream: https://sourceware.org/bzip2/

