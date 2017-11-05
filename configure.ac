dnl configure.ac
dnl
dnl Copyright (C) 2007-2012 Nippon Telegraph and Telephone Corporation.

#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ(2.60)
AC_INIT(NILFS utils, 2.2.7, linux-nilfs@vger.kernel.org)
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

dnl UTIL_CHECK_LIB(LIBRARY, FUNCTION, [VARSUFFIX = $1]))
dnl The VARSUFFIX is optional and overrides the default behaviour. For example:
dnl     UTIL_CHECK_LIB(yyy, func, xxx) generates have_xxx and HAVE_LIBXXX
dnl     UTIL_CHECK_LIB(yyy, func)      generates have_yyy and HAVE_LIBYYY
dnl ---------------------------------
dnl borrowed from util-linux 2.17.2
AC_DEFUN([UTIL_CHECK_LIB], [
	m4_define([suffix], m4_default([$3],$1))
	[have_]suffix=yes
	m4_ifdef([$3],
		 [AC_CHECK_LIB([$1], [$2],
		 	       [AC_DEFINE(AS_TR_CPP([HAVE_LIB]suffix), 1)],
			       [[have_]suffix=no])],
		 [AC_CHECK_LIB([$1], [$2], [], [[have_]suffix=no])])
	AM_CONDITIONAL(AS_TR_CPP([HAVE_]suffix), [test [$have_]suffix = yes])
])

# Check for options.
AC_ARG_WITH([libmount],
	AS_HELP_STRING([--with-libmount],
		       [compile mount.nilfs2 with libmount support]),
	[with_libmount=$withval],
	[with_libmount=no; \
	 if test -L /etc/mtab; then with_libmount=yes; fi])

# Leave --enable-libmount[=ARG] and --disable-libmount options for
# compatibility reason.
AC_ARG_ENABLE(libmount,
	AS_HELP_STRING([--enable-libmount (obsolete)],
		       [same as --with-libmount]),
	[with_libmount=$enableval], [])

AC_ARG_WITH([selinux],
	AS_HELP_STRING([--without-selinux], [compile without SELinux support]),
	[], with_selinux=yes)

AC_ARG_WITH([blkid],
	AS_HELP_STRING([--without-blkid], [compile without blkid support]),
	[], with_blkid=yes)

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

LIB_POSIX_TIMER=''
AC_CHECK_FUNC(clock_gettime,,
	[AC_CHECK_LIB(rt, clock_gettime, LIB_POSIX_TIMER=-lrt,
	[AC_MSG_ERROR([clock_gettime not found])])])
AC_SUBST(LIB_POSIX_TIMER)

# Checks for header files.
AC_HEADER_STDC
AC_HEADER_SYS_WAIT
AC_CHECK_HEADERS([ctype.h err.h fcntl.h grp.h libintl.h limits.h \
		  linux/magic.h linux/types.h locale.h mntent.h mqueue.h \
		  paths.h poll.h pwd.h semaphore.h stdint.h stdlib.h string.h \
		  strings.h sys/ioctl.h sys/mount.h sys/time.h syslog.h \
		  time.h unistd.h])

# Check for conditional libraries and headers.
if test "${with_libmount}" = "yes"; then
   AC_CHECK_LIB(mount, mnt_context_do_mount, [LIB_MOUNT="-lmount"],
   	AC_MSG_ERROR([Mount library is enabled but libmount not found]))
   AC_CHECK_HEADERS([libmount/libmount.h])
   with_selinux=no
fi
AM_CONDITIONAL(CONFIG_LIBMOUNT, [test "$with_libmount" = "yes"])
AC_SUBST(LIB_MOUNT)

if test "${with_selinux}" = "yes"; then
   AC_CHECK_LIB(selinux, getprevcon,
	[AC_DEFINE(HAVE_LIBSELINUX, 1,
	    [Define to 1 if you have the 'selinux' library (-lselinux).])
	 LIB_SELINUX="-lselinux"
	],
	AC_MSG_ERROR([SELinux selected but libselinux not found]))
   # This function is missing in old libselinux 1.xx versions
   AC_CHECK_FUNCS([security_get_initial_context])
fi
AC_SUBST([LIB_SELINUX])

if test "${with_blkid}" = "yes"; then
   AC_CHECK_LIB(blkid, blkid_new_probe_from_filename,
   		[AC_DEFINE(HAVE_LIBBLKID, 1,
		    [Define to 1 if you have the 'blkid' library (-lblkid).])
		 LIB_BLKID="-lblkid"
		],
		AC_MSG_ERROR([BLKID library not found]))
   AC_CHECK_HEADERS([blkid/blkid.h])
fi
AC_SUBST(LIB_BLKID)

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
		gettimeofday localtime_r memmove memset strcasecmp strchr \
		strdup strerror strrchr strstr strtok_r strtoul strtoull])

# Checks for system services
AC_SYS_LARGEFILE

# Install directories
AC_PREFIX_DEFAULT([/usr])
test "x$prefix" = "xNONE" && prefix="/usr"
test "x$exec_prefix" = "xNONE" && exec_prefix="${prefix}"
AC_SUBST([root_sbindir], [/sbin])
AC_SUBST([sbindir], [${exec_prefix}/sbin])
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
		 sbin/nilfs-resize/Makefile
		 scripts/Makefile])
AC_OUTPUT
