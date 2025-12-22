## Makefile.am

AM_CFLAGS = -Wall
AM_CPPFLAGS = -I$(top_srcdir)/include
LDADD = $(top_builddir)/lib/libnilfs.la \
	$(top_builddir)/lib/libmountchk.la \
	$(top_builddir)/lib/libnilfsgc.la $(LIB_POSIX_SEM)

sbin_PROGRAMS = nilfs-resize

nilfs_resize_SOURCES = nilfs-resize.c

if CREATE_COMPAT_SBIN_LINK
install-exec-hook:
	@if [ -d "$(DESTDIR)$(exec_prefix)/sbin" ]; then \
	    for p in $(sbin_PROGRAMS); do \
	        echo "  LN      $(DESTDIR)$(exec_prefix)/sbin/$$p"; \
	        ln -sf ../bin/$$p $(DESTDIR)$(exec_prefix)/sbin/$$p; \
	    done; \
	fi

uninstall-hook:
	@if [ -d "$(DESTDIR)$(exec_prefix)/sbin" ]; then \
	    for p in $(sbin_PROGRAMS); do \
	        if [ -L "$(DESTDIR)$(exec_prefix)/sbin/$$p" ]; then \
	            echo "  RM      $(DESTDIR)$(exec_prefix)/sbin/$$p"; \
	            rm -f "$(DESTDIR)$(exec_prefix)/sbin/$$p"; \
	        fi; \
	    done; \
	fi
endif  # CREATE_COMPAT_SBIN_LINK

EXTRA_DIST = .gitignore
