## Makefile.am
##
## Copyright (C) 2007 Nippon Telegraph and Telephone Corporation.
##
## Written by Koji Sato <koji@osrg.net>.

AM_CFLAGS = -Wall
AM_CPPFLAGS = -I$(top_srcdir)/include

bin_PROGRAMS = chcp dumpseg lscp lssu mkcp rmcp

chcp_SOURCES = chcp.c
chcp_LDADD = $(top_builddir)/lib/libnilfs.la

dumpseg_SOURCES = dumpseg.c
dumpseg_LDADD = $(top_builddir)/lib/libnilfs.la

lscp_SOURCES = lscp.c
lscp_LDADD = $(top_builddir)/lib/libnilfs.la

lssu_SOURCES = lssu.c
lssu_LDADD = $(top_builddir)/lib/libnilfs.la

mkcp_SOURCES = mkcp.c
mkcp_LDADD = $(top_builddir)/lib/libnilfs.la

rmcp_SOURCES = rmcp.c
rmcp_LDADD = $(top_builddir)/lib/libnilfs.la
