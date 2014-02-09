## Makefile.am

AM_CFLAGS = -Wall
AM_CPPFLAGS = -I$(top_srcdir)/include
LDADD = $(top_builddir)/lib/libnilfs.la \
	$(top_builddir)/lib/libnilfscleaner.la \
	$(top_builddir)/lib/libparser.la

sbin_PROGRAMS = nilfs-clean

nilfs_clean_SOURCES = nilfs-clean.c

EXTRA_DIST = .gitignore
