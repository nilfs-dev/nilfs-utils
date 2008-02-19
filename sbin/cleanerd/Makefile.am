## Makefile.am
##
## Copyright (C) 2007-2008 Nippon Telegraph and Telephone Corporation.
##
## Makefile.am,v 1.6 2008-02-04 08:13:55 koji Exp
##
## Written by Koji Sato <koji@osrg.net>.

sbin_PROGRAMS = nilfs_cleanerd

nilfs_cleanerd_SOURCES = cleanerd.c cldconfig.c vector.c \
	cleanerd.h cldconfig.h vector.h
nilfs_cleanerd_CFLAGS = -Wall
nilfs_cleanerd_CPPFLAGS = -I$(top_srcdir)/include \
	-DSYSCONFDIR=\"$(sysconfdir)\" -D_GNU_SOURCE
nilfs_cleanerd_LDADD = $(top_builddir)/lib/libnilfs.la

dist_sysconf_DATA = nilfs_cleanerd.conf

install-exec-hook:
	list='$(sbin_PROGRAMS)'; \
	for p in $$list; do \
		chown root $(DESTDIR)$(sbindir)/$$p$(EXEEXT); \
		chmod u+s $(DESTDIR)$(sbindir)/$$p$(EXEEXT); \
	done
