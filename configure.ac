dnl configure.ac
dnl
dnl Copyright (C) 2007-2012 Nippon Telegraph and Telephone Corporation.

#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ(2.60)
AC_INIT([NILFS utils],[2.3.0-dev],[linux-nilfs@vger.kernel.org])
AM_INIT_AUTOMAKE
AC_CONFIG_SRCDIR([config.h.in])
AC_CONFIG_HEADERS([config.h])
AC_CONFIG_MACRO_DIR([m4])

# Checks for programs.
AC_PROG_CC
AM_PROG_CC_C_O
AC_PROG_CPP
AC_PROG_INSTALL
AC_PROG_LN_S
AC_PROG_MAKE_SET
AC_USE_SYSTEM_EXTENSIONS
LT_PREREQ([2.4])
LT_INIT([disable-static])
AC_PATH_PROG([LDCONFIG], [ldconfig],
	[AC_MSG_ERROR([ldconfig not found])],
	[$PATH:/usr/sbin:/sbin])

# Check for options.
AC_ARG_WITH([libmount],
	AS_HELP_STRING([--without-libmount],
		       [compile mount.nilfs2 without libmount support]),
	[], with_libmount=yes)

AC_ARG_WITH([selinux],
	AS_HELP_STRING([--without-selinux], [compile without SELinux support]),
	[], with_selinux=yes)

AC_ARG_WITH([blkid],
	AS_HELP_STRING([--without-blkid], [compile without blkid support]),
	[], with_blkid=yes)

AC_ARG_WITH([pkgconfigdir],
    [AS_HELP_STRING([--with-pkgconfigdir[[=DIR]]],
        [directory for pkg-config files (no to disable) [default=auto]])],
    [pkgconfigdir=$withval],
    [pkgconfigdir=auto])

AC_ARG_ENABLE([uapi_header_install],
	AS_HELP_STRING([--enable-uapi-header-install],
		       [install kernel uapi header files]),
	[enable_uapi_header_install="${enableval}"],
	[enable_uapi_header_install=no])

AC_ARG_ENABLE([usrmerge],
	[AS_HELP_STRING([--enable-usrmerge[[=TYPE]]],
	  [set UsrMerge hierarchy type (no, sbin, bin, or auto);
	   'sbin' (default TYPE) installs to /usr/sbin, 'bin' installs to
	   /usr/bin [default=auto, which probes the system configuration]])],
	[enable_usrmerge="${enableval}"],
	[enable_usrmerge=auto])

# Checks for the existence of pkg-config itself
m4_ifdef([PKG_PROG_PKG_CONFIG], [PKG_PROG_PKG_CONFIG], [PKG_CONFIG=""])

AS_IF([test "x$pkgconfigdir" = "xno"], [enable_pkgconfig_output=no],
      [test -n "$PKG_CONFIG"], [enable_pkgconfig_output=yes],
      [test "x$pkgconfigdir" = "xyes"], [
         AC_MSG_ERROR([pkg-config requested but not found])
      ], [
         enable_pkgconfig_output=no
         AC_MSG_WARN([pkg-config not found. PC file installation disabled.])
      ])

# Checks for UsrMerge hierarchy
AS_IF([test "x$enable_usrmerge" = "xauto"], [
    AS_IF([test /usr/sbin -ef /usr/bin], [enable_usrmerge=bin],
          [test -x /usr/bin/rpm && \
           test "$(rpm --eval '%{_sbindir}')" = /usr/bin],
              [enable_usrmerge=bin],
          [test /usr/sbin/mkfs -ef /usr/bin/mkfs], [enable_usrmerge=bin],
          [test /sbin -ef /usr/sbin], [enable_usrmerge=sbin],
          [enable_usrmerge=no])
])

# Checks for libraries.
m4_ifdef([PKG_CHECK_MODULES], [
    PKG_CHECK_MODULES([UUID], [uuid], [
        have_uuid_pkg_config=yes
        AC_DEFINE([HAVE_LIBUUID], 1,
                  [Define to 1 if you have the 'uuid' library (-luuid).])
    ], [have_uuid_pkg_config=no])
], [have_uuid_pkg_config=no])

# Fall back
AS_IF([test "x$have_uuid_pkg_config" = "xno"], [
    AC_CHECK_LIB([uuid], [uuid_generate], [
        AC_DEFINE([HAVE_LIBUUID], 1,
                  [Define to 1 if you have the 'uuid' library (-luuid).])
        AC_SUBST(UUID_LIBS, ["-luuid"])
        AC_SUBST(UUID_CFLAGS, ["-I/usr/include/uuid"])
    ], [
        AC_MSG_ERROR([UUID library not found])
    ])
    AC_CHECK_HEADERS([uuid/uuid.h])
])

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
AC_HEADER_SYS_WAIT
AC_CHECK_HEADERS([ctype.h err.h fcntl.h grp.h libintl.h limits.h \
		  linux/magic.h linux/types.h locale.h mntent.h mqueue.h \
		  paths.h poll.h pwd.h semaphore.h stddef.h stdint.h stdlib.h \
		  string.h strings.h sys/ioctl.h sys/mman.h sys/mount.h \
		  sys/sysmacros.h sys/time.h syslog.h time.h unistd.h])

