## Makefile.am

lib_LTLIBRARIES = libnilfs.la libnilfsgc.la libnilfscleaner.la
noinst_LTLIBRARIES = librealpath.la libnilfsfeature.la libcno.la \
	libmountchk.la

librealpath_la_SOURCES = realpath.c
librealpath_la_CFLAGS = -Wall -fPIC
librealpath_la_CPPFLAGS = -I$(top_srcdir)/include
librealpath_la_LDFLAGS = -static -lrt

libnilfsfeature_la_SOURCES = feature.c
libnilfsfeature_la_CFLAGS = -Wall -fPIC
libnilfsfeature_la_CPPFLAGS = -I$(top_srcdir)/include
libnilfsfeature_la_LDFLAGS = -static -lrt

libcno_la_SOURCES = cno.c
libcno_la_CFLAGS = -Wall -fPIC
libcno_la_CPPFLAGS = -I$(top_srcdir)/include
libcno_la_LDFLAGS = -static -lrt

libmountchk_la_SOURCES = ismounted.c
libmountchk_la_CFLAGS = -Wall -fPIC
libmountchk_la_CPPFLAGS = -I$(top_srcdir)/include
libmountchk_la_LDFLAGS = -static -lrt

libnilfs_CURRENT = 1
libnilfs_REVISION = 0
libnilfs_AGE = 1
libnilfs_VERSIONINFO = $(libnilfs_CURRENT):$(libnilfs_REVISION):$(libnilfs_AGE)

libnilfs_la_SOURCES = nilfs.c sb.c crc32.c
libnilfs_la_CFLAGS = -Wall
libnilfs_la_CPPFLAGS = -I$(top_srcdir)/include
libnilfs_la_LDFLAGS = -version-info $(libnilfs_VERSIONINFO)
libnilfs_la_LIBADD = librealpath.la -lrt

nilfsgc_CURRENT = 0
nilfsgc_REVISION = 0
nilfsgc_AGE = 0
nilfsgc_VERSIONINFO = $(nilfsgc_CURRENT):$(nilfsgc_REVISION):$(nilfsgc_AGE)

libnilfsgc_la_SOURCES = gc.c vector.c cnoconv.c
libnilfsgc_la_CFLAGS = -Wall -fPIC
libnilfsgc_la_CPPFLAGS = -I$(top_srcdir)/include
libnilfsgc_la_LDFLAGS = -version-info $(nilfsgc_VERSIONINFO)
libnilfsgc_la_LIBADD = libnilfs.la -lrt

cleanerctl_CURRENT = 0
cleanerctl_REVISION = 0
cleanerctl_AGE = 0
cleanerctl_VERSIONINFO = $(cleanerctl_CURRENT):$(cleanerctl_REVISION):$(cleanerctl_AGE)

libnilfscleaner_la_SOURCES = cleaner_ctl.c cleaner_exec.c
libnilfscleaner_la_CFLAGS = -Wall -fPIC
libnilfscleaner_la_CPPFLAGS = -I$(top_srcdir)/include
libnilfscleaner_la_LDFLAGS = -version-info $(cleanerctl_VERSIONINFO)
libnilfscleaner_la_LIBADD = -lrt -luuid
