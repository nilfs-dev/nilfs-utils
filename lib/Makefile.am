## Makefile.am
##
## Copyright (C) 2007-2009 Nippon Telegraph and Telephone Corporation.
##
## Written by Koji Sato <koji@osrg.net>,
##            Ryusuke Konishi <ryusuke@osrg.net>.

CURRENT = 0
REVISION = 0
AGE = 0
VERSIONINFO = $(CURRENT):$(REVISION):$(AGE)

lib_LTLIBRARIES = libnilfs.la
noinst_LTLIBRARIES = libnilfsmisc.la

libnilfsmisc_la_SOURCES = crc32.c realpath.c
libnilfsmisc_la_CFLAGS = -Wall -fPIC
libnilfsmisc_la_CPPFLAGS = -I$(top_srcdir)/include
libnilfsmisc_la_LDFLAGS = -static

libnilfs_la_SOURCES = nilfs.c sb.c $(COMMONSOURCES)
libnilfs_la_CFLAGS = -Wall
libnilfs_la_CPPFLAGS = -I$(top_srcdir)/include
libnilfs_la_LDFLAGS = -version-info $(VERSIONINFO)
libnilfs_la_LIBADD = libnilfsmisc.la

install-exec-hook:
	$(LDCONFIG) $(DESTDIR)$(libdir)
