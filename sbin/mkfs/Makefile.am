## Makefile.am

AM_CFLAGS = -Wall
AM_CPPFLAGS = -I$(top_srcdir)/include -DBADBLOCKSDIR=\"$(badblocksdir)\"
LDADD = $(LIB_BLKID) -luuid $(top_builddir)/lib/libnilfsfeature.la \
	$(top_builddir)/lib/libmountchk.la \
	$(top_builddir)/lib/libcrc32.la

core_sbin_PROGRAMS = mkfs.nilfs2

mkfs_nilfs2_SOURCES = mkfs.c bitops.c mkfs.h

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
