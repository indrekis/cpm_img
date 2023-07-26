cmake_minimum_required(VERSION 3.15)
project(libdsk)

# configure_file(${PROJECT_SOURCE_DIR}/config.h.cmake ${CMAKE_CURRENT_BINARY_DIR}/config.h)

# add_subdirectory(tools) 

if(VCPKG_TOOLCHAIN) # MSVC
    SET(CMAKE_C_FLAGS  "/wd4996") #_SCL_SECURE_NO_WARNINGS
endif()

set(CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS ON)


set(dsk_LIB_SRCS
    lib/dskread.c
    lib/dskwrite.c
    lib/dskcheck.c
    lib/dskstat.c
    lib/dsklphys.c
    lib/dskfmt.c
    lib/dskopen.c
    lib/dskpars.c
    lib/dskerror.c
    lib/dskseek.c
    lib/dsksecid.c
    lib/dskgeom.c
    lib/dsktread.c
    lib/dsksgeom.c
    lib/dskjni.c
    lib/dskreprt.c
    lib/dskcmt.c
    lib/dskretry.c
    lib/dskdirty.c
    lib/dsktrkid.c
    lib/dskrtrd.c
    lib/comp.h
    lib/compi.h
    lib/compress.h
    lib/compress.inc
    lib/compress.c
    lib/compsq.c
    lib/compsq.h
    lib/compgz.c
    lib/compgz.h
    lib/comptlzh.c
    lib/comptlzh.h
    lib/compbz2.c
    lib/compbz2.h
    lib/compdskf.c
    lib/compdskf.h
    lib/crctable.c
    lib/crctable.h
    lib/crc16.c
    lib/crc16.h
#    lib/rpccli.c
#    lib/rpcfuncs.h
#    lib/rpcmap.c
#    lib/rpcpack.c
#    lib/rpcserv.c
#    lib/remote.c
#    lib/remote.h
#    lib/remote.inc
#    lib/remall.h
#    lib/rpctios.c
#    lib/rpctios.h
#    lib/rpcfork.c
#    lib/rpcfork.h
#    lib/rpcfossl.c
#    lib/rpcfossl.h
#    lib/rpcwin32.c
#    lib/rpcwin32.h
    lib/drv.h
    lib/drvi.h
    lib/drivers.h
    lib/drivers.inc
    lib/drvjv3.h
    lib/drvjv3.c
    lib/drvlinux.h
    lib/drvlinux.c
    lib/drvntwdm.h
    lib/drvntwdm.c
    lib/drvwin32.h
    lib/drvwin32.c
    lib/w95defs.h
    lib/drvwin16.h
    lib/drvwin16.c
    lib/w16defs.h
    lib/drvint25.h
    lib/drvint25.c
    lib/drvdos16.h
    lib/drvdos16.c
    lib/drvdos32.h
    lib/drvdos32.c
    lib/drvcpcem.h
    lib/drvcpcem.c
    lib/drvdskf.h
    lib/drvdskf.c
    lib/drvimd.h
    lib/drvimd.c
    lib/drvlogi.h
    lib/drvlogi.c
    lib/drvsimh.h
    lib/drvsimh.c
    lib/drvposix.h
    lib/drvposix.c
    lib/drvnwasp.h
    lib/drvnwasp.c
    lib/drvadisk.h
    lib/drvadisk.c
    lib/drvrcpm.h
    lib/drvrcpm.c
    lib/drvtele.h
    lib/drvtele.c
    lib/drvmyz80.h
    lib/drvmyz80.c
    lib/drvydsk.h
    lib/drvydsk.c
    lib/drvcfi.h
    lib/drvcfi.c
    lib/drvqm.h
    lib/drvqm.c)
     
find_package(ZLIB REQUIRED)
find_package(BZip2 REQUIRED)

add_library(libdsk SHARED ${dsk_LIB_SRCS})
include_directories(${PROJECT_SOURCE_DIR} ${PROJECT_SOURCE_DIR}/include ${ZLIB_INCLUDE_DIRS} ${BZIP2_INCLUDE_DIRS})

target_link_libraries(libdsk ZLIB::ZLIB BZip2::BZip2)


set_target_properties(libdsk PROPERTIES VERSION 3.3.13 SOVERSION 3)
add_compile_definitions(libdsk LIBDSK_EXPORTS)


install(TARGETS libdsk ${INSTALL_TARGETS_DEFAULT_ARGS})

#-------------------------------------------------------------------

set(dsktrans_SRCS
    tools/dsktrans.c
    tools/utilopts.c
    tools/utilopts.h
    tools/formname.c
    tools/formname.h
    tools/apriboot.h
    tools/bootsec.c)

add_executable(dsktrans  ${dsktrans_SRCS} )

target_link_libraries(dsktrans libdsk  ZLIB::ZLIB BZip2::BZip2)

install(TARGETS dsktrans ${INSTALL_TARGETS_DEFAULT_ARGS})

#-------------------------------------------------------------------

set(dskform_SRCS
    tools/dskform.c
    tools/utilopts.c
    tools/utilopts.h
    tools/formname.c
    tools/formname.h)

add_executable(dskform  ${dskform_SRCS} )

target_link_libraries(dskform libdsk  ZLIB::ZLIB BZip2::BZip2)

install(TARGETS dskform ${INSTALL_TARGETS_DEFAULT_ARGS})

#-------------------------------------------------------------------

set(dskid_SRCS 
    tools/dskid.c 
    tools/utilopts.c 
    tools/utilopts.h)

add_executable(dskid  ${dskid_SRCS} )

target_link_libraries(dskid libdsk  ZLIB::ZLIB BZip2::BZip2)

install(TARGETS dskid ${INSTALL_TARGETS_DEFAULT_ARGS})

#-------------------------------------------------------------------
set(dskdump_SRCS
    tools/dskdump.c
    tools/utilopts.c
    tools/utilopts.h
    tools/formname.c
    tools/formname.h)

add_executable(dskdump  ${dskdump_SRCS} )

target_link_libraries(dskdump libdsk  ZLIB::ZLIB BZip2::BZip2)

install(TARGETS dskdump ${INSTALL_TARGETS_DEFAULT_ARGS})

#-------------------------------------------------------------------


#-------------------------------------------------------------------