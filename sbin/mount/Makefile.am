## Makefile.am

COMMONSOURCES = fstab.c sundries.c xmalloc.c mntent.c
COMMONHEADERS = mount.nilfs2.h fstab.h sundries.h xmalloc.h mntent.h \
	mount_constants.h mount_opts.h paths.h

AM_CFLAGS = -Wall
AM_CPPFLAGS = -I$(top_srcdir)/include
LDADD = $(top_builddir)/lib/librealpath.la \
	$(top_builddir)/lib/libcleanerexec.la

sbin_PROGRAMS = mount.nilfs2 umount.nilfs2

mount_nilfs2_SOURCES = mount.nilfs2.c mount_opts.c \
	$(COMMONSOURCES) $(COMMONHEADERS)

umount_nilfs2_SOURCES = umount.nilfs2.c $(COMMONSOURCES) $(COMMONHEADERS)
