/*
 * umount.nilfs2.c - umount NILFS
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
 *         using examples from util-linux-2.12r/{umount,lomount}.c.
 *
 * The following functions are based on util-linux-2.12r/mount.c
 *  - umount_one()
 *  - complain()
 *
 * The following function is extracted from util-linux-2.12r/lomount.c
 *  - del_loop()
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

#if HAVE_SYS_MOUNT_H
#include <sys/mount.h>
#endif	/* HAVE_SYS_MOUNT_H */

#if HAVE_MNTENT_H
#include <mntent.h>
#endif	/* HAVE_MNTENT_H */

#if HAVE_SYS_STAT_H
#include <sys/stat.h>
#endif	/* HAVE_SYS_STAT_H */

#if HAVE_SYS_WAIT_H
#include <sys/wait.h>
#endif	/* HAVE_SYS_WAIT_H */

#if HAVE_SYSLOG_H
#include <syslog.h>
#endif	/* HAVE_SYSLOG_H */

#include <signal.h>
#include <stdarg.h>
#include <errno.h>

#include "fstab.h"
#include "pathnames.h"
#include "sundries.h"
#include "xmalloc.h"
#include "mount_mntent.h"
#include "mount_constants.h"
#include "mount_opts.h"
#include "mount.nilfs2.h"
#include "cleaner_exec.h"
#include "nls.h"


int verbose;
int mount_quiet;
int readonly;
int readwrite;
static int nomtab;

const char fstype[] = NILFS2_FS_NAME;
char *progname = "umount." NILFS2_FS_NAME;

const char gcpid_opt_fmt[] = PIDOPT_NAME "=%d";
typedef int gcpid_opt_t;

const char pp_opt_fmt[] = PPOPT_NAME "=%lu";
typedef unsigned long pp_opt_t;

struct umount_options {
	int flags;
	int force;
	int lazy;	/* not supported yet */
	int remount;
	int suid;	/* reserved for non-root user mount/umount
			   (not supported yet) */
};

