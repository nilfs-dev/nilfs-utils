## Makefile.am

AM_CFLAGS = -Wall
AM_CPPFLAGS = -I$(top_srcdir)/include
LDADD = $(top_builddir)/lib/libnilfs.la \
	$(top_builddir)/lib/libmountchk.la \
	$(top_builddir)/lib/libnilfsgc.la $(LIB_POSIX_SEM)

sbin_PROGRAMS = nilfs-resize

nilfs_resize_SOURCES = nilfs-resize.c

EXTRA_DIST = .gitignore
