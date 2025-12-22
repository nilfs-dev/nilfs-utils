## Makefile.am

AM_CFLAGS = -Wall
AM_CPPFLAGS = -I$(top_srcdir)/include -DCORE_SBINDIR=\"$(core_sbindir)\"

lib_LTLIBRARIES = libnilfs.la libnilfsgc.la
noinst_LTLIBRARIES = librealpath.la libnilfsfeature.la libparser.la \
	libmountchk.la libcrc32.la libcleanerexec.la libsegment.la \
	libcleaner.la

librealpath_la_SOURCES = realpath.c

libnilfsfeature_la_SOURCES = feature.c

libparser_la_SOURCES = parser.c

libmountchk_la_SOURCES = ismounted.c

libcrc32_la_SOURCES = crc32.c

libcleanerexec_la_SOURCES = cleaner_exec.c

libsegment_la_SOURCES = segment.c

libnilfs_CURRENT = 3
libnilfs_REVISION = 0
libnilfs_AGE = 0
libnilfs_VERSIONINFO = $(libnilfs_CURRENT):$(libnilfs_REVISION):$(libnilfs_AGE)

libnilfs_la_SOURCES = nilfs.c sb.c
libnilfs_la_LDFLAGS = -version-info $(libnilfs_VERSIONINFO)
libnilfs_la_LIBADD = librealpath.la libcrc32.la $(LIB_POSIX_SEM)

nilfsgc_CURRENT = 3
nilfsgc_REVISION = 0
nilfsgc_AGE = 0
nilfsgc_VERSIONINFO = $(nilfsgc_CURRENT):$(nilfsgc_REVISION):$(nilfsgc_AGE)

libnilfsgc_la_SOURCES = gc.c vector.c cnormap.c
libnilfsgc_la_LDFLAGS = -version-info $(nilfsgc_VERSIONINFO)
libnilfsgc_la_LIBADD = libnilfs.la libsegment.la $(LIB_POSIX_TIMER)

libcleaner_la_SOURCES = cleaner_ctl.c
libcleaner_la_LIBADD = librealpath.la libcleanerexec.la $(LIB_POSIX_MQ) \
	-luuid $(LIB_POSIX_TIMER)
