## Makefile.am

SUBDIRS = mount

AM_CFLAGS = -Wall
AM_CPPFLAGS = -I$(top_srcdir)/include
LDADD = $(top_builddir)/lib/libnilfs.la

root_sbin_PROGRAMS = mkfs.nilfs2 nilfs_cleanerd
sbin_PROGRAMS = nilfs-clean nilfs-resize nilfs-tune

mkfs_nilfs2_SOURCES = mkfs.c bitops.c mkfs.h
mkfs_nilfs2_LDADD = -luuid $(LIB_BLKID) \
	$(top_builddir)/lib/libcrc32.la \
	$(top_builddir)/lib/libmountchk.la \
	$(top_builddir)/lib/libnilfsfeature.la

nilfs_cleanerd_SOURCES = cleanerd.c cldconfig.c cldconfig.h
nilfs_cleanerd_CPPFLAGS = $(AM_CPPFLAGS) -DSYSCONFDIR=\"$(sysconfdir)\"
# Use -static option to make nilfs_cleanerd self-contained.
nilfs_cleanerd_LDFLAGS = -static
nilfs_cleanerd_LDADD = $(LDADD) $(LIB_POSIX_MQ) -luuid \
	$(top_builddir)/lib/libnilfsgc.la

nilfs_clean_SOURCES = nilfs-clean.c
nilfs_clean_LDADD =  $(LDADD) $(top_builddir)/lib/libcleaner.la \
	$(top_builddir)/lib/libparser.la

nilfs_resize_SOURCES = nilfs-resize.c
nilfs_resize_LDADD = $(LDADD) $(top_builddir)/lib/libmountchk.la \
	$(top_builddir)/lib/libnilfsgc.la

nilfs_tune_SOURCES = nilfs-tune.c
nilfs_tune_LDADD = $(LDADD) $(top_builddir)/lib/libmountchk.la \
	$(top_builddir)/lib/libnilfsfeature.la

EXTRA_DIST = .gitignore
