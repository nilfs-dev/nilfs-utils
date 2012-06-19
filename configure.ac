dnl configure.ac
dnl
dnl Copyright (C) 2007-2012 Nippon Telegraph and Telephone Corporation.

#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ(2.60)
AC_INIT(NILFS utils, 2.1.3, linux-nilfs@vger.kernel.org)
AM_INIT_AUTOMAKE
AC_CONFIG_SRCDIR([config.h.in])
AC_CONFIG_HEADER([config.h])
AC_CONFIG_MACRO_DIR([m4])

# Checks for programs.
AC_PROG_CC
AC_PROG_CC_C99
AM_PROG_CC_C_O
AC_PROG_CPP
AC_PROG_INSTALL
AC_PROG_LN_S
AC_PROG_MAKE_SET
AC_GNU_SOURCE
AC_PROG_LIBTOOL
AC_PATH_PROG([LDCONFIG], [ldconfig],
	[AC_MSG_ERROR([ldconfig not found])],
	[$PATH:/sbin])

# Check for options.
AC_ARG_ENABLE(libmount,
	[AC_HELP_STRING([--enable-libmount=[ARG]],
			[Link mount.nilfs2 with libmount [ARG=yes] (EXPERIMENTAL)])],
	[enable_libmount=$enableval],
	[enable_libmount=no; \
	 if test -L /etc/mtab; then enable_libmount=yes; fi])

# Checks for libraries.
AC_CHECK_LIB([uuid], [uuid_generate],
	[AC_DEFINE([HAVE_LIBUUID], 1,
	  [Define to 1 if you have the 'uuid' library (-luuid).])],
	[AC_MSG_ERROR([UUID library not found])])

LIB_POSIX_MQ=''
AC_CHECK_FUNC(mq_open,,
	[AC_CHECK_LIB(rt, mq_open, LIB_POSIX_MQ=-lrt,
	[AC_CHECK_LIB(posix4, mq_open, LIB_POSIX_MQ=-lposix4,
	[AC_MSG_ERROR([posix message queue not found])])])])
AC_SUBST(LIB_POSIX_MQ)

LIB_POSIX_SEM=''
AC_CHECK_FUNC(sem_open,,
	[AC_CHECK_LIB(rt, sem_open, LIB_POSIX_SEM=-lrt,
	[AC_CHECK_LIB(pthread, sem_open, LIB_POSIX_SEM=-lpthread,
	[AC_CHECK_LIB(posix4, sem_open, LIB_POSIX_SEM=-lposix4,
	[AC_MSG_ERROR([posix semaphore not found])])])])])
AC_SUBST(LIB_POSIX_SEM)

# Checks for header files.
AC_HEADER_STDC
AC_HEADER_SYS_WAIT
AC_CHECK_HEADERS([fcntl.h libintl.h limits.h locale.h mntent.h paths.h \
		  stdlib.h string.h strings.h sys/ioctl.h sys/mount.h \
		  sys/time.h syslog.h unistd.h linux/types.h grp.h pwd.h \
		  mntent.h semaphore.h ctype.h mqueue.h linux/magic.h])

# Check for conditional libraries and headers.
if test "${enable_libmount}" = "yes"; then
   AC_CHECK_LIB(mount, mnt_context_do_mount, [LIB_MOUNT="-lmount"],
   	AC_MSG_ERROR([libmount needed]))
   AC_CHECK_HEADERS([libmount/libmount.h])
fi
AM_CONDITIONAL(CONFIG_LIBMOUNT, [test "$enable_libmount" = "yes"])
AC_SUBST(LIB_MOUNT)

# Checks for typedefs, structures, and compiler characteristics.
AC_C_CONST
AC_C_INLINE
AC_TYPE_MODE_T
AC_TYPE_OFF_T
AC_TYPE_PID_T
AC_TYPE_SIZE_T
AC_TYPE_SSIZE_T
AC_HEADER_TIME
AC_STRUCT_TM
AC_C_VOLATILE

# Checks for library functions.
AC_FUNC_ALLOCA
AC_FUNC_CHOWN
AC_FUNC_ERROR_AT_LINE
AC_FUNC_FORK
AC_PROG_GCC_TRADITIONAL
AC_FUNC_LSTAT
AC_FUNC_LSTAT_FOLLOWS_SLASHED_SYMLINK
AC_FUNC_MALLOC
AC_FUNC_MMAP
AC_FUNC_REALLOC
AC_TYPE_SIGNAL
AC_FUNC_STAT
AC_FUNC_STRFTIME
AC_FUNC_VPRINTF
AC_CHECK_FUNC(posix_memalign,,
	      [AC_MSG_ERROR([cannot find posix_memalign() function])])
AC_CHECK_FUNCS([alarm atexit ftruncate getcwd getgrgid getmntent_r getpwuid \
		gettimeofday localtime_r memmove memset munmap strcasecmp strchr \
		strdup strerror strrchr strstr strtok_r strtoul strtoull])

# Checks for system services
AC_SYS_LARGEFILE

# Install directories
AC_PREFIX_DEFAULT([/usr])
AC_SUBST([sbindir], [/sbin])
AC_SUBST([sysconfdir], [/etc])
AC_SUBST([localstatedir], [/var])

AC_CONFIG_FILES([Makefile
		 bin/Makefile
		 include/Makefile
		 lib/Makefile
		 man/Makefile
		 sbin/Makefile
		 sbin/cleanerd/Makefile
		 sbin/mkfs/Makefile
		 sbin/mount/Makefile
		 sbin/nilfs-tune/Makefile
		 sbin/nilfs-clean/Makefile
		 sbin/nilfs-resize/Makefile])
AC_OUTPUT
