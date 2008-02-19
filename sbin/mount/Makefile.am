## Makefile.am
##
## Copyright (C) 2007-2008 Nippon Telegraph and Telephone Corporation.
##
## Makefile.am,v 1.4 2008-02-04 08:13:55 koji Exp
##
## Written by Koji Sato <koji@osrg.net>.

COMMONSOURCES = cleaner_ctl.c fstab.c sundries.c xmalloc.c realpath.c mntent.c
COMMONHEADERS = mount.nilfs2.h fstab.h sundries.h xmalloc.h realpath.h \
	mntent.h mount_constants.h mount_opts.h paths.h nls.h

AM_CFLAGS = -Wall
AM_CPPFLAGS = -I$(top_srcdir)/include

sbin_PROGRAMS = mount.nilfs2 umount.nilfs2

mount_nilfs2_SOURCES = mount.nilfs2.c mount_opts.c \
	$(COMMONSOURCES) $(COMMONHEADERS)

umount_nilfs2_SOURCES = umount.nilfs2.c $(COMMONSOURCES) $(COMMONHEADERS)

install-exec-hook:
	list='$(sbin_PROGRAMS)'; \
	for p in $$list; do \
		chown root $(DESTDIR)$(sbindir)/$$p$(EXEEXT); \
		chmod u+s $(DESTDIR)$(sbindir)/$$p$(EXEEXT); \
	done
