## Makefile.am

CURRENT = 0
REVISION = 0
AGE = 0
VERSIONINFO = $(CURRENT):$(REVISION):$(AGE)

lib_LTLIBRARIES = libnilfs.la
noinst_LTLIBRARIES = libnilfsmisc.la

libnilfsmisc_la_SOURCES = crc32.c realpath.c cno.c
libnilfsmisc_la_CFLAGS = -Wall -fPIC -pthread
libnilfsmisc_la_CPPFLAGS = -I$(top_srcdir)/include
libnilfsmisc_la_LDFLAGS = -static -lpthread

libnilfs_la_SOURCES = nilfs.c sb.c
libnilfs_la_CFLAGS = -Wall
libnilfs_la_CPPFLAGS = -I$(top_srcdir)/include
libnilfs_la_LDFLAGS = -version-info $(VERSIONINFO)
libnilfs_la_LIBADD = libnilfsmisc.la -lpthread
