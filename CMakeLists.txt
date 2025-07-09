# Configure by:
# Using MSVC 2019 and create 64-bit plugin:
#  cmake -A x64   .. -G"Visual Studio 16" 	
# Using MSVC 2019 and create 32-bit plugin:
#  cmake -A Win32 .. -G"Visual Studio 16" 
#
# To enable FLTK usage:
#  cmake -A x64   .. -G"Visual Studio 16" -DFLTK_ENABLED_EXPERIMENTAL=1
#  cmake -A Win32 .. -G"Visual Studio 16" -DFLTK_ENABLED_EXPERIMENTAL=1
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
elseif(CMAKE_SIZEOF_VOID_P EQUAL 4)
	set(SOURCE_FILES ${SOURCE_FILES} cpmimg_32.def)
endif()

add_library(cpmimg_wcx SHARED ${SOURCE_FILES} )
set_target_properties(cpmimg_wcx PROPERTIES OUTPUT_NAME cpmimg)
if(CMAKE_SIZEOF_VOID_P EQUAL 8)	
	set_target_properties(cpmimg_wcx PROPERTIES SUFFIX .wcx64 PREFIX "") 
elseif(CMAKE_SIZEOF_VOID_P EQUAL 4)
	set_target_properties(cpmimg_wcx PROPERTIES SUFFIX .wcx PREFIX "") 	
endif()

target_link_libraries(cpmimg_wcx PRIVATE cpmfs libdsk) 

# target_compile_options(cpmimg_wcx PRIVATE "/fsanitize=address") # RTC1 - x86 only?

add_executable(cpmls cpmtools/cpmls.c  cpmtools/getopt.c)
target_link_libraries(cpmls cpmfs libdsk)

add_executable(mkfs.cpm cpmtools/mkfs.cpm.c  cpmtools/getopt.c)
target_link_libraries(mkfs.cpm cpmfs libdsk)


if( DEFINED FLTK_ENABLED_EXPERIMENTAL)
find_package(FLTK CONFIG)

if( FLTK_FOUND )
target_compile_definitions(cpmimg_wcx PRIVATE -DFLTK_ENABLED_EXPERIMENTAL=1)
target_include_directories(cpmimg_wcx PRIVATE ${FLTK_INCLUDE_DIR})
target_link_libraries(cpmimg_wcx PRIVATE fltk fltk_gl fltk_forms fltk_images)
endif()

endif()

# https://www.ghisler.ch/wiki/index.php?title=Plugins_Automated_Installation