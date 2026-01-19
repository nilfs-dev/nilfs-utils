/*
 * umount_libmount.c - NILFS umount helper program (libmount version)
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
 * Written by Ryusuke Konishi <konishi.ryusuke@gmail.com>
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

#if HAVE_ERR_H
#include <err.h>
#endif	/* HAVE_ERR_H */

#if HAVE_STRINGS_H
#include <strings.h>
#endif	/* HAVE_STRINGS_H */

#if HAVE_STRING_H
#include <string.h>
#endif	/* HAVE_STRING_H */

#if HAVE_SYS_STAT_H
#include <sys/stat.h>
#endif	/* HAVE_SYS_STAT_H */

#if HAVE_SYSLOG_H
#include <syslog.h>
#endif	/* HAVE_SYSLOG_H */

#include <libmount.h>

#include <stdarg.h>
#include <errno.h>
#include <assert.h>

#include "compat.h"	/* getprogname() */
#include "libmount_compat.h"
#include "mount.nilfs2.h"
#include "mount_attrs.h"
#include "cleaner_exec.h"
#include "nls.h"

#ifdef _GNU_SOURCE
#include <getopt.h>
#endif	/* _GNU_SOURCE */

/* options */
static int verbose;
static int force;
static int suid;	/* reserved for non-root user mount/umount
			   (not supported yet) */

/* global variables */
static const char fstype[] = NILFS2_FS_NAME;

/* umount info */
struct nilfs_umount_info {
	struct libmnt_context *cxt;
	struct nilfs_mount_attrs old_attrs;
};

/*
 * Other routines
 */
static void nilfs_umount_logger(int priority, const char *fmt, ...)
{
	va_list args;

	if ((verbose && priority > LOG_INFO) || priority >= LOG_INFO)
		return;
	va_start(args, fmt);
	vwarnx(fmt, args);
	va_end(args);
}

#if 0
static int nilfs_libmount_table_errcb(struct libmnt_table *tb,
				      const char *filename, int line)
{
	if (filename)
		warnx(_("%s: parse error: ignore entry at line %d."),
		      filename, line);
	return 0;
}
#endif

static void show_version(void)
{
	printf("%s (%s %s)\n", getprogname(), PACKAGE, PACKAGE_VERSION);
}

static void nilfs_umount_parse_options(int argc, char *argv[],
				       struct nilfs_umount_info *umi)
{
	struct libmnt_context *cxt = umi->cxt;
	struct libmnt_fs *fs;
	int c, show_version_only = 0;

	fs = mnt_context_get_fs(cxt);
	if (!fs)
		errx(MNT_EX_SYSERR, _("failed to get fs"));