# Check /etc/mtab
mtab_type=''
AC_MSG_CHECKING([for /etc/mtab])
if test -L /etc/mtab; then
   AC_MSG_RESULT([symlink])
   mtab_type=symlink
elif test -f /etc/mtab; then
   AC_MSG_RESULT([regular file])
   mtab_type=file
fi

# Check for conditional libraries and headers.
AS_IF([test "${with_blkid}" = "yes"], [
    # Only try pkg-config if PKG_CHECK_MODULES macro is present
    m4_ifdef([PKG_CHECK_MODULES], [
        PKG_CHECK_MODULES([BLKID], [blkid], [
            have_blkid_pkg_config=yes
            AC_DEFINE([HAVE_LIBBLKID], 1,
                      [Define to 1 if you have the 'blkid' library (-lblkid).])
        ], [have_blkid_pkg_config=no])
    ], [have_blkid_pkg_config=no])

    # Fall back
    AS_IF([test "x$have_blkid_pkg_config" = "xno"], [
        AC_CHECK_LIB(blkid, blkid_new_probe_from_filename, [
            AC_DEFINE(HAVE_LIBBLKID, 1,
                [Define to 1 if you have the 'blkid' library (-lblkid).])
            AC_SUBST(BLKID_LIBS, ["-lblkid"])
            AC_SUBST(BLKID_CFLAGS, ["-I/usr/include/blkid"])
        ], [
            AC_MSG_ERROR([BLKID library not found])
        ], [$UUID_LIBS])  # Other libraries to link when compiling tests

        AC_CHECK_HEADERS([blkid/blkid.h])
    ])
])

AS_IF([test "${with_libmount}" = "yes"], [
    AS_IF([test "$mtab_type" = file], [
        AC_MSG_WARN(m4_normalize([
            Use --without-libmount option to generate mount helpers that work
            with the legacy mtab file.
        ]))
    ])

    # Only try pkg-config if PKG_CHECK_MODULES macro is present
    m4_ifdef([PKG_CHECK_MODULES], [
        PKG_CHECK_MODULES([MOUNT], [mount], [
            have_mount_pkg_config=yes
            AC_DEFINE([HAVE_LIBMOUNT], 1,
                      [Define to 1 if you have the 'mount' library (-lmount).])
        ], [have_mount_pkg_config=no])
    ], [have_mount_pkg_config=no])

    # Fall back
    AS_IF([test "x$have_mount_pkg_config" = "xno"], [
        AC_CHECK_LIB(mount, mnt_context_do_mount, [
            AC_DEFINE(HAVE_LIBMOUNT, 1,
                [Define to 1 if you have the 'mount' library (-lmount).])
            AC_SUBST(MOUNT_LIBS, ["-lmount $BLKID_LIBS"])
            AC_SUBST(MOUNT_CFLAGS, ["-I/usr/include/libmount"])
        ], [
            AC_MSG_ERROR([MOUNT library is enabled but libmount not found])
        ], [$BLKID_LIBS])  # Other libraries to link when compiling tests

        AC_CHECK_HEADERS([libmount/libmount.h])
    ])
    with_selinux=no
])
AM_CONDITIONAL(CONFIG_LIBMOUNT, [test "$with_libmount" = "yes"])

AS_IF([test "${with_selinux}" = "yes"], [
    # Only try pkg-config if PKG_CHECK_MODULES macro is present
    m4_ifdef([PKG_CHECK_MODULES], [
        PKG_CHECK_MODULES([SELINUX], [libselinux], [
            have_selinux_pkg_config=yes
            AC_DEFINE([HAVE_LIBSELINUX], 1,
                [Define to 1 if you have the 'selinux' library (-lselinux).])
        ], [have_selinux_pkg_config=no])
    ], [have_selinux_pkg_config=no])

    # Fall back
    AS_IF([test "x$have_selinux_pkg_config" = "xno"], [
        AC_CHECK_LIB(selinux, getprevcon, [
            AC_DEFINE(HAVE_LIBSELINUX, 1,
                [Define to 1 if you have the 'selinux' library (-lselinux).])
            AC_SUBST(SELINUX_LIBS, ["-lselinux"])
            AC_SUBST(SELINUX_CFLAGS, [""])
        ], [
            AC_MSG_ERROR([SELinux selected but libselinux not found])
        ])
        AC_CHECK_HEADERS([selinux/context.h selinux/selinux.h])
    ])
    # This function is missing in old libselinux 1.xx versions
    AC_CHECK_FUNCS([security_get_initial_context])
])

