## Makefile.am

root_sbin_PROGRAMS = nilfs_cleanerd
LDADD = $(top_builddir)/lib/libnilfs.la $(top_builddir)/lib/libnilfsgc.la \
	$(LIB_POSIX_MQ) -luuid

nilfs_cleanerd_SOURCES = cleanerd.c cldconfig.c \
	cldconfig.h
nilfs_cleanerd_CFLAGS = -Wall
nilfs_cleanerd_CPPFLAGS = -I$(top_srcdir)/include \
	-DSYSCONFDIR=\"$(sysconfdir)\"
# Use -static option to make nilfs_cleanerd self-contained.
nilfs_cleanerd_LDFLAGS = -static

dist_sysconf_DATA = nilfs_cleanerd.conf

EXTRA_DIST = .gitignore
