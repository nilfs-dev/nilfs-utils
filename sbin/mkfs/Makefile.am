## Makefile.am
##
## Copyright (C) 2007-2008 Nippon Telegraph and Telephone Corporation.
##
## Written by Koji Sato <koji@osrg.net>.

sbin_PROGRAMS = mkfs.nilfs2

mkfs_nilfs2_SOURCES = mkfs.c bitops.c ../../lib/crc32.c mkfs.h
mkfs_nilfs2_CFLAGS = -Wall
mkfs_nilfs2_CPPFLAGS = -I$(top_srcdir)/include
mkfs_nilfs2_LDADD = -luuid
