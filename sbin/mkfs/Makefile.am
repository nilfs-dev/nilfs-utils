## Makefile.am
##
## Copyright (C) 2007-2008 Nippon Telegraph and Telephone Corporation.
##
## Makefile.am,v 1.4 2008-02-04 08:13:55 koji Exp
##
## Written by Koji Sato <koji@osrg.net>.

sbin_PROGRAMS = mkfs.nilfs2

mkfs_nilfs2_SOURCES = mkfs.c bitops.c ../../lib/crc32.c mkfs.h
mkfs_nilfs2_CFLAGS = -Wall
mkfs_nilfs2_CPPFLAGS = -I$(top_srcdir)/include
mkfs_nilfs2_LDADD = -luuid

install-exec-hook:
	list='$(sbin_PROGRAMS)'; \
	for p in $$list; do \
		chown root $(DESTDIR)$(sbindir)/$$p$(EXEEXT); \
		chmod u+s $(DESTDIR)$(sbindir)/$$p$(EXEEXT); \
	done
