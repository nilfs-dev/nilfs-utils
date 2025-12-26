## Makefile.am

SUBDIRS = mount

AM_CFLAGS = -Wall
AM_CPPFLAGS = -I$(top_srcdir)/include
LDADD = $(top_builddir)/lib/libnilfs.la

core_sbin_PROGRAMS = mkfs.nilfs2 nilfs_cleanerd
sbin_PROGRAMS = nilfs-clean nilfs-resize nilfs-tune

mkfs_nilfs2_SOURCES = mkfs.c bitops.c mkfs.h
mkfs_nilfs2_CPPFLAGS = $(AM_CPPFLAGS) -DBADBLOCKSDIR=\"$(badblocksdir)\"
mkfs_nilfs2_LDADD = $(LIB_BLKID) -luuid \
	$(top_builddir)/lib/libcrc32.la \
	$(top_builddir)/lib/libmountchk.la \
	$(top_builddir)/lib/libnilfsfeature.la

nilfs_cleanerd_SOURCES = cleanerd.c cldconfig.c cldconfig.h
nilfs_cleanerd_CPPFLAGS = $(AM_CPPFLAGS) -DSYSCONFDIR=\"$(sysconfdir)\"
# Use internal convenience libraries to make nilfs_cleanerd self-contained.
nilfs_cleanerd_LDADD = $(LIB_POSIX_MQ) -luuid \
	$(top_builddir)/lib/libnilfsgc_static.la

nilfs_clean_SOURCES = nilfs-clean.c
nilfs_clean_LDADD =  $(LDADD) $(top_builddir)/lib/libcleaner.la \
	$(top_builddir)/lib/libparser.la

nilfs_resize_SOURCES = nilfs-resize.c
nilfs_resize_LDADD = $(LDADD) $(top_builddir)/lib/libmountchk.la \
	$(top_builddir)/lib/libnilfsgc.la

nilfs_tune_SOURCES = nilfs-tune.c
nilfs_tune_LDADD = $(LDADD) $(top_builddir)/lib/libmountchk.la \
	$(top_builddir)/lib/libnilfsfeature.la

EXTRA_DIST = .gitignore


# Define dependencies so that the target binary is not blocked by a directory
# with the same name
fix-conflicting-dirs:
	@for p in $(sbin_PROGRAMS); do \
	    if [ -d "$$p" ]; then \
	        echo "Removing directory '$$p' to allow binary creation"; \
	        rm -rf "$$p"; \
	    fi; \
	done

$(sbin_PROGRAMS): fix-conflicting-dirs

if CREATE_COMPAT_SBIN_LINK
install-exec-hook:
	@if [ -d "$(DESTDIR)$(exec_prefix)/sbin" ]; then \
	    for p in $(sbin_PROGRAMS) $(core_sbin_PROGRAMS); do \
	        echo "  LN      $(DESTDIR)$(exec_prefix)/sbin/$$p"; \
	        ln -sf ../bin/$$p $(DESTDIR)$(exec_prefix)/sbin/$$p; \
	    done; \
	fi

uninstall-hook:
	@if [ -d "$(DESTDIR)$(exec_prefix)/sbin" ]; then \
	    for p in $(sbin_PROGRAMS) $(core_sbin_PROGRAMS); do \
	        if [ -L "$(DESTDIR)$(exec_prefix)/sbin/$$p" ]; then \
	            echo "  RM      $(DESTDIR)$(exec_prefix)/sbin/$$p"; \
	            rm -f "$(DESTDIR)$(exec_prefix)/sbin/$$p"; \
	        fi; \
	    done; \
	fi
endif  # CREATE_COMPAT_SBIN_LINK
