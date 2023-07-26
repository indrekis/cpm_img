
#
############ next target ###############
#
#set(dskscan_SRCS
#    dskscan.c
#    utilopts.c
#    utilopts.h
#    formname.c
#    formname.h)
#
#kde4_add_executable(dskscan ${dskscan_SRCS})
#
#target_link_libraries(dskscan ${KDE4_KDECORE_LIBS})
#
#install(TARGETS dskscan ${INSTALL_TARGETS_DEFAULT_ARGS})
#
#
############ next target ###############
#
#set(dskutil_SRCS
#    dskutil.c
#    utilopts.c
#    utilopts.h
#    formname.c
#    formname.h)
#
#kde4_add_executable(dskutil ${dskutil_SRCS})
#
#target_link_libraries(dskutil ${KDE4_KDECORE_LIBS})
#
#install(TARGETS dskutil ${INSTALL_TARGETS_DEFAULT_ARGS})
#
#
############ next target ###############
#
#set(md3serial_SRCS md3serial.c utilopts.c utilopts.h)
#
#kde4_add_executable(md3serial ${md3serial_SRCS})
#
#target_link_libraries(md3serial ${KDE4_KDECORE_LIBS})
#
#install(TARGETS md3serial ${INSTALL_TARGETS_DEFAULT_ARGS})
#
#
############ next target ###############
#
#set(apriboot_SRCS
#    apriboot.c
#    bootsec.c
#    apriboot.h
#    formname.c
#    formname.h
#    utilopts.c
#    utilopts.h)
#
#kde4_add_executable(apriboot ${apriboot_SRCS})
#
#target_link_libraries(apriboot ${KDE4_KDECORE_LIBS})
#
#install(TARGETS apriboot ${INSTALL_TARGETS_DEFAULT_ARGS})
#
#
############ next target ###############
#
#set(@TOOLCLASSES@_SRCS ${CMAKE_CURRENT_BINARY_DIR}/dummy.cpp)
#
#kde4_add_executable(@TOOLCLASSES@ ${@TOOLCLASSES@_SRCS})
#
#target_link_libraries(@TOOLCLASSES@ ${KDE4_KDECORE_LIBS})
#
#
############ next target ###############
#
#set(forkslave_SRCS ${CMAKE_CURRENT_BINARY_DIR}/dummy.cpp)
#
#kde4_add_executable(forkslave ${forkslave_SRCS})
#
#target_link_libraries(forkslave ${KDE4_KDECORE_LIBS})
#
#
############ next target ###############
#
#set(dsktest_SRCS ${CMAKE_CURRENT_BINARY_DIR}/dummy.cpp)
#
#kde4_add_executable(dsktest ${dsktest_SRCS})
#
#target_link_libraries(dsktest ${KDE4_KDECORE_LIBS})
#
#
############ next target ###############
#
#set(serslave_SRCS ${CMAKE_CURRENT_BINARY_DIR}/dummy.cpp)
#
#kde4_add_executable(serslave ${serslave_SRCS})
#
#target_link_libraries(serslave ${KDE4_KDECORE_LIBS})
#
#
############ next target ###############
#
#set(check1_SRCS check1.c)
#
#if(KDE4_BUILD_TESTS)
#
#kde4_add_executable(check1 ${check1_SRCS})
#
#target_link_libraries(check1 ${KDE4_KDECORE_LIBS})
#
#endif(KDE4_BUILD_TESTS)
#
############ next target ###############
#
#set(check2_SRCS check2.c)
#
#if(KDE4_BUILD_TESTS)
#
#kde4_add_executable(check2 ${check2_SRCS})
#
#target_link_libraries(check2 ${KDE4_KDECORE_LIBS})
#
#endif(KDE4_BUILD_TESTS)
#
############ next target ###############
#
#set(check3_SRCS check3.c)
#
#if(KDE4_BUILD_TESTS)
#
#kde4_add_executable(check3 ${check3_SRCS})
#
#target_link_libraries(check3 ${KDE4_KDECORE_LIBS})
#
#endif(KDE4_BUILD_TESTS)
#
############ install files ###############




#original Makefile.am contents follow:

### Process this file with automake to produce Makefile.in
#
#AM_CPPFLAGS=-I$(top_srcdir)/include
#LDADD=../lib/libdsk.la
#
#CLASSPATH=@CLASSPATH@
#JAVAC=@JAVAC@ 
#
#bin_PROGRAMS=dsktrans dskform dskid dskdump dskscan dskutil md3serial apriboot
#dsktrans_SOURCES=dsktrans.c utilopts.c utilopts.h formname.c formname.h \
#		 apriboot.h bootsec.c
#apriboot_SOURCES=apriboot.c bootsec.c apriboot.h formname.c formname.h \
#		 utilopts.c utilopts.h
#dskform_SOURCES=dskform.c utilopts.c utilopts.h formname.c formname.h
#dskid_SOURCES=dskid.c utilopts.c utilopts.h
#md3serial_SOURCES=md3serial.c utilopts.c utilopts.h
#dskscan_SOURCES=dskscan.c utilopts.c utilopts.h formname.c formname.h
#dskdump_SOURCES=dskdump.c utilopts.c utilopts.h formname.c formname.h
#dskutil_SOURCES=dskutil.c utilopts.c utilopts.h formname.c formname.h
#forkslave_SOURCES=forkslave.c
#serslave_SOURCES=serslave.c crc16.c crc16.h
#dsktest_SOURCES=dsktest.c utilopts.c utilopts.h
#
#noinst_PROGRAMS=@TOOLCLASSES@ forkslave dsktest serslave 
#EXTRA_PROGRAMS=
#EXTRA_DIST=DskTrans.java DskFormat.java DskID.java FormatNames.java UtilOpts.java ScreenReporter.java
#
#check_PROGRAMS = check1 check2 check3
#check1_SOURCES = check1.c
#check2_SOURCES = check2.c
#check3_SOURCES = check3.c
#CLEANFILES=*.class
#
#%.class:        $(srcdir)/%.java
#	here=`pwd` && cd $(srcdir) && $(JAVAC) -classpath $(CLASSPATH):$$here/../lib/libdsk.jar -d $$here $<
#
