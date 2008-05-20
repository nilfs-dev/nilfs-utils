dnl configure.ac
dnl
dnl Copyright (C) 2007 Nippon Telegraph and Telephone Corporation.
dnl
dnl Written by Koji Sato <koji@osrg.net>.

#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ(2.60)
AC_INIT(NILFS utils, 2.0.2, nilfs@osrg.net)
AM_INIT_AUTOMAKE
AC_CONFIG_SRCDIR([config.h.in])
AC_CONFIG_HEADER([config.h])

# Checks for programs.
AC_PROG_CC
AM_PROG_CC_C_O
AC_PROG_CPP
AC_PROG_INSTALL
AC_PROG_LN_S
AC_PROG_MAKE_SET
AC_PROG_LIBTOOL
AC_PATH_PROG([LDCONFIG], [ldconfig],
	[AC_MSG_ERROR([ldconfig not found])],
	[$PATH:/sbin])

# Checks for libraries.
AC_CHECK_LIB([uuid], [uuid_generate],
	[AC_DEFINE([HAVE_LIBUUID], 1,
	  [Define to 1 if you have the `uuid' library (-luuid).])],
	[AC_MSG_ERROR([UUID library not found])])

# Checks for header files.
AC_HEADER_STDC
AC_HEADER_SYS_WAIT
AC_CHECK_HEADERS([fcntl.h libintl.h limits.h locale.h mntent.h paths.h \
		  stdlib.h string.h strings.h sys/ioctl.h sys/mount.h \
		  sys/time.h syslog.h unistd.h linux/types.h])

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
AC_CHECK_FUNCS([alarm atexit ftruncate getcwd gettimeofday localtime_r \
		memmove memset munmap strchr strdup strerror strrchr strstr \
		strtoul])

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
		 sbin/mount/Makefile])
AC_OUTPUT
