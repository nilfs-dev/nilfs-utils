/*
 * mount_attrs.h - NILFS mount attributes for libmount (declarations)
 */
#ifndef _MOUNT_ATTRS_H
#define _MOUNT_ATTRS_H

#include <sys/types.h>

#define NOATTR_NAME		"none"

/* mount options */
struct nilfs_mount_attrs {
	pid_t gcpid;
	int nogc;
	unsigned long pp;
};

struct libmnt_context;

void nilfs_mount_attrs_init(struct nilfs_mount_attrs *mattrs);
int nilfs_mount_attrs_parse(struct nilfs_mount_attrs *mattrs,
			    const char *optstr, char **found, char **rest,
			    int mtab);
void nilfs_mount_attrs_update(struct nilfs_mount_attrs *old_attrs,
			      struct nilfs_mount_attrs *new_attrs,
			      struct libmnt_context *cxt);

#endif /* _MOUNT_ATTRS_H */
