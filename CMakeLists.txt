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
project(CPMimage)

add_subdirectory(libdsk_cmake) 
include_directories(${PROJECT_SOURCE_DIR}/libdsk_cmake/include)

# Definitions for the cmpfs, temporary
add_compile_definitions(FORMAT="osbexec1")

set(CMAKE_CXX_STANDARD 20)

add_library(cpmfs STATIC cpmtools/device_libdsk.c cpmtools/cpmfs.c )

set(SOURCE_FILES cpmimg_wcx.cpp  minimal_fixed_string.h  resource.h  sysio_winapi.cpp  sysio_winapi.h wcxhead.h main_resources.rc
string_tools.cpp string_tools.h plugin_config.cpp plugin_config.h 
cpmimg_plugin_gui.cpp cpmimg_plugin_gui.h cpmimg_plugin_gui.rc cpmimg_plugin_gui_resources.h 
	)

if(CMAKE_SIZEOF_VOID_P EQUAL 8)	
	set(SOURCE_FILES ${SOURCE_FILES} cpmimg_64.def)
	set(BITTNES_SUFIX "")
elseif(CMAKE_SIZEOF_VOID_P EQUAL 4)
	set(SOURCE_FILES ${SOURCE_FILES} cpmimg_32.def)
	set(BITTNES_SUFIX "_32")
endif()

add_library(cpmimg_wcx SHARED ${SOURCE_FILES} )
set_target_properties(cpmimg_wcx PROPERTIES OUTPUT_NAME cpmimg)
if(CMAKE_SIZEOF_VOID_P EQUAL 8)	
	set_target_properties(cpmimg_wcx PROPERTIES SUFFIX .wcx64 PREFIX "") 
elseif(CMAKE_SIZEOF_VOID_P EQUAL 4)
	set_target_properties(cpmimg_wcx PROPERTIES SUFFIX .wcx PREFIX "") 	
endif()

target_link_libraries(cpmimg_wcx PRIVATE cpmfs libdsk${BITTNES_SUFIX}) 

# target_compile_options(cpmimg_wcx PRIVATE "/fsanitize=address") # RTC1 - x86 only?

add_executable(cpmls cpmtools/cpmls.c  cpmtools/getopt.c)
target_link_libraries(cpmls cpmfs libdsk${BITTNES_SUFIX})

add_executable(mkfs.cpm cpmtools/mkfs.cpm.c  cpmtools/getopt.c)
target_link_libraries(mkfs.cpm cpmfs libdsk${BITTNES_SUFIX})


# https://www.ghisler.ch/wiki/index.php?title=Plugins_Automated_Installation