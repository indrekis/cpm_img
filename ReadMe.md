- [Introduction](#introduction)
- [Installation](#installation)
- [Plugin configuration](#plugin-configuration)
- [Compilation](#compilation)
- [Preparing images for tests](#preparing-images-for-tests)
- [Problems and limitations](#problems-and-limitations)
  - [Non-problems but caveats](#non-problems-but-caveats)
  - [Plans](#plans)
- [Credits](#credits)


# Introduction

CPMimg is a wcx (archive) plugin for 64-bit ~~and 32-bit~~ Total Commander (TCmd) that provides read-write access to CP/M disk images.

Key features:

- Supports read-write access to CP/M any disk formats supported by the [libdsk](https://www.seasip.info/Unix/LibDsk/) and [cpmtools](https://github.com/lipro-cpm4l/cpmtools).
 - So, TD0 format support is read-only.
 - Note: CP/M images are extremely diverse and mostly saved using a dedicated archiving format, containing disk structure information. Most popular are [ImageDisk](http://dunfield.classiccmp.org/img/) (.IMD) and [Teledisk](http://dunfield.classiccmp.org/img42841/teledisk.htm) (.TD0).
 - Libdsk required extensive though trivial patching because of command-line oriented error handling. 
- The config file selects the current image format based on the ``diskdefs`` file in ``cpmtools`` format. If the image cannot be opened using this format, it asks for the format to use, showing the compatible or partially compatible formats.
  - If no such formats are found, it will show all possible formats.

# Installation

As usual for the TCmd plugins:

- Manual installation:
   1. Unzip the WCX to the directory of your choice (e.g., c:\wincmd\plugins\wcx)
   2. In TCmd, choose **Configuration** -> **Options**
   3. Open the '**Packer**' page
   4. Click '**Configure packer extension WCXs**'
   5. Type '*img*' as the extension
   6. Click '**New type**', and select the *img.wcx64* (*img.wcx* for 32-bit TCmd)
   7. Click **OK**
- Automated installation:
   1. Open the archive containing the plugin directly in TCmd.
   2. The program will prompt you to install the plugin.

Settings are stored in the `cpmdiskimg.ini` file in the configuration path provided by the TCmd (usually in the same place where wincmd.ini is located). The configuration file will be recreated with default values if missing or corrupted.

Binary releases available [here](https://github.com/indrekis/cpm_img/releases).

**This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.**  

# Plugin configuration

The configuration file, named cpmdiskimg.ini, is searched using the path provided by the TCmd (most often, the path where wincmd.ini is located). If the configuration file is absent or incorrect, it is created with the default configuration.

Except for the logging options, the plugin currently rereads the configuration before opening each image (archive).

Options can also be changed from the "Options" dialog of the packing dialog.

Example configuration file:

```
[CPM_disk_img_plugin]
allow_dialogs=0
allow_txt_log=1
log_file_path=D:\Temp\cpmimg.txt
debug_level=1

image_format=osbexec1
diskdefs_file_path=d:\totalcmd3\plugins32\wcx\img\diskdefs
```

- `allow_dialogs==1` -- enable dialogs on some image peculiarities. 
 - Because of the write options dialog, FLTK is always linked to the plugin, but those dialogs can be annoying anyway, so the user can still disable them.
- `allow_txt_log==1` -- allows detailed log output, describing image properties and anomalies. It can noticeably slow down the plugin, and it is not for general use, as it can lead to problems. It is helpful for in-depth image analysis.  
 - Additionally, important image analysis events are logged to the debug console for the debug builds (without NDEBUG defined). In addition to using a full-fledged debugger, debugging output can be seen using [SimpleProgramDebugger](http://www.nirsoft.net/utils/simple_program_debugger.html).
- `log_file_path=<filename>` -- logging filename. If opening this file for writing fails, logging is disabled (allow_txt_log==0). The file is created from scratch at the first use of the plugin during the current TCmd session.
- `debug_level` -- not used now.
- `image_format` -- default used format from the file selected by ``diskdefs_file_path``.

# Compilation

Code can be compiled using the Visual Studio project or CMakeLists.txt (tested using MSVC and MinGW). 

Uses C++20, with no obligatory external dependencies. 

Examples of the command lines to compile using CMake are in the CMakeLists.txt.

# Preparing images for tests

The plugin was tested using two kinds of images:

- Images of virtual machines and emulators used for different tasks.
- Historical floppy images from the retro-computing sites.

The plugin is tested on several hundred floppy images.

# Problems and limitations

- Currently, there is only a 64-bit build, but 32-bit would not be a problem, though it requires some CMake files fiddling. 
- Sometimes goes astray -- opens file "successfully" using the wrong format. Mainly, it is manifested by an image showing many user "folders". Users are a rarely used feature, anyway. 

## Non-problems but caveats

- UI is based on WinAPI to avoid conflict with several FLTK copies. Possibly -- find another lightweight but convenient GUI toolkit.

## Plans

- Cleanup the code.
- Improve disk image type detection heuristics.
- Improve UI, allow changing the image type used while an incorrect type is used "successfully".
- Support for the [passwd] and [label] names used by the cpmtools. 
  - Possibly also add [metainfo] file and [boot] tracks support.
- Convert more errors detected by the libdsk to error codes.
- Possibly, detect the real end of the text file by searching for the 0x1A byte.

# Credits

Many thanks to the libdsk, cpmtools creators, and those who preserve the CP/M software and create corresponding disk images.