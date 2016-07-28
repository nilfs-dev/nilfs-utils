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

/* options */
int mount_quiet;	/* for sundries.c */
static int verbose;
static int force;
static int suid;	/* reserved for non-root user mount/umount
			   (not supported yet) */

/* global variables */
const char fstype[] = NILFS2_FS_NAME;
char *progname = "umount." NILFS2_FS_NAME;

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

static void show_version(void)
{
	printf("%s (%s %s)\n", progname, PACKAGE, PACKAGE_VERSION);
}

static void nilfs_umount_parse_options(int argc, char *argv[],
				       struct nilfs_umount_info *umi)
{
	struct libmnt_context *cxt = umi->cxt;
	struct libmnt_fs *fs;
	int c, show_version_only = 0;

	fs = mnt_context_get_fs(cxt);
	if (!fs)
		die(EX_SYSERR, _("failed to get fs"));

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
		exit(EXIT_SUCCESS);
	}
}

static void complain(int err, const char *dev)
{
	switch (err) {
	case ENXIO:
		error(_("%s: %s: invalid block device"), progname, dev);
		break;
	case EINVAL:
		error(_("%s: %s: not mounted"), progname, dev);
		break;
	case EIO:
		error(_("%s: %s: I/O error while unmounting"), progname, dev);
		break;
	case EBUSY:
		error(_("%s: %s: device is busy"), progname, dev);
		break;
	case ENOENT:
		error(_("%s: %s: not found"), progname, dev);
		break;
	case EPERM:
		error(_("%s: %s: must be superuser to umount"), progname, dev);
		break;
	case EACCES:
		error(_("%s: %s: block devices not permitted on fs"), progname,
		      dev);
		break;
	default:
		error(_("%s: %s: %s"), progname, dev, strerror(err));
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
		error(_("%s: preparing failed: %s"), progname,
		      strerror(-res));
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
			die(EX_SYSERR, _("failed to parse attributes"));
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
				       progname, NILFS_CLEANERD_NAME,
				       (int)mattrs.gcpid);

			nilfs_mount_attrs_update(&umi->old_attrs, &mattrs, cxt);
			mnt_context_finalize_umount(cxt);
		} else {
			error(_("%s: failed to restart %s"),
			      progname, NILFS_CLEANERD_NAME);
		}
	}
	return res;
}

static int nilfs_umount_one(struct nilfs_umount_info *umi)
{
	int res, err = EX_FAIL;

	res = nilfs_prepare_umount(umi);
	if (res)
		goto failed;

	if (!mnt_context_is_fake(umi->cxt)) {
		res = nilfs_do_umount_one(umi);
		if (res) {
			complain(res, mnt_context_get_source(umi->cxt));
			goto failed;
		}
	}
	res = mnt_context_finalize_umount(umi->cxt);
	if (!res)
		err = 0;
failed:
	return err;
}

int main(int argc, char *argv[])
{
	struct nilfs_umount_info umi = {0};
	int ret = 0;

	if (argc > 0) {
		char *cp = strrchr(argv[0], '/');

		progname = (cp ? cp + 1 : argv[0]);
	}

	nilfs_cleaner_logger = nilfs_umount_logger;

	mnt_init_debug(0);
	umi.cxt = mnt_new_context();
	if (!umi.cxt)
		die(EX_SYSERR, _("libmount context allocation failed"));

	nilfs_umount_parse_options(argc, argv, &umi);

#if 0
	mnt_context_set_tables_errcb(umi.cxt, nilfs_libmount_table_errcb);
#endif
	mnt_context_set_fstype(umi.cxt, fstype);
	mnt_context_disable_helpers(umi.cxt, 1);

	umask(022);

	if (force)
		error(_("Force option is ignored (only supported for NFS)"));

	if (getuid() != geteuid()) {
		suid = 1;
#if 0 /* XXX: normal user mount support */
		if (mnt_context_is_nomtab(mi.cxt) ||
		    mnt_context_is_rdonly_umount(mi.cxt))
			die(EX_USAGE, _("only root can do that"));
#else
		die(EX_USAGE,
		    _("%s: umount by non-root user is not supported yet"),
		    progname);
#endif
	}

	argc -= optind;
	argv += optind;

	if (argc < 1)
		die(EX_USAGE, _("No mountpoint specified"));

	while (argc--) {
		if (!*argv)
			die(EX_USAGE, _("Cannot umount \"\"\n"));

		mnt_context_set_source(umi.cxt, NULL);
		mnt_context_set_target(umi.cxt, *argv++);
		ret += nilfs_umount_one(&umi);
	}

	mnt_free_context(umi.cxt);
	exit(ret);
}
