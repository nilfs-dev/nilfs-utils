## Makefile.am

sbin_PROGRAMS = nilfs_cleanerd
LDADD = $(top_builddir)/lib/libnilfs.la $(top_builddir)/lib/libnilfsgc.la \
	-lpthread

nilfs_cleanerd_SOURCES = cleanerd.c cldconfig.c \
	cleanerd.h cldconfig.h
nilfs_cleanerd_CFLAGS = -Wall -pthread
nilfs_cleanerd_CPPFLAGS = -I$(top_srcdir)/include \
	-DSYSCONFDIR=\"$(sysconfdir)\"

dist_sysconf_DATA = nilfs_cleanerd.conf
