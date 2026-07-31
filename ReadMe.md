- [Introduction](#introduction)
- [Installation](#installation)
- [Plugin configuration](#plugin-configuration)
  - [Image format selection dialog](#image-format-selection-dialog)
- [Compilation](#compilation)
  - [Packaging note](#packaging-note)
- [Images for tests](#images-for-tests)
- [Problems and limitations](#problems-and-limitations)
- [Plugin behavior details](#plugin-behavior-details)
  - [Notes on automatic format probing](#notes-on-automatic-format-probing)
    - [How probing selects and ranks candidates](#how-probing-selects-and-ranks-candidates)
    - [Prioritized search batches](#prioritized-search-batches)
  - [Image metainformation details](#image-metainformation-details)
  - [Logging](#logging)
- [Plans](#plans)
- [Credits](#credits)


# Introduction

CPMimg is a WCX (archive) plugin for 64-bit and 32-bit Total Commander (TCmd) that provides read-write access to CP/M disk images.

Key features:

- Supports CP/M disk formats described by a [cpmtools](https://github.com/lipro-cpm4l/cpmtools)-compatible `diskdefs` file and accessible through [LibDsk](https://www.seasip.info/Unix/LibDsk/). 
  - So, TD0 images are read-only.
  - Libdsk required extensive though trivial patching because of command-line-oriented error handling.
- Supports raw sector images and container formats handled by LibDsk. CP/M disk layouts are highly diverse; commonly used container formats include [ImageDisk](http://dunfield.classiccmp.org/img/) (`.IMD`) and [Teledisk](http://dunfield.classiccmp.org/img42841/teledisk.htm) (`.TD0`).
- Uses the configured `image_format` as the default `diskdefs` entry. If that format cannot be mounted, or if the resulting CP/M directory does not look correct, the plugin can ask the user to select another format instead of exposing random image data as files.
- Provides advisory format hints based on preliminary LibDsk geometry and, where useful, the exact image payload size. These hints narrow the candidate list but do not constitute automatic format detection.
- Allows the selected format to be used only for the current image, retained as the default for later images in the current TCmd session, or saved persistently to `cpmdiskimg.ini`.
- During the session, keeps the format selection local to each opened archive. Changing the default for later images does not alter an archive that is already open.
- Autodetection of the image type in the isolated probe -- separate executable. 
- Details about the image can be shown by an additional virtual,
read-only file in the archive root, `__CPM_DISK_INFO__.TXT`.

# Installation

As usual for the TCmd plugins:

- Manual installation:
   1. Unzip the WCX to the directory of your choice (e.g., c:\wincmd\plugins\wcx)
   2. In TCmd, choose **Configuration** -> **Options**
   3. Open the '**Packer**' page
   4. Click '**Configure packer extension WCXs**'
   5. Type '*img*' as the extension
   6. Click '**New type**', and select the *cpmimg.wcx64* (*cpmimg.wcx* for 32-bit TCmd)
   7. Click **OK**
- Automated installation:
   1. Open the archive containing the plugin directly in TCmd.
   2. The program will prompt you to install the plugin.

Settings are stored in the `cpmdiskimg.ini` file at the configuration path provided by the TCmd (usually the same place as `wincmd.ini`). The configuration file will be recreated with default values if missing or corrupted.

Binary releases available [here](https://github.com/indrekis/cpm_img/releases).

**This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.**  

# Plugin configuration

The configuration file is named `cpmdiskimg.ini`. It is searched for using the configuration path provided by TCmd, most often the directory containing `wincmd.ini`. If the file is absent or invalid, the plugin recreates it using default values.

Except for resources that are already open, the plugin rereads the configuration before opening each image. Each opened archive keeps its own selected format.

Options can also be changed from the **Options** dialog of the packing dialog.

Example configuration:

```ini
[CPM_disk_img_plugin]
allow_dialogs=1
allow_txt_log=0
enable_format_probing=1
show_disk_info_file=1
log_file_path=D:\Temp\cpmimg.txt
debug_level=0

image_format=osbexec1
diskdefs_file_path=d:\totalcmd3\plugins\wcx\cpmimg\diskdefs
```

- `allow_dialogs=1` — enables plugin dialogs used for image peculiarities and format selection.
- `allow_txt_log=1` — enables detailed diagnostic logging. Logging can noticeably reduce performance and is intended primarily for image analysis and debugging.
  - Important diagnostic events are also sent to the debugger in debug builds (without NDEBUG defined). They can be viewed with a  full-fledged debugger or tools such as [SimpleProgramDebugger](http://www.nirsoft.net/utils/simple_program_debugger.html).
- `log_file_path=<filename>` — selects the log file. If the file cannot be opened for writing, text logging is disabled. The file is recreated when logging is initialized for a TCmd session.
- `debug_level` — currently reserved; values above the supported range make the configuration invalid.
- `image_format` — the default format name from the selected `diskdefs` file. It is copied into each archive when that archive is opened.
- `diskdefs_file_path` — path to the `cpmtools`-compatible `diskdefs` file used both for mounting images and for populating the format-selection dialog.
- `enable_format_probing` -- use `0` to disable automatic probing, `1` to enable. Manual **Probe now** remains available.
- `show_disk_info_file` -- 0 to disable creation of the `__CPM_DISK_INFO__.TXT` virtual file with metainformation, 1 -- enable it.

## Image format selection dialog

The dialog is shown after the configured format fails to mount or after directory validation rejects the resulting directory as implausible. Directory validation checks CP/M directory status bytes, file names, extent fields, record counts, and allocation block bounds. Its purpose is to catch common false-positive mounts; it cannot prove that a format is correct.

The candidate list is prepared using two independent hints:

1. **Preliminary LibDsk geometry**
   - sector length;
   - total tracks, calculated as cylinders multiplied by heads;
   - sectors per track.
2. **Exact image payload size**, calculated from the `diskdefs` sector length, sectors per track, tracks, and optional offset.

For `.logdisk` images, the 128-byte LibDsk trailer is excluded before comparing the image payload size.

When preliminary geometry is considered reliable, matching fields are shown in bold, and the dialog reports:

- `Geometry match: Yes` — all three geometry fields match;
- `Geometry match: Could be` — two fields match;
- `Geometry match: Low probability` — one field matches;
- `Geometry match: No` — none of the geometry fields match, and there is no stronger size hint;
- `Geometry match: Size match` — the exact payload size matches even though the geometry score is zero.

LibDsk boot-sector probing can occasionally identify an unrelated disk layout. When the preliminary geometry contradicts an authoritative raw payload size, it is not used as negative evidence. The dialog then reports:

- `Geometry (size OK): Unreliable` — the preliminary geometry is unreliable, but the selected `diskdefs` entry has the expected payload size;
- `Geometry: Unreliable` — the preliminary geometry is unreliable, and the selected entry has no exact size match.

These messages are hints only. Several CP/M formats may share the same geometry and image size, so the user may still need to know the machine or software that produced the image.

The dialog checkboxes have the following scope:

- With both checkboxes cleared, the selected format is used only to retry the current image.
- **Use this disk type for other images in current session** changes the in-memory default for images opened later in the same TCmd session.
- **Save to config file** also stores the selected format as `image_format` in `cpmdiskimg.ini`.

The format autodetection:

- **Probe now**: probe only the current image.
- **Automatically probe future unknown images**: change the preference for the current Total Commander session.
- **Save this probing preference permanently**: also write it to `cpmdiskimg.ini`.
- **Use this disk type for other images in current session** /
  **Save selected disk type to config file** -- cache the selected CP/M format, independently from the probing preference.

Pressing **Cancel** or **Escape** aborts the open operation. 


# Compilation

Code can be compiled using the Visual Studio project or CMakeLists.txt (tested using MSVC and MinGW). 

For the coexistence of the 32-bit and 64-bit plugin and to minimize customization of the VCPKG-based build environment, both  32-bit and 64-bit versions are built statically.

- Uses C++20, with no obligatory external dependencies. 
- The UI uses raw WinAPI controls to avoid conflicts between multiple GUI runtime copies inside TCmd. 
  - Tested GUI libraries use too many static and global objects...

Examples of the command lines to compile using CMake are in the CMakeLists.txt.

## Packaging note

The release package must include the probing helper executable that matches the
plugin bitness:

- `cpmimg_probe32.exe` with `cpmimg.wcx`
- `cpmimg_probe64.exe` with `cpmimg.wcx64`

Without the helper, the plugin falls back to manual format selection.

To create a distribution package, use:

```bash
python tools/make_release.py --clean --vcpkg-root <VCPKG_PATH>
```


# Images for tests

The plugin was tested using two kinds of images:

- Images of virtual machines and emulators used for different tasks.
- Historical floppy images from the retro-computing sites.

The plugin is tested on several hundred floppy images.

# Problems and limitations

- CP/M format identification remains heuristic. Directory validation rejects many wrong mounts, but an incorrect format can still produce a superficially plausible directory.  Mainly, problems are manifested by an image showing many user "folders". Users are a rarely used feature, anyway.
- Geometry and exact image size are not unique identifiers. Multiple `diskdefs` entries may describe media with the same physical layout.
- LibDsk boot-sector probing can produce false-positive geometry. The format dialog treats contradictory geometry as unreliable where an authoritative raw payload size is available.
- A candidate shown as `Yes` or `Size match` is still only a recommendation; the selected `diskdefs` entry is verified by attempting to mount and validate the CP/M directory.
- Container formats may include metadata or compression, so their physical file size does not necessarily equal the raw disk payload size.
- Write support depends on the underlying LibDsk driver. TD0 support is read-only.


# Plugin behavior details 

## Notes on automatic format probing

Starting with the safe probing implementation, the plugin can test candidate CP/M disk formats in an isolated helper process (`cpmimg_probe32.exe` or `cpmimg_probe64.exe`). If a candidate crashes, hangs, or rejects the image, Total Commander keeps running. When automatic probing is enabled and the initial direct mount fails, the plugin can run isolated probing and then show the format-selection dialog with ranked candidates. 

> Note: probing can incur noticeable delays.

All unique disk definitions are ranked. Candidates are tested in priority batches: exact IMD (or other container) logical-size and layout matches first, then weaker layout or LibDsk geometry matches, and finally unhinted formats. Lower-priority tiers are reached only when no format in a better tier mounts successfully.

### How probing selects and ranks candidates

The probing helper always tests candidates in an isolated process, but the candidate list is prioritized before helpers are started. For raw flat images, the ranking primarily uses:

- exact expected raw size:
  `offset + secLength * sectrk * tracks`,
- track count,
- sector length,
- sectors per track,
- LibDsk geometry as an additional hint.

For container formats such as IMD, the plugin no longer uses the container file size directly. Instead it parses the container and computes the **logical raw capacity** from the sector layout stored inside the image. For IMD, this means summing the declared size of every sector described by every track record, including sectors stored in compressed form.

For IMD-like containers, the priority is based on:

- exact logical raw capacity,
- track-record count,
- dominant / matching sector size,
- dominant / matching sectors per track.

All `diskdefs` remain eligible for probing. Narrowing by geometry is used only to improve ranking.

The probing client also removes duplicate format names before helper processes
are started.

### Prioritized search batches

Candidates are grouped by priority and tested in batches. The current
implementation uses:

- up to **7** candidates per batch
- up to **42** total prioritized candidates

If a priority tier produces one or more successful mounts, lower-priority tiers are not probed further. However, all candidates from the successful tier are still checked so that aliases or near-equivalent `diskdef` entries are visible to the user.

## Image metainformation details

The virtual file with image details -- metainformation, named  `__CPM_DISK_INFO__.TXT`, is formed in memory and is not stored in the CP/M directory or in the disk image. It can be viewed with **F3** or copied out of the archive. Deleting it or copying it back has no effect and does not modify the image.

The generated text uses an INI-like format and includes:

- image/container name, physical size, logical disk size, and write support,
- selected `diskdef`, format-selection source, and probing information,
- sector, track, block, boot-area, offset, skew, and extent parameters,
- CP/M variant and supported filesystem features,
- used/free allocation blocks and directory entries,
- logical file count, physical extent count, and user areas present,
- CP/M Plus disk label, label password, and label timestamps,
- CP/M Plus per-file password entries, protection modes, and decoded passwords,
- the level of structural validation performed while mounting the image.

CP/M Plus passwords are intentionally shown as plain text. They use a reversible one-byte XOR encoding and are treated as historical metadata.

The name does not conform to CP/M 8.3 naming rules, so it cannot conflict with a real file stored on a supported CP/M disk.

The pseudo-file can be disabled in `cpmdiskimg.ini`.


## Logging

The text log records the probing outcome, for example:

```text
Info# IMD logical size 409600 bytes, 80 track records
Info# Probe kpiv: 100/100; Mount OK; 24 files, 31 extents, 46 blocks
Info# Probe zen7: 5/100; Mount rejected
Info# No candidate passed the isolated mount test (42 prioritized formats tested, search limit reached); select the format manually.
```

If no format passes the isolated test, the dialog still opens and allows manual selection.



# Plans

- Continue cleaning up the code.
- Improve image-type detection using additional container metadata and CP/M filesystem consistency checks.
- Allow changing the image type after an incorrect format has been mounted "successfully".
- Convert more LibDsk failures into plugin error codes.
- Possibly detect the logical end of CP/M text files by searching for the `0x1A` byte.


# Credits

Many thanks to the libdsk, cpmtools creators, and those who preserve the CP/M software and create corresponding disk images.


