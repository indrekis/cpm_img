# Configure by:
# Using MSVC 2019 and create 64-bit plugin:
#  cmake -A x64   .. -G"Visual Studio 17" -DCMAKE_TOOLCHAIN_FILE="<VCPKG_PATH>/vcpkg/scripts/buildsystems/vcpkg.cmake"	 
# Using MSVC 2019 and create 32-bit plugin:
#  cmake -A Win32 .. -G"Visual Studio 17" -DCMAKE_TOOLCHAIN_FILE="<VCPKG_PATH>/vcpkg/scripts/buildsystems/vcpkg.cmake" -DVCPKG_TARGET_TRIPLET=x86-windows-static	
#
# Important: the simplest way to support the 32-bit and 64-bit coexistence is to use the static linking of libdsk with zlib and bz2.
# So they are built as static libraries using x86-windows-static triplet: "vcpkg install zlib:x86-windows-static", "vcpkg install bzip2:x86-windows-static".
#
# Build by: 
#  cmake --build . --config Release 
# or any of:
#  cmake --build . --config [Debug|RelWithDebInfo|MinSizeRel]
# otherwise ('cmake --build .') could lead to /MD vs /MDd vs others mismatch:
# `warning LNK4098: defaultlib 'MSVCRT' conflicts with use of other libs fltk`

cmake_minimum_required(VERSION 3.15)
project(CPMimage VERSION 0.99 LANGUAGES C CXX)

# BEGIN CPMIMG VERSION COMPONENT DEFAULTS
set(CPMIMG_VERSION_MAJOR "${PROJECT_VERSION_MAJOR}")
set(CPMIMG_VERSION_MINOR "${PROJECT_VERSION_MINOR}")
set(CPMIMG_VERSION_PATCH "${PROJECT_VERSION_PATCH}")
set(CPMIMG_VERSION_TWEAK "${PROJECT_VERSION_TWEAK}")

if(CPMIMG_VERSION_PATCH STREQUAL "")
    set(CPMIMG_VERSION_PATCH 0)
endif()

if(CPMIMG_VERSION_TWEAK STREQUAL "")
    set(CPMIMG_VERSION_TWEAK 0)
endif()
# END CPMIMG VERSION COMPONENT DEFAULTS
# BEGIN CPMIMG RELEASE PACKAGING
if(MSVC)
    # Keep the plugin and vendored LibDsk self-contained and compatible with
    # the static vcpkg zlib/bzip2 triplets used by the release build.
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

set(CPMIMG_VERSION_TWEAK_VALUE "${PROJECT_VERSION_TWEAK}")
if(CPMIMG_VERSION_TWEAK_VALUE STREQUAL "")
    set(CPMIMG_VERSION_TWEAK_VALUE 0)
endif()

configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/packaging/pluginst.inf.in"
    "${CMAKE_CURRENT_BINARY_DIR}/pluginst.inf"
    @ONLY
)

configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/packaging/cpmimg_version.h.in"
    "${CMAKE_CURRENT_BINARY_DIR}/cpmimg_version.h"
    @ONLY
)
# END CPMIMG RELEASE PACKAGING


add_subdirectory(libdsk_cmake) 
include_directories(${PROJECT_SOURCE_DIR}/libdsk_cmake/include)

# Definitions for the cmpfs, temporary
add_compile_definitions(FORMAT="osbexec1")

set(CMAKE_CXX_STANDARD 20)

add_library(cpmfs STATIC cpmtools/device_libdsk.c cpmtools/cpmfs.c )
target_compile_definitions(cpmfs PUBLIC NOTWINDLL)

set(SOURCE_FILES cpmimg_wcx.cpp  minimal_fixed_string.h  resource.h  sysio_winapi.cpp  sysio_winapi.h wcxhead.h main_resources.rc
string_tools.cpp string_tools.h plugin_config.cpp plugin_config.h 
cpmimg_plugin_gui.cpp cpmimg_plugin_gui.h cpmimg_plugin_gui.rc cpmimg_plugin_gui_resources.h cpmimg_probe_client.cpp cpmimg_probe_client.h 
	)

if(CMAKE_SIZEOF_VOID_P EQUAL 8)	
	set(SOURCE_FILES ${SOURCE_FILES} cpmimg_64.def)
	set(BITTNES_SUFIX "")
elseif(CMAKE_SIZEOF_VOID_P EQUAL 4)
	set(SOURCE_FILES ${SOURCE_FILES} cpmimg_32.def)
	set(BITTNES_SUFIX "_32")
