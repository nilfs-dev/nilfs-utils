## Makefile.am

AM_CFLAGS = -Wall -pthread
AM_CPPFLAGS = -I$(top_srcdir)/include
LDADD = $(top_builddir)/lib/libnilfs.la -lpthread

bin_PROGRAMS = chcp dumpseg lscp lssu mkcp rmcp

chcp_SOURCES = chcp.c

dumpseg_SOURCES = dumpseg.c

lscp_SOURCES = lscp.c

lssu_SOURCES = lssu.c

mkcp_SOURCES = mkcp.c

rmcp_SOURCES = rmcp.c
rmcp_LDADD = $(LDADD) $(top_builddir)/lib/libnilfsmisc.la
