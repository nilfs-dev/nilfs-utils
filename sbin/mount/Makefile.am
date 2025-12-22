## Makefile.am

COMMONSOURCES = sundries.c xmalloc.c
COMMONHEADERS = mount.nilfs2.h sundries.h xmalloc.h

LEGACYSOURCES = fstab.c mount_mntent.c mount_opts.c
LEGACYHEADERS = fstab.h mount_constants.h mount_mntent.h mount_opts.h

AM_CFLAGS = -Wall
AM_CPPFLAGS = -I$(top_srcdir)/include -DCORE_SBINDIR=\"$(core_sbindir)\"
LDADD = $(top_builddir)/lib/librealpath.la \
	$(top_builddir)/lib/libcleanerexec.la $(LIB_MOUNT) $(LIB_SELINUX) \
	$(LIB_POSIX_TIMER)

core_sbin_PROGRAMS = mount.nilfs2 umount.nilfs2

if CONFIG_LIBMOUNT

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

if CREATE_COMPAT_SBIN_LINK
install-exec-hook:
	@if [ -d "$(DESTDIR)$(exec_prefix)/sbin" ]; then \
	    for p in $(core_sbin_PROGRAMS); do \
	        echo "  LN      $(DESTDIR)$(exec_prefix)/sbin/$$p"; \
	        ln -sf ../bin/$$p $(DESTDIR)$(exec_prefix)/sbin/$$p; \
	    done; \
	fi

uninstall-hook:
	@if [ -d "$(DESTDIR)$(exec_prefix)/sbin" ]; then \
	    for p in $(core_sbin_PROGRAMS); do \
	        if [ -L "$(DESTDIR)$(exec_prefix)/sbin/$$p" ]; then \
	            echo "  RM      $(DESTDIR)$(exec_prefix)/sbin/$$p"; \
	            rm -f "$(DESTDIR)$(exec_prefix)/sbin/$$p"; \
	        fi; \
	    done; \
	fi
endif  # CREATE_COMPAT_SBIN_LINK

EXTRA_DIST = .gitignore
