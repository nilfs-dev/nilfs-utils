## Makefile.am

sbin_PROGRAMS = nilfs_cleanerd

nilfs_cleanerd_SOURCES = cleanerd.c cldconfig.c vector.c \
	cleanerd.h cldconfig.h vector.h
nilfs_cleanerd_CFLAGS = -Wall
nilfs_cleanerd_CPPFLAGS = -I$(top_srcdir)/include \
	-DSYSCONFDIR=\"$(sysconfdir)\"
nilfs_cleanerd_LDADD = $(top_builddir)/lib/libnilfs.la

dist_sysconf_DATA = nilfs_cleanerd.conf
