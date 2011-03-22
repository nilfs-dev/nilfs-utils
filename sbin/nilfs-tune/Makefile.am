## Makefile.am

AM_CFLAGS = -Wall -pthread
AM_CPPFLAGS = -I$(top_srcdir)/include
LDADD = $(top_builddir)/lib/libnilfs.la -lpthread

sbin_PROGRAMS = nilfs-tune

nilfs_tune_SOURCES = nilfs-tune.c
