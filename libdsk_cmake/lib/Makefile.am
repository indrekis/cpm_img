## Process this file with automake to produce Makefile.in

AM_CPPFLAGS=-I$(top_srcdir)/include
JAVAC=@JAVAC@
JAVACFLAGS=@JAVACFLAGS@
JAVAH=@JAVAH@
JAVA=@JAVA@
JAVADOC=@JAVADOC@
JAR=@JAR@
CLASSCPRE=uk.co.demon.seasip.libdsk
CLASSDPRE=uk/co/demon/seasip/libdsk
HEADERPRE=uk_co_demon_seasip_libdsk

lib_LTLIBRARIES=libdsk.la
noinst_PROGRAMS=@LIBDSKJAR@
EXTRA_PROGRAMS=
EXTRA_DIST=$(CLASSDPRE)/Drive.java \
	   $(CLASSDPRE)/DskException.java \
	   $(CLASSDPRE)/FormatData.java \
	   $(CLASSDPRE)/FormatType.java \
	   $(CLASSDPRE)/Geometry.java \
	   $(CLASSDPRE)/Reporter.java \
	   $(CLASSDPRE)/LibDsk.java
#
# Binary compatibility. See libtool.info for full details, but:
# If the source has changed at all, increment the middle number
# If interfaces have been added, removed or changed, increment the
# first number and set the middle one to 0.
# If interfaces have been added increment the right-hand number.
# If interfaces have been removed set the right-hand number to 0.
# 
#
libdsk_la_LDFLAGS = -version-info 9:0:4
libdsk_la_SOURCES = dskread.c  dskwrite.c dskcheck.c dskstat.c \
		   dsklphys.c dskfmt.c   dskopen.c  dskpars.c \
		   dskerror.c dskseek.c  dsksecid.c dskgeom.c \
		   dsktread.c dsksgeom.c dskjni.c   dskreprt.c \
		   dskcmt.c dskretry.c dskdirty.c dsktrkid.c dskrtrd.c \
		   dskcopy.c dskiconv.c dskdiag.c \
	  	   blast.h blast.c \
		   comp.h compi.h compress.h compress.inc compress.c \
		   compsq.c compsq.h \
		   compgz.c compgz.h \
		   comptlzh.c comptlzh.h \
		   compbz2.c compbz2.h \
		   compdskf.c compdskf.h \
		   compqrst.c compqrst.h \
		   crctable.c crctable.h \
		   crc16.c    crc16.h \
		   rpccli.c  rpcfuncs.h  rpcmap.c  \
	           rpcpack.c  rpcserv.c \
		   remote.c   remote.h  remote.inc remall.h \
		   rpctios.c  rpctios.h \
		   rpcfork.c  rpcfork.h \
		   rpcfossl.c rpcfossl.h \
		   rpcwin32.c rpcwin32.h \
		   drv.h drvi.h drivers.h drivers.inc \
		   drvjv3.h   drvjv3.c \
		   drvlinux.h drvlinux.c \
		   drvntwdm.h drvntwdm.c \
		   drvwin32.h drvwin32.c w95defs.h \
		   drvwin16.h drvwin16.c w16defs.h \
		   drvint25.h drvint25.c \
		   drvdos16.h drvdos16.c \
		   drvdos32.h drvdos32.c \
		   drvcpcem.h drvcpcem.c \
		   drvdskf.h  drvdskf.c \
		   drvimd.h   drvimd.c \
		   drvlogi.h  drvlogi.c \
		   drvsimh.h  drvsimh.c \
		   drvgotek.h drvgotek.c \
		   drvposix.h drvposix.c \
		   drvnwasp.h drvnwasp.c \
		   drvadisk.h drvadisk.c \
		   drvrcpm.h  drvrcpm.c \
		   drvsap.h   drvsap.c \
		   drvtele.h  drvtele.c \
		   drvmyz80.h drvmyz80.c \
		   drvydsk.h  drvydsk.c \
		   drvcfi.h   drvcfi.c \
		   drvqm.h    drvqm.c \
		   drvqrst.h  drvqrst.c \
		   drvdc42.h  drvdc42.c \
		   drvldbs.h  drvldbs.c ldbs.h ldbs.c \
		   drvldbst.h drvldbst.c ldbs2txt.c txt2ldbs.c

JARCLASSES=$(CLASSDPRE)/Drive.class \
	   $(CLASSDPRE)/DskException.class \
	   $(CLASSDPRE)/FormatData.class \
	   $(CLASSDPRE)/FormatType.class \
	   $(CLASSDPRE)/Geometry.class \
	   $(CLASSDPRE)/Reporter.class \
	   $(CLASSDPRE)/LibDsk.class
JARHEADERS=$(HEADERPRE)_Drive.h \
	   $(HEADERPRE)_Geometry.h \
	   $(HEADERPRE)_FormatType.h \
	   $(HEADERPRE)_LibDsk.h
CLEANFILES=$(JARCLASSES) $(JARHEADERS)

libdsk.jar:	$(JARCLASSES)
	$(JAR) c0f $@ uk/*
	$(JAVADOC) -d $(top_srcdir)/doc/jni $(CLASSCPRE)

$(CLASSDPRE)/%.class:   $(srcdir)/$(CLASSDPRE)/%.java
	here=`pwd` && cd $(srcdir) && $(JAVAC) $(JAVACFLAGS) -d $$here $<

%.class:        $(srcdir)/%.java
	here=`pwd` && cd $(srcdir) && $(JAVAC) $(JAVACFLAGS) -d $$here $<


$(HEADERPRE)_%.h:         $(CLASSDPRE)/%.class
	$(JAVAH) -jni $(CLASSCPRE).`basename $< .class`
	touch $@

$(srcdir)/dskjni.c:	$(JARHEADERS)

install-exec-local:
	$(mkinstalldirs) $(libdir)
	if test x@LIBDSKJAR@ = x; then true; else $(INSTALL_DATA) @LIBDSKJAR@ $(DESTDIR)/$(libdir); fi

