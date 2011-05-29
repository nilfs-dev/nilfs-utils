## Makefile.am

AM_CFLAGS = -Wall
AM_CPPFLAGS = -I$(top_srcdir)/include
LDADD = -luuid $(top_builddir)/lib/libnilfsfeature.la \
	$(top_builddir)/lib/libmountchk.la

sbin_PROGRAMS = mkfs.nilfs2

mkfs_nilfs2_SOURCES = mkfs.c bitops.c $(top_srcdir)/lib/crc32.c mkfs.h
