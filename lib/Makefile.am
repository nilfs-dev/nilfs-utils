## Makefile.am

lib_LTLIBRARIES = libnilfs.la libnilfsgc.la
noinst_LTLIBRARIES = libnilfsmisc.la

libnilfsmisc_la_SOURCES = realpath.c cno.c feature.c ismounted.c
libnilfsmisc_la_CFLAGS = -Wall -fPIC -pthread
libnilfsmisc_la_CPPFLAGS = -I$(top_srcdir)/include
libnilfsmisc_la_LDFLAGS = -static -lpthread

libnilfs_CURRENT = 1
libnilfs_REVISION = 0
libnilfs_AGE = 1
libnilfs_VERSIONINFO = $(libnilfs_CURRENT):$(libnilfs_REVISION):$(libnilfs_AGE)

libnilfs_la_SOURCES = nilfs.c sb.c crc32.c
libnilfs_la_CFLAGS = -Wall
libnilfs_la_CPPFLAGS = -I$(top_srcdir)/include
libnilfs_la_LDFLAGS = -version-info $(libnilfs_VERSIONINFO)
libnilfs_la_LIBADD = libnilfsmisc.la -lpthread

nilfsgc_CURRENT = 0
nilfsgc_REVISION = 0
nilfsgc_AGE = 0
nilfsgc_VERSIONINFO = $(nilfsgc_CURRENT):$(nilfsgc_REVISION):$(nilfsgc_AGE)

libnilfsgc_la_SOURCES = gc.c vector.c cnoconv.c
libnilfsgc_la_CFLAGS = -Wall -fPIC -pthread
libnilfsgc_la_CPPFLAGS = -I$(top_srcdir)/include
libnilfsgc_la_LDFLAGS = -version-info $(nilfsgc_VERSIONINFO)
libnilfsgc_la_LIBADD = libnilfs.la -lpthread