AM_CONDITIONAL(CONFIG_UAPI_HEADER_INSTALL,
	       [test "$enable_uapi_header_install" = yes])

# Checks for typedefs, structures, and compiler characteristics.
AC_C_CONST
AC_C_INLINE
AC_TYPE_MODE_T
AC_TYPE_OFF_T
AC_TYPE_PID_T
AC_TYPE_SIZE_T
AC_TYPE_SSIZE_T
AC_STRUCT_TM
AC_C_VOLATILE

# Checks for library functions.
AC_FUNC_ALLOCA
AC_FUNC_CHOWN
AC_FUNC_ERROR_AT_LINE
AC_FUNC_FORK
AC_FUNC_LSTAT
AC_FUNC_LSTAT_FOLLOWS_SLASHED_SYMLINK
AC_FUNC_MALLOC
AC_FUNC_MMAP
AC_FUNC_REALLOC
AC_FUNC_STAT
AC_FUNC_STRFTIME
AC_FUNC_VPRINTF
AC_CHECK_FUNC(posix_memalign,,
	      [AC_MSG_ERROR([cannot find posix_memalign() function])])
AC_CHECK_FUNCS([alarm atexit ftruncate getcwd getgrgid getmntent_r getpwuid \
		gettimeofday localtime_r memmove memset pread strcasecmp \
		strchr strdup strerror strrchr strsignal strstr strtok_r \
		strtoul strtoull])

# Checks for system services
AC_SYS_LARGEFILE

# Install directories
AC_PREFIX_DEFAULT([/usr])
test "x$prefix" = "xNONE" && prefix="/usr"
test "x$exec_prefix" = "xNONE" && exec_prefix="${prefix}"

# Determine the location of /sbin binaries based on the --enable-usrmerge
# option
AS_CASE(["x$enable_usrmerge"],
    [xbin], [
        core_sbindir='${exec_prefix}/bin'
        sbindir='${exec_prefix}/bin'
        badblocksdir='/usr/bin'
        create_compat_sbin_link=yes
    ],
    [xsbin|xyes], [
        core_sbindir='${exec_prefix}/sbin'
        sbindir='${exec_prefix}/sbin'
        badblocksdir='/usr/sbin'
        create_compat_sbin_link=no
    ],
    [
        core_sbindir='/sbin'
        sbindir='${exec_prefix}/sbin'
        badblocksdir='/sbin'
        create_compat_sbin_link=no
    ]
)

AC_SUBST([core_sbindir])
AC_SUBST([sbindir])
AC_SUBST([badblocksdir])

AM_CONDITIONAL([CREATE_COMPAT_SBIN_LINK],
	       [test "x$create_compat_sbin_link" = "xyes"])

# Automatic detection of library installation directory

AS_CASE(["$ac_configure_args"], [*--libdir*], [user_specified_libdir=yes],
        [user_specified_libdir=no])

AS_IF([test "x$user_specified_libdir" = "xno" && \
       test "x$libdir" = 'x${exec_prefix}/lib' && \
       test "x$cross_compiling" = "xno"], [
    AC_MSG_CHECKING([for architecture-specific library directory])
    detected_libdir=""

    AS_IF(
        # 1. Primary: systemd-path (available on most modern distros)
        [command -v systemd-path >/dev/null 2>&1],
        [detected_libdir=$(systemd-path system-library-arch 2>/dev/null)],

        # 2. Fallback: use rpm --eval for RPM-based systems
        [command -v rpm >/dev/null 2>&1],
        [detected_libdir=$(rpm --eval '%{_libdir}' 2>/dev/null)]
    )

    AS_IF([test "x$detected_libdir" != "x"], [
        libdir="$detected_libdir"
        AC_MSG_RESULT([$libdir (detected)])
    ], [
        AC_MSG_RESULT([$libdir (default)])
    ])
])

AC_SUBST([libdir])

AC_SUBST([sysconfdir], [/etc])
AC_SUBST([localstatedir], [/var])

AS_IF([test "x$enable_pkgconfig_output" = "xyes"], [
    AS_IF([test "x$pkgconfigdir" = "xyes" || test "x$pkgconfigdir" = "xauto"],
          [pkgconfigdir='${libdir}/pkgconfig'])

    AC_SUBST([pkgconfigdir])
    AC_CONFIG_FILES([lib/nilfs.pc lib/nilfsgc.pc])
])

AM_CONDITIONAL([INSTALL_PKGCONFIG],
               [test "x$enable_pkgconfig_output" = "xyes"])

AC_CONFIG_FILES([Makefile
		 bin/Makefile
		 etc/Makefile
		 include/Makefile
		 lib/Makefile
		 man/Makefile
		 sbin/Makefile
		 sbin/mount/Makefile
		 scripts/Makefile])
AC_OUTPUT
