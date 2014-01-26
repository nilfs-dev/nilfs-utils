## Makefile.am

AM_CFLAGS = -Wall
AM_CPPFLAGS = -I$(top_srcdir)/include
LDADD = $(top_builddir)/lib/libnilfs.la \
	 $(top_builddir)/lib/libnilfsfeature.la \
	 $(top_builddir)/lib/libmountchk.la

sbin_PROGRAMS = nilfs-tune

nilfs_tune_SOURCES = nilfs-tune.c

EXTRA_DIST = .gitignore
