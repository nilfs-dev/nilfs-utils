## Makefile.am

AM_CFLAGS = -Wall
AM_CPPFLAGS = -I$(top_srcdir)/include
LDADD = $(top_builddir)/lib/libnilfs.la

bin_PROGRAMS = chcp dumpseg lscp lssu mkcp rmcp

chcp_SOURCES = chcp.c
chcp_LDADD = $(LDADD) $(LIB_POSIX_SEM) $(top_builddir)/lib/libcno.la

dumpseg_SOURCES = dumpseg.c

lscp_SOURCES = lscp.c

lssu_SOURCES = lssu.c

mkcp_SOURCES = mkcp.c
mkcp_LDADD = $(LDADD) $(LIB_POSIX_SEM)

rmcp_SOURCES = rmcp.c
rmcp_LDADD = $(LDADD) $(top_builddir)/lib/libcno.la

EXTRA_DIST = .gitignore