struct umount_options options = {
	.force = 0,
	.lazy = 0,
	.remount = 0,
	.suid = 0,
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

static void show_version(void)
{
	printf("%s (%s %s)\n", progname, PACKAGE, PACKAGE_VERSION);
}

static void parse_options(int argc, char *argv[], struct umount_options *opts)
{
	int c, show_version_only = 0;

	while ((c = getopt(argc, argv, "nlfvrV")) != EOF) {
		switch (c) {
		case 'n':
			nomtab++;
			break;
		case 'l':
			opts->lazy++;
			break;
		case 'f':
			opts->force++;
			break;
		case 'v':
			verbose++;
			break;
		case 'r':
			opts->remount++;
			break;
		case 'V':
			show_version_only++;
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

static int umount_one(const char *, const char *, const char *, const char *,
		      struct mntentchn *);

static int umount_dir(const char *arg)
{
	const char *mntdir;
	struct mntentchn *mc;
	int ret = 0;

	if (!*arg)
		die(EX_USAGE, _("Cannot umount \"\"\n"));

	mntdir = canonicalize(arg);

	mc = getmntdirbackward(mntdir, NULL);
	if (!mc) {
		error(_("Could not find %s in mtab"), mntdir);

		ret = umount_one(arg, mntdir, fstype, arg, NULL);
	} else {
		if (strncmp(mc->m.mnt_type, fstype, strlen(fstype)))
			die(EX_USAGE,
			    _("Different filesystem (%s) mounted on %s"),
			    mc->m.mnt_type, mntdir);

		ret = umount_one(mc->m.mnt_fsname, mc->m.mnt_dir,
				 mc->m.mnt_type, mc->m.mnt_opts, mc);
	}
	return ret;
}

int main(int argc, char *argv[])
{
	struct umount_options *opts = &options;
	int ret = 0;

	if (argc > 0) {
		char *cp = strrchr(argv[0], '/');

		progname = (cp ? cp + 1 : argv[0]);
	}

	nilfs_cleaner_logger = nilfs_umount_logger;

	parse_options(argc, argv, opts);

	umask(022);

	if (opts->force)
		error(_("Force option is ignored (only supported for NFS)"));

	if (opts->lazy)
		error(_("Lazy mount not supported - ignored."));

	if (getuid() != geteuid()) {
		opts->suid = 1;
#if 0 /* XXX: normal user mount support */
		if (opts->nomtab || opts->remount)
			die(EX_USAGE, _("only root can do that"));
#else
		die(EX_USAGE,
		    _("%s: umount by non-root user is not supported yet"),
		    progname);
#endif
	}

	argc -= optind;
	argv += optind;

	atexit(unlock_mtab);

	if (argc < 1)
		die(EX_USAGE, _("No mountpoint specified"));
	else
		while (argc--)
			ret += umount_dir(*argv++);

	exit(ret);
}

/*
 * Code based on util-linux-2.12r/lomount.c
 */
#define LOOP_SET_FD		0x4C00
#define LOOP_CLR_FD		0x4C01

static int del_loop(const char *device)
{
	int fd;

	fd = open(device, O_RDONLY);
	if (fd < 0) {
		int errsv = errno;

		error(_("loop: can't delete device %s: %s\n"),
		      device, strerror(errsv));
		return 1;
	}
	if (ioctl(fd, LOOP_CLR_FD, 0) < 0) {
		perror("ioctl: LOOP_CLR_FD");
		return 1;
	}
	close(fd);
	if (verbose > 1)
		printf(_("del_loop(%s): success\n"), device);
	return 0;
}

/*
 * Code based on util-linux-2.12r/umount.c
 */
/* complain about a failed umount */
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
		/* Let us hope fstab has a line "proc /proc ..."
		   and not "none /proc ..."*/
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

static inline int read_only_mount_point(const struct mntentchn *mc)
{
	return (find_opt(mc->m.mnt_opts, "ro", NULL) >= 0);
}

static inline pid_t get_mtab_gcpid(const struct mntentchn *mc)
{
	pid_t pid = 0;
	gcpid_opt_t id;

	if (find_opt(mc->m.mnt_opts, gcpid_opt_fmt, &id) >= 0)
			pid = id;
	return pid;
}

static void change_mtab_opt(const char *spec, const char *node,
			    const char *type, char *opts)
{
	struct my_mntent mnt;

	mnt.mnt_fsname = canonicalize(spec);
	mnt.mnt_dir = canonicalize(node);
	mnt.mnt_type = xstrdup(type);
	mnt.mnt_freq = 0;
	mnt.mnt_passno = 0;
	/* Above entries are used only when adding new entry */
	mnt.mnt_opts = opts;

	if (!nomtab)
		update_mtab(node, &mnt);

	free(mnt.mnt_fsname);
	free(mnt.mnt_dir);
	free(mnt.mnt_type);
	free(mnt.mnt_opts);
}

/* Umount a single device.  Return a status code, so don't exit
   on a non-fatal error.  We lock/unlock around each umount.  */
static int
umount_one(const char *spec, const char *node, const char *type,
	   const char *opts, struct mntentchn *mc)
{
	int umnt_err = 0;
	int res, alive = 0;
	const char *loopdev;
	pid_t pid;
	pp_opt_t prot_period;

	if (streq(node, "/") || streq(node, "root"))
		nomtab++;

	if (mc) {
		if (!read_only_mount_point(mc)) {
			pid = get_mtab_gcpid(mc);
			if (pid != 0) {
				alive = nilfs_ping_cleanerd(pid);
				nilfs_shutdown_cleanerd(spec, pid);
			}
		}
	}

	res = umount(node);
	if (res < 0)
		umnt_err = errno;

	if (res < 0 && (umnt_err == EBUSY)) {
		if (options.remount) {
			/* Umount failed - let us try a remount */
			res = mount(spec, node, NULL,
				    MS_MGC_VAL | MS_REMOUNT | MS_RDONLY, NULL);
			if (res == 0) {
				error(_("%s: %s busy - remounted read-only"),
				      progname, spec);
				change_mtab_opt(spec, node, type,
						xstrdup("ro"));
				return 0;
			} else if (errno != EBUSY) {	/* hmm ... */
				error(_("%s: could not remount %s read-only"),
				      progname, spec);
			}
		} else if (alive && !nilfs_ping_cleanerd(pid)) {
			if (find_opt(mc->m.mnt_opts, pp_opt_fmt, &prot_period)
			    < 0)
				prot_period = ULONG_MAX;

			if (nilfs_launch_cleanerd(spec, node, prot_period,
						  &pid) == 0) {
				gcpid_opt_t oldpid;
				char *s = xstrdup(opts);

				if (verbose)
					printf(_("%s: restarted %s(pid=%d)\n"),
					       progname, NILFS_CLEANERD_NAME,
					       (int)pid);
				s = replace_drop_opt(s, gcpid_opt_fmt, &oldpid,
						     pid, pid != 0);
				change_mtab_opt(spec, node, type, s);
				goto out;
			} else
				error(_("%s: failed to restart %s"),
				      progname, NILFS_CLEANERD_NAME);
		}
	}

	loopdev = 0;
	if (res >= 0) {
		/* Umount succeeded */
		if (verbose)
			printf(_("%s: %s umounted\n"), progname, spec);

		/* Free any loop devices that we allocated ourselves */
		if (mc) {
			char *optl;

			/* old style mtab line? */
			if (streq(mc->m.mnt_type, "loop")) {
				loopdev = spec;
				goto gotloop;
			}

			/* new style mtab line? */
			optl = mc->m.mnt_opts ? xstrdup(mc->m.mnt_opts) : "";
			for (optl = strtok(optl, ","); optl;
			     optl = strtok(NULL, ",")) {
				if (!strncmp(optl, "loop=", 5)) {
					loopdev = optl+5;
					goto gotloop;
				}
			}
		} else {
			/*
			 * If option "-o loop=spec" occurs in mtab,
			 * note the mount point, and delete mtab line.
			 */
			mc = getmntoptfile(spec);
			if (mc)
				node = mc->m.mnt_dir;
		}

#if 0 /* XXX: -d flag is not delivered by the mount program */
		/* Also free loop devices when -d flag is given */
		if (delloop && is_loop_device(spec))
			loopdev = spec;
#endif
	}
 gotloop:
	if (loopdev)
		del_loop(loopdev);

	if (!nomtab &&
	    (umnt_err == 0 || umnt_err == EINVAL || umnt_err == ENOENT)) {
		update_mtab(node, NULL);
	}
 out:
	if (res >= 0)
		return 0;
	if (umnt_err)
		complain(umnt_err, node);
	return 1;
}
