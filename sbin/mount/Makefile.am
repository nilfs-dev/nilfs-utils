## Makefile.am

COMMONSOURCES = cleaner_ctl.c fstab.c sundries.c xmalloc.c mntent.c
COMMONHEADERS = mount.nilfs2.h fstab.h sundries.h xmalloc.h mntent.h \
	mount_constants.h mount_opts.h paths.h

AM_CFLAGS = -Wall
AM_CPPFLAGS = -I$(top_srcdir)/include

sbin_PROGRAMS = mount.nilfs2 umount.nilfs2

mount_nilfs2_SOURCES = mount.nilfs2.c mount_opts.c \
	$(COMMONSOURCES) $(COMMONHEADERS)
mount_nilfs2_LDADD = $(top_builddir)/lib/libnilfsmisc.la

umount_nilfs2_SOURCES = umount.nilfs2.c $(COMMONSOURCES) $(COMMONHEADERS)
umount_nilfs2_LDADD = $(top_builddir)/lib/libnilfsmisc.la
