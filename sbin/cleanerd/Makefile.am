## Makefile.am

core_sbin_PROGRAMS = nilfs_cleanerd
LDADD = $(top_builddir)/lib/libnilfs.la $(top_builddir)/lib/libnilfsgc.la \
	$(LIB_POSIX_MQ) -luuid

nilfs_cleanerd_SOURCES = cleanerd.c cldconfig.c \
	cldconfig.h
nilfs_cleanerd_CFLAGS = -Wall
nilfs_cleanerd_CPPFLAGS = -I$(top_srcdir)/include \
	-DSYSCONFDIR=\"$(sysconfdir)\"
# Use -static option to make nilfs_cleanerd self-contained.
nilfs_cleanerd_LDFLAGS = -static

dist_sysconf_DATA = nilfs_cleanerd.conf

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
