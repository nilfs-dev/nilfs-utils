## Makefile.am

include_HEADERS = nilfs.h nilfs_cleaner.h
noinst_HEADERS = realpath.h nls.h parser.h nilfs_feature.h \
	vector.h nilfs_gc.h cnormap.h cleaner_msg.h cleaner_exec.h \
	compat.h crc32.h pathnames.h segment.h util.h

if CONFIG_UAPI_HEADER_INSTALL
nobase_include_HEADERS = linux/nilfs2_api.h linux/nilfs2_ondisk.h
else
noinst_HEADERS += linux/nilfs2_api.h linux/nilfs2_ondisk.h
endif
