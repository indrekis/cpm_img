cmake_minimum_required(VERSION 3.15)
project(libdsk)

# configure_file(${PROJECT_SOURCE_DIR}/config.h.cmake ${CMAKE_CURRENT_BINARY_DIR}/config.h)

# add_subdirectory(tools) 

if(VCPKG_TOOLCHAIN) # MSVC
    SET(CMAKE_C_FLAGS  "/wd4996") #_SCL_SECURE_NO_WARNINGS
    include_directories(${PROJECT_SOURCE_DIR}/msvc_dirent)
endif()

set(CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS ON)

if(CMAKE_SIZEOF_VOID_P EQUAL 8)	
	set(BITTNES_SUFIX "")
elseif(CMAKE_SIZEOF_VOID_P EQUAL 4)
	set(BITTNES_SUFIX "_32")
endif()



set(dsk_LIB_SRCS

lib/blast.c    
lib/blast.h    
lib/comp.h     
lib/compbz2.c  
lib/compbz2.h  
lib/compdskf.c 
lib/compdskf.h 
lib/compgz.c   
lib/compgz.h   
lib/compi.h    
lib/compqrst.c 
lib/compqrst.h         
lib/compress.c         
lib/compress.h         
lib/compsq.c           
lib/compsq.h   
lib/comptlzh.c 
lib/comptlzh.h 
lib/crc16.c    
lib/crc16.h    
lib/crctable.c 
lib/crctable.h 
lib/drivers.h  
lib/drv.h      
lib/drvadisk.c 
lib/drvadisk.h 
lib/drvcfi.c   
lib/drvcfi.h   
lib/drvcpcem.c 
lib/drvcpcem.h 
lib/drvdc42.c  
lib/drvdc42.h  
lib/drvdos16.c 
lib/drvdos16.h 
lib/drvdos32.c 
lib/drvdos32.h 
lib/drvdskf.c  
lib/drvdskf.h  
lib/drvgotek.c 
lib/drvgotek.h 
lib/drvi.h     
lib/drvimd.c   
lib/drvimd.h   
lib/drvint25.c 
lib/drvint25.h 
lib/drvjv3.c   
lib/drvjv3.h   
lib/drvldbs.c  
lib/drvldbs.h  
lib/drvldbst.c 
lib/drvldbst.h 
lib/drvlinux.c 
lib/drvlinux.h 
lib/drvlogi.c  
lib/drvlogi.h  
lib/drvmyz80.c 
lib/drvmyz80.h 
lib/drvntwdm.c 
lib/drvntwdm.h 
lib/drvnwasp.c 
lib/drvnwasp.h
lib/drvposix.c
lib/drvposix.h
lib/drvqm.c   
lib/drvqm.h   
lib/drvqrst.c 
lib/drvqrst.h 
lib/drvrcpm.c 
lib/drvrcpm.h 
lib/drvsap.c  
lib/drvsap.h  
lib/drvsimh.c 
lib/drvsimh.h 
lib/drvtele.c 
lib/drvtele.h 
lib/drvwin16.c 
lib/drvwin16.h 
lib/drvwin32.c 
lib/drvwin32.h 
lib/drvydsk.c  
lib/drvydsk.h  
lib/dskcheck.c 
lib/dskcmt.c   
lib/dskcopy.c  
lib/dskdiag.c  
lib/dskdirty.c 
lib/dskerror.c 
lib/dskfmt.c   
lib/dskgeom.c  
lib/dskiconv.c 
lib/dskjni.c   
lib/dsklphys.c 
lib/dskopen.c  
lib/dskpars.c  
lib/dskread.c  
lib/dskreprt.c 
lib/dskretry.c 
lib/dskrtrd.c  
lib/dsksecid.c 
lib/dskseek.c  
lib/dsksgeom.c 
lib/dskstat.c  
lib/dsktread.c 
lib/dsktrkid.c 
lib/dskwrite.c 
lib/ldbs.c     
lib/ldbs.h     
lib/ldbs2txt.c 
lib/remall.h   
# lib/remote.c   
# lib/remote.h   
# lib/rpccli.c   
# lib/rpcfork.c  
# lib/rpcfork.h  
# lib/rpcfossl.c 
# lib/rpcfossl.h 
# lib/rpcfuncs.h
# lib/rpcmap.c
# lib/rpcpack.c
# lib/rpcserv.c
# lib/rpctios.c
# lib/rpctios.h
# lib/rpcwin32.c
# lib/rpcwin32.h
lib/txt2ldbs.c
#lib/uk_co_demon_seasip_libdsk_Drive.h
#lib/uk_co_demon_seasip_libdsk_FormatType.h
#lib/uk_co_demon_seasip_libdsk_Geometry.h
#lib/uk_co_demon_seasip_libdsk_LibDsk.h 
lib/w16defs.h
lib/w95defs.h 
)     
     
find_package(ZLIB REQUIRED)
find_package(BZip2 REQUIRED)

add_library(libdsk${BITTNES_SUFIX} SHARED ${dsk_LIB_SRCS})
include_directories(${PROJECT_SOURCE_DIR} ${PROJECT_SOURCE_DIR}/include ${ZLIB_INCLUDE_DIRS} ${BZIP2_INCLUDE_DIRS})

target_link_libraries(libdsk${BITTNES_SUFIX} ZLIB::ZLIB BZip2::BZip2)

set_target_properties(libdsk${BITTNES_SUFIX} PROPERTIES VERSION 3.3.13 SOVERSION 3)
add_compile_definitions(libdsk${BITTNES_SUFIX} LIBDSK_EXPORTS)


install(TARGETS libdsk${BITTNES_SUFIX} ${INSTALL_TARGETS_DEFAULT_ARGS})

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

target_link_libraries(dsktrans libdsk${BITTNES_SUFIX}  ZLIB::ZLIB BZip2::BZip2)

install(TARGETS dsktrans ${INSTALL_TARGETS_DEFAULT_ARGS})

#-------------------------------------------------------------------

set(dskform_SRCS
    tools/dskform.c
    tools/utilopts.c
    tools/utilopts.h
    tools/formname.c
    tools/formname.h)

add_executable(dskform  ${dskform_SRCS} )

target_link_libraries(dskform libdsk${BITTNES_SUFIX}  ZLIB::ZLIB BZip2::BZip2)

install(TARGETS dskform ${INSTALL_TARGETS_DEFAULT_ARGS})

#-------------------------------------------------------------------

set(dskid_SRCS 
    tools/dskid.c 
    tools/utilopts.c 
    tools/utilopts.h)

add_executable(dskid  ${dskid_SRCS} )

target_link_libraries(dskid libdsk${BITTNES_SUFIX}  ZLIB::ZLIB BZip2::BZip2)

install(TARGETS dskid ${INSTALL_TARGETS_DEFAULT_ARGS})

#-------------------------------------------------------------------
set(dskdump_SRCS
    tools/dskdump.c
    tools/utilopts.c
    tools/utilopts.h
    tools/formname.c
    tools/formname.h)

add_executable(dskdump  ${dskdump_SRCS} )

target_link_libraries(dskdump libdsk${BITTNES_SUFIX}  ZLIB::ZLIB BZip2::BZip2)

install(TARGETS dskdump ${INSTALL_TARGETS_DEFAULT_ARGS})

#-------------------------------------------------------------------


#-------------------------------------------------------------------