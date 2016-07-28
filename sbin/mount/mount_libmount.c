/*
 * mount_libmount.c - NILFS mount helper program (libmount version)
 *
 * Copyright (C) 2007-2012 Nippon Telegraph and Telephone Corporation.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 021110-1307, USA.
 *
 * Written by Ryusuke Konishi <konishi.ryusuke@lab.ntt.co.jp>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif	/* HAVE_CONFIG_H */

#if HAVE_SYS_TYPES_H
#include <sys/types.h>
#endif	/* HAVE_SYS_TYPES_H */

#include <stdio.h>

#if HAVE_STDLIB_H
#include <stdlib.h>
#endif	/* HAVE_STDLIB_H */

#if HAVE_UNISTD_H
#include <unistd.h>
#endif	/* HAVE_UNISTD_H */

#if HAVE_FCNTL_H
#include <fcntl.h>
#endif	/* HAVE_FCNTL_H */

#if HAVE_STRINGS_H
#include <strings.h>
#endif	/* HAVE_STRINGS_H */

#if HAVE_STRING_H
#include <string.h>
#endif	/* HAVE_STRING_H */

#if HAVE_SYS_IOCTL_H
#include <sys/ioctl.h>		/* ioctl() */
#endif	/* HAVE_SYS_IOCTL_H */

#if HAVE_SYS_MOUNT_H
#include <sys/mount.h>		/* BLKROGET */
#endif	/* HAVE_SYS_MOUNT_H */

#if HAVE_SYS_STAT_H
#include <sys/stat.h>
#endif	/* HAVE_SYS_STAT_H */

#if HAVE_SYSLOG_H
#include <syslog.h>
#endif	/* HAVE_SYSLOG_H */

#if HAVE_LIBMOUNT_LIBMOUNT_H
#include <libmount/libmount.h>
#endif	/* HAVE_LIBMOUNT_H */

#include <stdarg.h>
#include <errno.h>
#include <assert.h>

#include "sundries.h"
#include "xmalloc.h"
#include "mount.nilfs2.h"
#include "mount_attrs.h"
#include "cleaner_exec.h"
#include "nls.h"

#ifdef _GNU_SOURCE
#include <getopt.h>
#endif	/* _GNU_SOURCE */

/* mount options */
int mount_quiet;	/* for sundries.c */
static int verbose;
static int devro;

static char *mount_fstype;

/* global variables */
const char fstype[] = NILFS2_FS_NAME;
char *progname = "mount." NILFS2_FS_NAME;

/* mount info */
struct nilfs_mount_info {
	struct libmnt_context *cxt;
	unsigned long mflags;
	int type;
	int mounted;
	struct nilfs_mount_attrs old_attrs;
	struct nilfs_mount_attrs new_attrs;
};

enum {
	NORMAL_MOUNT,
	RW2RO_REMOUNT,
	RW2RW_REMOUNT,
};


static void nilfs_mount_logger(int priority, const char *fmt, ...)
{
	va_list args;

	if ((verbose && priority > LOG_INFO) || priority >= LOG_INFO)
		return;
	va_start(args, fmt);
	fprintf(stderr, "%s: ", progname);
	vfprintf(stderr, fmt, args);
	fputs(_("\n"), stderr);
	va_end(args);
}

#if 0
static int nilfs_libmount_table_errcb(struct libmnt_table *tb,
				      const char *filename, int line)
{
	if (filename)
		error(_("%s: parse error: ignore entry at line %d."),
		      filename, line);
	return 0;
}
#endif

/*
 * Other routines
 */
static int device_is_readonly(const char *device, int *ro)
{
	int fd, res = 0;

	fd = open(device, O_RDONLY);
	if (fd < 0)
		return -errno;

	if (ioctl(fd, BLKROGET, ro) < 0)
		res = -errno;
	close(fd);
	return res;
}

static void show_version(void)
{
	printf("%s (%s %s)\n", progname, PACKAGE, PACKAGE_VERSION);
}

static void nilfs_mount_parse_options(int argc, char *argv[],
				      struct nilfs_mount_info *mi)
{
	struct libmnt_context *cxt = mi->cxt;
	struct libmnt_fs *fs;
	int c, show_version_only = 0;

	fs = mnt_context_get_fs(cxt);
	if (!fs)
		die(EX_SYSERR, _("failed to get fs"));

	while ((c = getopt(argc, argv, "fvnt:o:rwV")) != EOF) {
		switch (c) {
		case 'f':
			mnt_context_enable_fake(cxt, 1);
			break;
		case 'v':
			mnt_context_enable_verbose(cxt, 1);
			verbose = 1;
			break;
		case 'n':
			mnt_context_disable_mtab(cxt, 1);
			break;
		case 't':
			mount_fstype = optarg;
			break;
		case 'o':
		{
			char *rest;

			if (nilfs_mount_attrs_parse(&mi->new_attrs, optarg,
						    NULL, &rest, 0))
				die(EX_SYSERR, _("failed to parse options"));
			if (rest && mnt_context_append_options(cxt, rest))
				die(EX_SYSERR, _("failed to append options"));
			free(rest);
			break;
		}
		case 'r':
			if (mnt_context_append_options(cxt, "ro"))
				die(EX_SYSERR, _("failed to append options"));
			break;
		case 'w':
			if (mnt_context_append_options(cxt, "rw"))
				die(EX_SYSERR, _("failed to append options"));
			break;
		case 'V':
			show_version_only = 1;
			break;
		default:
			break;
		}
	}

