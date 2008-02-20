## Makefile.am
##
## Copyright (C) 2007 Nippon Telegraph and Telephone Corporation.
##
## Written by Koji Sato <koji@osrg.net>.

CURRENT = 0
REVISION = 0
AGE = 0
VERSIONINFO = $(CURRENT):$(REVISION):$(AGE)

lib_LTLIBRARIES = libnilfs.la
libnilfs_la_SOURCES = nilfs.c crc32.c
libnilfs_la_CFLAGS = -Wall
libnilfs_la_CPPFLAGS = -I$(top_srcdir)/include
libnilfs_la_LDFLAGS = -version-info $(VERSIONINFO)

install-exec-hook:
	$(LDCONFIG) $(DESTDIR)$(libdir)