endif()


# Isolated format probe. Each candidate runs in its own process so faults in
# legacy cpmfs/libdsk code cannot terminate Total Commander.
add_executable(cpmimg_probe cpmimg_probe_main.cpp)
target_link_libraries(cpmimg_probe PRIVATE cpmfs libdsk${BITTNES_SUFIX})

if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    set_target_properties(cpmimg_probe PROPERTIES OUTPUT_NAME cpmimg_probe64)
else()
    set_target_properties(cpmimg_probe PROPERTIES OUTPUT_NAME cpmimg_probe32)
endif()

add_library(cpmimg_wcx SHARED ${SOURCE_FILES} )
add_dependencies(cpmimg_wcx cpmimg_probe) # Build the isolated probe before packaging the WCX plugin
set_target_properties(cpmimg_wcx PROPERTIES OUTPUT_NAME cpmimg)
if(CMAKE_SIZEOF_VOID_P EQUAL 8)	
	set_target_properties(cpmimg_wcx PROPERTIES SUFFIX .wcx64 PREFIX "") 
elseif(CMAKE_SIZEOF_VOID_P EQUAL 4)
	set_target_properties(cpmimg_wcx PROPERTIES SUFFIX .wcx PREFIX "") 	
endif()

target_link_libraries(cpmimg_wcx PRIVATE cpmfs libdsk${BITTNES_SUFIX}) 
target_compile_definitions(cpmimg_wcx PRIVATE NOTWINDLL)

# BEGIN CPMIMG INSTALL RULES
if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(CPMIMG_PACKAGE_ARCH x64)
else()
    set(CPMIMG_PACKAGE_ARCH x86)
endif()

target_include_directories(
    cpmimg_wcx
    PRIVATE "${CMAKE_CURRENT_BINARY_DIR}"
)

install(
    TARGETS cpmimg_wcx
    RUNTIME
        DESTINATION .
        COMPONENT cpmimg_runtime
    LIBRARY
        DESTINATION .
        COMPONENT cpmimg_runtime
)

install(
    FILES
        "${CMAKE_CURRENT_SOURCE_DIR}/diskdefs"
        "${CMAKE_CURRENT_SOURCE_DIR}/ReadMe.md"
        "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE.txt"
        "${CMAKE_CURRENT_BINARY_DIR}/pluginst.inf"
    DESTINATION .
    COMPONENT cpmimg_runtime
)

install(
    FILES "${CMAKE_CURRENT_SOURCE_DIR}/cpmtools/COPYING"
    DESTINATION licenses
    RENAME cpmtools-COPYING.txt
    COMPONENT cpmimg_runtime
)

install(
    FILES "${CMAKE_CURRENT_SOURCE_DIR}/libdsk_cmake/COPYING"
    DESTINATION licenses
    RENAME libdsk-COPYING.txt
    COMPONENT cpmimg_runtime
)

if(MSVC)
    install(
        FILES "$<TARGET_PDB_FILE:cpmimg_wcx>"
        DESTINATION "symbols/${CPMIMG_PACKAGE_ARCH}"
        OPTIONAL
        COMPONENT cpmimg_symbols
    )
endif()

install(
    FILES "${CMAKE_CURRENT_SOURCE_DIR}/THIRD_PARTY_NOTICES.md"
    DESTINATION .
    COMPONENT cpmimg_runtime
)

install(
    FILES
        "${CMAKE_CURRENT_SOURCE_DIR}/licenses/zlib-LICENSE.txt"
        "${CMAKE_CURRENT_SOURCE_DIR}/licenses/bzip2-LICENSE.txt"
    DESTINATION licenses
    COMPONENT cpmimg_runtime
)
install(TARGETS cpmimg_probe RUNTIME DESTINATION . COMPONENT cpmimg_runtime)

# END CPMIMG INSTALL RULES


# target_compile_options(cpmimg_wcx PRIVATE "/fsanitize=address") # RTC1 - x86 only?

add_executable(cpmls cpmtools/cpmls.c  cpmtools/getopt.c)
target_link_libraries(cpmls cpmfs libdsk${BITTNES_SUFIX})

add_executable(mkfs.cpm cpmtools/mkfs.cpm.c  cpmtools/getopt.c)
target_link_libraries(mkfs.cpm cpmfs libdsk${BITTNES_SUFIX})


# https://www.ghisler.ch/wiki/index.php?title=Plugins_Automated_Installation