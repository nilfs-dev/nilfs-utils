## Makefile.am

COMMONSOURCES = sundries.c xmalloc.c
COMMONHEADERS = mount.nilfs2.h sundries.h xmalloc.h

LEGACYSOURCES = fstab.c mount_mntent.c mount_opts.c
LEGACYHEADERS = fstab.h mount_constants.h mount_mntent.h mount_opts.h \
		mount_paths.h

AM_CFLAGS = -Wall
AM_CPPFLAGS = -I$(top_srcdir)/include
LDADD = $(top_builddir)/lib/librealpath.la \
	$(top_builddir)/lib/libcleanerexec.la

sbin_PROGRAMS = mount.nilfs2 umount.nilfs2

if CONFIG_LIBMOUNT

LDADD += $(LIB_MOUNT)

COMMONSOURCES += mount_attrs.c
COMMONHEADERS += mount_attrs.h

mount_nilfs2_SOURCES = mount_libmount.c $(COMMONSOURCES) $(COMMONHEADERS)
umount_nilfs2_SOURCES = umount_libmount.c $(COMMONSOURCES) $(COMMONHEADERS)

else

COMMONSOURCES += $(LEGACYSOURCES)
COMMONHEADERS += $(LEGACYHEADERS)

mount_nilfs2_SOURCES = mount.nilfs2.c $(COMMONSOURCES) $(COMMONHEADERS)

umount_nilfs2_SOURCES = umount.nilfs2.c $(COMMONSOURCES) $(COMMONHEADERS)

endif  # CONFIG_LIBMOUNT
