/*
 * mount_opts.h - Parse options for mount program, declarations
 *
 * Code extracted from util-linux-2.12r/mount/mount.c
 *
 * modified by Ryusuke Konishi <ryusuke@osrg.net>
 */
#ifndef _MOUNT_OPTS_H
#define _MOUNT_OPTS_H


/* Custom mount options for our own purposes.  */
/* Maybe these should now be freed for kernel use again */
#define MS_NOAUTO	0x80000000
#define MS_USERS	0x40000000
#define MS_USER		0x20000000
#define MS_OWNER	0x10000000
#define MS_GROUP	0x08000000
#define MS_PAMCONSOLE	0x04000000
#define MS_NETDEV	0x00040000
#define MS_COMMENT	0x00020000
#define MS_LOOP		0x00010000

/* Options that we keep the mount system call from seeing.  */
#define MS_NOSYS	(MS_NOAUTO|MS_USERS|MS_USER|MS_COMMENT|MS_LOOP|MS_PAMCONSOLE|MS_NETDEV)

/* Options that we keep from appearing in the options field in the mtab.  */
#define MS_NOMTAB	(MS_REMOUNT|MS_NOAUTO|MS_USERS|MS_USER|MS_PAMCONSOLE)

/* Options that we make ordinary users have by default.  */
#define MS_SECURE	(MS_NOEXEC|MS_NOSUID|MS_NODEV)

/* Options that we make owner-mounted devices have by default */
#define MS_OWNERSECURE	(MS_NOSUID|MS_NODEV)


extern void parse_opts(const char *options, int *flags, char **extra_opts);
extern char *fix_opts_string(int flags, const char *extra_opts, const char *user);


#endif /* _MOUNT_OPTS_H */
