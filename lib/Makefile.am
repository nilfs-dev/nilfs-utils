## Makefile.am

AM_CFLAGS = -Wall
AM_CPPFLAGS = -I$(top_srcdir)/include

lib_LTLIBRARIES = libnilfs.la libnilfsgc.la libnilfscleaner.la
noinst_LTLIBRARIES = librealpath.la libnilfsfeature.la libparser.la \
	libmountchk.la libcrc32.la libcleanerexec.la

librealpath_la_SOURCES = realpath.c

libnilfsfeature_la_SOURCES = feature.c

libparser_la_SOURCES = parser.c

libmountchk_la_SOURCES = ismounted.c

libcrc32_la_SOURCES = crc32.c

libcleanerexec_la_SOURCES = cleaner_exec.c

libnilfs_CURRENT = 2
libnilfs_REVISION = 4
libnilfs_AGE = 2
libnilfs_VERSIONINFO = $(libnilfs_CURRENT):$(libnilfs_REVISION):$(libnilfs_AGE)

libnilfs_la_SOURCES = nilfs.c sb.c
libnilfs_la_LDFLAGS = -version-info $(libnilfs_VERSIONINFO)
libnilfs_la_LIBADD = librealpath.la libcrc32.la $(LIB_POSIX_SEM)

nilfsgc_CURRENT = 2
nilfsgc_REVISION = 1
nilfsgc_AGE = 2
nilfsgc_VERSIONINFO = $(nilfsgc_CURRENT):$(nilfsgc_REVISION):$(nilfsgc_AGE)

libnilfsgc_la_SOURCES = gc.c vector.c cnoconv.c cnormap.c
libnilfsgc_la_LDFLAGS = -version-info $(nilfsgc_VERSIONINFO)
libnilfsgc_la_LIBADD = libnilfs.la $(LIB_POSIX_SEM) $(LIB_POSIX_TIMER)

cleanerctl_CURRENT = 1
cleanerctl_REVISION = 0
cleanerctl_AGE = 1
cleanerctl_VERSIONINFO = $(cleanerctl_CURRENT):$(cleanerctl_REVISION):$(cleanerctl_AGE)

libnilfscleaner_la_SOURCES = cleaner_ctl.c
libnilfscleaner_la_LDFLAGS = -version-info $(cleanerctl_VERSIONINFO)
libnilfscleaner_la_LIBADD = librealpath.la libcleanerexec.la $(LIB_POSIX_MQ) \
	-luuid $(LIB_POSIX_TIMER)