	while ((c = getopt(argc, argv, "flnvrV")) != EOF) {
		switch (c) {
		case 'f':
			force = 1;
			break;
		case 'l':
			mnt_context_enable_lazy(cxt, 1);
			break;
		case 'n':
			mnt_context_disable_mtab(cxt, 1);
			break;
		case 'v':
			mnt_context_enable_verbose(cxt, 1);
			verbose = 1;
			break;
		case 'r':
			mnt_context_enable_rdonly_umount(cxt, 1);
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
		exit(MNT_EX_SUCCESS);
	}
}

static void complain(int err, const char *node)
{
	switch (err) {
	case ENXIO:
		warnx(_("%s: invalid block device"), node);
		break;
	case EINVAL:
		warnx(_("%s: not mounted"), node);
		break;
	case EIO:
		warnx(_("%s: I/O error while unmounting"), node);
		break;
	case EBUSY:
		warnx(_("%s: target is busy"), node);
		break;
	case ENOENT:
		warnx(_("%s: not found"), node);
		break;
	case EPERM:
		warnx(_("%s: must be superuser to umount"), node);
		break;
	case EACCES:
		warnx(_("%s: block devices not permitted on fs"), node);
		break;
	default:
		warnx(_("%s: %s"), node, strerror(err));
		break;
	}
}

static int nilfs_prepare_umount(struct nilfs_umount_info *umi)
{
	struct libmnt_context *cxt = umi->cxt;
	const char *attrs;
	int res;

	res = mnt_context_prepare_umount(cxt);
	if (res < 0) {
		warnx(_("preparation failed: %s"), strerror(-res));
		goto failed;
	}
	/*
	 * mnt_context_prepare_umount(cxt) sets up source and target
	 * of the context.
	 */
	nilfs_mount_attrs_init(&umi->old_attrs);

	attrs = mnt_fs_get_attributes(mnt_context_get_fs(cxt));
	if (attrs) {
		if (nilfs_mount_attrs_parse(&umi->old_attrs, attrs,
					    NULL, NULL, 1))
			errx(MNT_EX_SYSERR, _("failed to parse attributes"));
	}
	res = 0;
 failed:
	return res;
}

static int nilfs_do_umount_one(struct nilfs_umount_info *umi)
{
	struct libmnt_context *cxt = umi->cxt;
	unsigned long mflags;
	int alive = 0, res;

	if (umi->old_attrs.gcpid) {
		alive = nilfs_ping_cleanerd(umi->old_attrs.gcpid);
		nilfs_shutdown_cleanerd(mnt_context_get_source(cxt),
					umi->old_attrs.gcpid);
	}

	res = mnt_context_do_umount(cxt);
	if (res == 0 || !mnt_context_is_rdonly_umount(cxt))
		return res;

	if (mnt_context_get_mflags(cxt, &mflags) < 0 ||
	    (mflags & (MS_REMOUNT | MS_RDONLY)) != (MS_REMOUNT | MS_RDONLY))
		return res;

	if (alive && !nilfs_ping_cleanerd(umi->old_attrs.gcpid)) {
		struct nilfs_mount_attrs mattrs = { .pp = umi->old_attrs.pp };

		if (nilfs_launch_cleanerd(mnt_context_get_source(cxt),
					  mnt_context_get_target(cxt),
					  mattrs.pp, &mattrs.gcpid) == 0) {
			if (verbose)
				printf(_("%s: restarted %s(pid=%d)\n"),
				       getprogname(), NILFS_CLEANERD_NAME,
				       (int)mattrs.gcpid);

			nilfs_mount_attrs_update(&umi->old_attrs, &mattrs, cxt);
			mnt_context_finalize_umount(cxt);
		} else {
			warnx(_("failed to restart %s"), NILFS_CLEANERD_NAME);
		}
	}
	return res;
}

static int nilfs_umount_one(struct nilfs_umount_info *umi)
{
	int res;

	res = nilfs_prepare_umount(umi);
	if (res)
		goto failed;

	if (!mnt_context_is_fake(umi->cxt)) {
		res = nilfs_do_umount_one(umi);
		if (res) {
			/*
			 * mnt_context_do_umount() returns:
			 *   0: success
			 * > 0: syscall error (positive errno)
			 * < 0: library error (negative error code)
			 *
			 * complain() expects a positive errno.
			 */
			int ec = res < 0 ? -res : res;

			complain(ec, mnt_context_get_target(umi->cxt));
			goto failed;
		}
	}
	res = mnt_context_finalize_umount(umi->cxt);
failed:
	return res;
}

int main(int argc, char *argv[])
{
	struct nilfs_umount_info umi = {0};
	unsigned int no_succ = 1, no_fail = 1;
	int status;

	nilfs_cleaner_logger = nilfs_umount_logger;

	mnt_init_debug(0);
	umi.cxt = mnt_new_context();
	if (!umi.cxt)
		errx(MNT_EX_SYSERR, _("libmount context allocation failed"));

	nilfs_umount_parse_options(argc, argv, &umi);

#if 0
	mnt_context_set_tables_errcb(umi.cxt, nilfs_libmount_table_errcb);
#endif
	if (mnt_context_set_fstype(umi.cxt, fstype))
		errx(MNT_EX_SYSERR,
		     _("libmount FS description allocation failed"));
	mnt_context_disable_helpers(umi.cxt, 1);

	umask(022);

	if (force)
		warnx(_("Force option is ignored (only supported for NFS)"));

	if (getuid() != geteuid()) {
		suid = 1;
#if 0 /* XXX: normal user mount support */
		if (mnt_context_is_nomtab(mi.cxt) ||
		    mnt_context_is_rdonly_umount(mi.cxt))
			errx(MNT_EX_USAGE, _("only root can do that"));
#else
		errx(MNT_EX_USAGE,
		     _("umount by non-root user is not supported yet"));
#endif
	}

	argc -= optind;
	argv += optind;

	if (argc < 1)
		errx(MNT_EX_USAGE, _("No mountpoint specified"));

	for( ; argc; argc--, argv++) {
		if (**argv == '\0') {
			warnx(_("cannot umount ''"));
			no_fail = 0;
			continue;
		}

		mnt_context_reset_status(umi.cxt);

		if (mnt_context_set_source(umi.cxt, NULL) ||
		    mnt_context_set_target(umi.cxt, *argv))
			errx(MNT_EX_SYSERR,
			     _("Mount entry allocation failed"));

		if (nilfs_umount_one(&umi) == 0)
			no_succ = 0;
		else
			no_fail = 0;
	}

	mnt_free_context(umi.cxt);

	if (no_fail)
		status = MNT_EX_SUCCESS; /* all succeeded */
	else if (no_succ)
		status = MNT_EX_FAIL;	/* all failed */
	else
		status = MNT_EX_SOMEOK;	/* some succeeded, some failed */

	exit(status);
}
