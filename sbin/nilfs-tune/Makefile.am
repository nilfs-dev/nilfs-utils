## Makefile.am

AM_CFLAGS = -Wall
AM_CPPFLAGS = -I$(top_srcdir)/include

sbin_PROGRAMS = nilfs-tune

nilfs_tune_SOURCES = nilfs-tune.c
nilfs_tune_LDADD = $(top_builddir)/lib/libnilfs.la
