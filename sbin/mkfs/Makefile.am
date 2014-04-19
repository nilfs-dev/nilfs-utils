## Makefile.am

AM_CFLAGS = -Wall
AM_CPPFLAGS = -I$(top_srcdir)/include
LDADD = -luuid $(LIB_BLKID) $(top_builddir)/lib/libnilfsfeature.la \
	$(top_builddir)/lib/libmountchk.la \
	$(top_builddir)/lib/libcrc32.la

root_sbin_PROGRAMS = mkfs.nilfs2

mkfs_nilfs2_SOURCES = mkfs.c bitops.c mkfs.h

EXTRA_DIST = .gitignore