	if (show_version_only) {
		show_version();
		exit(EXIT_SUCCESS);
	}
}

static struct libmnt_fs *nilfs_find_rw_mount(struct libmnt_context *cxt,
					     struct libmnt_table *mtab)
{
	struct libmnt_iter *iter = mnt_new_iter(MNT_ITER_BACKWARD);
	const char *src = mnt_context_get_source(cxt);
	const char *type = mnt_context_get_fstype(cxt);
	struct libmnt_fs *fs = NULL;

	if (!iter)
		die(EX_SYSERR, _("libmount iterator allocation failed"));

	while (mnt_table_next_fs(mtab, iter, &fs) == 0) {
		if (mnt_fs_match_fstype(fs, type) &&
		    mnt_fs_match_source(fs, src, mnt_table_get_cache(mtab)) &&
		    mnt_fs_match_options(fs, "rw"))
			break;
	}

	mnt_free_iter(iter);
	return fs;
}

static int nilfs_prepare_mount(struct nilfs_mount_info *mi)
{
	struct libmnt_context *cxt = mi->cxt;
	struct libmnt_fs *fs;
	struct libmnt_table *mtab;
	const char *attrs;
	int res;

	res = mnt_context_prepare_mount(cxt);
	if (res < 0) {
		error(_("%s: preparing failed: %s"), progname,
		      strerror(-res));
		goto failed;
	}
	/*
	 * mnt_context_prepare_mount() parses mtab (/etc/mtab or
	 * /proc/self/mountinfo + /run/mount/utabs or /proc/mounts)
	 */

	res = mnt_context_get_mflags(cxt, &mi->mflags);
	if (res < 0) {
		error(_("%s: get mount flags failed: %s"), progname,
		      strerror(-res));
		goto failed;
	}

	if (!(mi->mflags & MS_RDONLY) && !(mi->mflags & MS_BIND)) {
		res = device_is_readonly(mnt_context_get_source(cxt),
					 &devro);
		if (res < 0) {
			error(_("%s: device %s not accessible: %s"),
			      progname, mnt_context_get_source(cxt),
			      strerror(-res));
			goto failed;
		}
	}

	res = mnt_context_get_mtab(cxt, &mtab);
	if (res < 0) {
		error(_("%s: libmount mount check failed: %s"),
		      progname, strerror(-res));
		goto failed;
	}

	mi->mounted = mnt_table_is_fs_mounted(mtab, mnt_context_get_fs(cxt));

	if (mi->mflags & MS_BIND)
		return 0;

	fs = nilfs_find_rw_mount(cxt, mtab);
	if (fs == NULL)
		return 0; /* no previous rw-mount */

	switch (mi->mflags & (MS_RDONLY | MS_REMOUNT)) {
	case 0: /* overlapping rw-mount */
		error(_("%s: the device already has a rw-mount on %s.\n"
			"\t\tmultiple rw-mount is not allowed."),
		      progname, mnt_fs_get_target(fs));
		goto failed;
	case MS_RDONLY: /* ro-mount (a rw-mount exists) */
		break;
	case MS_REMOUNT | MS_RDONLY: /* rw->ro remount */
	case MS_REMOUNT: /* rw->rw remount */
		mi->type = (mi->mflags & MS_RDONLY) ?
			RW2RO_REMOUNT : RW2RW_REMOUNT;

		attrs = mnt_fs_get_attributes(fs);
		if (attrs) {
			if (nilfs_mount_attrs_parse(&mi->old_attrs, attrs,
						    NULL, NULL, 1)) {
				error(_("%s: libmount mount check failed: %s"),
				      progname, strerror(-res));
				goto failed;
			}
		}

		if (!mnt_fs_match_target(fs, mnt_context_get_target(cxt),
					 mnt_table_get_cache(mtab))) {
			error(_("%s: different mount point (%s). remount failed."),
			      progname, mnt_context_get_target(cxt));
			goto failed;
		}

		if (mi->old_attrs.gcpid) {
			res = nilfs_shutdown_cleanerd(
				mnt_fs_get_source(fs), mi->old_attrs.gcpid);
			if (res < 0) {
				error(_("%s: remount failed due to %s shutdown failure"),
				      progname, NILFS_CLEANERD_NAME);
				goto failed;
			}
		}
		break;
	}

	res = 0;
 failed:
	return res;
}

