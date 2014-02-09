## Makefile.am

lib_LTLIBRARIES = libnilfs.la libnilfsgc.la libnilfscleaner.la
noinst_LTLIBRARIES = librealpath.la libnilfsfeature.la libparser.la \
	libmountchk.la libcrc32.la libcleanerexec.la

librealpath_la_SOURCES = realpath.c
librealpath_la_CFLAGS = -Wall -fPIC
librealpath_la_CPPFLAGS = -I$(top_srcdir)/include
librealpath_la_LDFLAGS = -static

libnilfsfeature_la_SOURCES = feature.c
libnilfsfeature_la_CFLAGS = -Wall -fPIC
libnilfsfeature_la_CPPFLAGS = -I$(top_srcdir)/include
libnilfsfeature_la_LDFLAGS = -static

libparser_la_SOURCES = parser.c
libparser_la_CFLAGS = -Wall -fPIC
libparser_la_CPPFLAGS = -I$(top_srcdir)/include
libparser_la_LDFLAGS = -static

libmountchk_la_SOURCES = ismounted.c
libmountchk_la_CFLAGS = -Wall -fPIC
libmountchk_la_CPPFLAGS = -I$(top_srcdir)/include
libmountchk_la_LDFLAGS = -static

libcrc32_la_SOURCES = crc32.c
libcrc32_la_CFLAGS = -Wall -fPIC
libcrc32_la_CPPFLAGS = -I$(top_srcdir)/include
libcrc32_la_LDFLAGS = -static

libcleanerexec_la_SOURCES = cleaner_exec.c
libcleanerexec_la_CFLAGS = -Wall -fPIC
libcleanerexec_la_CPPFLAGS = -I$(top_srcdir)/include
libcleanerexec_la_LDFLAGS = -static

libnilfs_CURRENT = 2
libnilfs_REVISION = 0
libnilfs_AGE = 2
libnilfs_VERSIONINFO = $(libnilfs_CURRENT):$(libnilfs_REVISION):$(libnilfs_AGE)

libnilfs_la_SOURCES = nilfs.c sb.c
libnilfs_la_CFLAGS = -Wall
libnilfs_la_CPPFLAGS = -I$(top_srcdir)/include
libnilfs_la_LDFLAGS = -version-info $(libnilfs_VERSIONINFO)
libnilfs_la_LIBADD = librealpath.la libcrc32.la $(LIB_POSIX_SEM)

nilfsgc_CURRENT = 1
nilfsgc_REVISION = 0
nilfsgc_AGE = 1
nilfsgc_VERSIONINFO = $(nilfsgc_CURRENT):$(nilfsgc_REVISION):$(nilfsgc_AGE)

libnilfsgc_la_SOURCES = gc.c vector.c cnoconv.c
libnilfsgc_la_CFLAGS = -Wall -fPIC
libnilfsgc_la_CPPFLAGS = -I$(top_srcdir)/include
libnilfsgc_la_LDFLAGS = -version-info $(nilfsgc_VERSIONINFO)
libnilfsgc_la_LIBADD = libnilfs.la $(LIB_POSIX_SEM)

cleanerctl_CURRENT = 0
cleanerctl_REVISION = 1
cleanerctl_AGE = 0
cleanerctl_VERSIONINFO = $(cleanerctl_CURRENT):$(cleanerctl_REVISION):$(cleanerctl_AGE)

libnilfscleaner_la_SOURCES = cleaner_ctl.c
libnilfscleaner_la_CFLAGS = -Wall -fPIC
libnilfscleaner_la_CPPFLAGS = -I$(top_srcdir)/include
libnilfscleaner_la_LDFLAGS = -version-info $(cleanerctl_VERSIONINFO)
libnilfscleaner_la_LIBADD = librealpath.la libcleanerexec.la $(LIB_POSIX_MQ) -luuid