static int nilfs_do_mount_one(struct nilfs_mount_info *mi)
{
	struct libmnt_context *cxt = mi->cxt;
	int res, errsv;

	res = mnt_context_do_mount(cxt);
	if (!res)
		goto out;

	errsv = errno;
	switch (errsv) {
	case ENODEV:
		error(_("%s: cannot find or load %s filesystem"), progname,
		      fstype);
		break;
	default:
		error(_("%s: Error while mounting %s on %s: %s"), progname,
		      mnt_context_get_source(cxt),
		      mnt_context_get_target(cxt), strerror(errsv));
		break;
	}
	if (mi->type != RW2RO_REMOUNT && mi->type != RW2RW_REMOUNT)
		goto out;

	/* Cleaner daemon was stopped and it needs to run */
	/* because filesystem is still mounted */
	if (!mi->old_attrs.nogc) {
		struct nilfs_mount_attrs mattrs = { .pp = mi->old_attrs.pp };

		/* Restarting cleaner daemon */
		if (nilfs_launch_cleanerd(mnt_context_get_source(cxt),
					  mnt_context_get_target(cxt),
					  mattrs.pp, &mattrs.gcpid) == 0) {
			if (mnt_context_is_verbose(cxt))
				printf(_("%s: restarted %s\n"),
				       progname, NILFS_CLEANERD_NAME);

			nilfs_mount_attrs_update(&mi->old_attrs, &mattrs, cxt);
			mnt_context_finalize_mount(cxt);
		} else {
			error(_("%s: failed to restart %s"),
			      progname, NILFS_CLEANERD_NAME);
		}
	} else {
		printf(_("%s not restarted\n"), NILFS_CLEANERD_NAME);
	}
out:
	return res;
}

static int nilfs_update_mount_state(struct nilfs_mount_info *mi)
{
	struct libmnt_context *cxt = mi->cxt;
	struct nilfs_mount_attrs *old_attrs;
	int rungc, gc_ok;

	gc_ok = !(mi->mflags & MS_RDONLY) && !(mi->mflags & MS_BIND);
	rungc = gc_ok && !mi->new_attrs.nogc;
	old_attrs = (mi->mflags & MS_REMOUNT) ? &mi->old_attrs : NULL;

	if (rungc) {
		if (mi->new_attrs.pp == ULONG_MAX)
			mi->new_attrs.pp = mi->old_attrs.pp;

		if (nilfs_launch_cleanerd(mnt_context_get_source(cxt),
					  mnt_context_get_target(cxt),
					  mi->new_attrs.pp,
					  &mi->new_attrs.gcpid) < 0)
			error(_("%s aborted"), NILFS_CLEANERD_NAME);
		else if (mnt_context_is_verbose(cxt))
			printf(_("%s: started %s\n"), progname,
			       NILFS_CLEANERD_NAME);
	}

	nilfs_mount_attrs_update(old_attrs, &mi->new_attrs, cxt);

	return mnt_context_finalize_mount(cxt);
}

static int nilfs_mount_one(struct nilfs_mount_info *mi)
{
	int res, err = EX_FAIL;

	res = nilfs_prepare_mount(mi);
	if (res)
		goto failed;

	if (!mnt_context_is_fake(mi->cxt)) {
		res = nilfs_do_mount_one(mi);
		if (res)
			goto failed;
	}
	res = nilfs_update_mount_state(mi);
	if (!res)
		err = 0;
failed:
	return err;
}

int main(int argc, char *argv[])
{
	struct nilfs_mount_info mi = {0};
	char *device, *mntdir;
	int res = 0;

	if (argc > 0) {
		char *cp = strrchr(argv[0], '/');

		progname = (cp ? cp + 1 : argv[0]);
	}

	nilfs_cleaner_logger = nilfs_mount_logger;

	mi.type = NORMAL_MOUNT;

	mnt_init_debug(0);
	mi.cxt = mnt_new_context();
	if (!mi.cxt)
		die(EX_SYSERR, _("libmount context allocation failed"));

#if 0
	mnt_context_set_tables_errcb(mi.cxt, nilfs_libmount_table_errcb);
#endif
	mnt_context_set_fstype(mi.cxt, fstype);
	mnt_context_disable_helpers(mi.cxt, 1);

	nilfs_mount_attrs_init(&mi.old_attrs);
	nilfs_mount_attrs_init(&mi.new_attrs);
	nilfs_mount_parse_options(argc, argv, &mi);

	umask(022);

	if (optind >= argc || !argv[optind])
		die(EX_USAGE, _("No device specified"));

	device = argv[optind++];
	mnt_context_set_source(mi.cxt, device);

	if (optind >= argc || !argv[optind])
		die(EX_USAGE, _("No mountpoint specified"));

	mntdir = argv[optind++];
	mnt_context_set_target(mi.cxt, mntdir);

	if (mount_fstype && strncmp(mount_fstype, fstype, strlen(fstype)))
		die(EX_USAGE, _("Unknown filesystem (%s)"), mount_fstype);

	if (getuid() != geteuid())
		die(EX_USAGE,
		    _("%s: mount by non-root user is not supported yet"),
		    progname);

	res = nilfs_mount_one(&mi);

	mnt_free_context(mi.cxt);
	return res;
}
