/*
 * mount.nilfs2.c - NILFS mounter (mount.nilfs2)
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
 *         using examples from util-linux-2.12r/mount.c.
 *
 * The following functions are extracted from util-linux-2.12r/mount.c:
 *  - print_one()
 *  - update_mtab_entry()
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

#if HAVE_SYSLOG_H
#include <syslog.h>
#endif	/* HAVE_SYSLOG_H */

#include <signal.h>
#include <stdarg.h>
#include <errno.h>

#ifdef HAVE_LIBSELINUX
#include <selinux/selinux.h>
#include <selinux/context.h>
#endif /* HAVE_LIBSELINUX */

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

/* mount options */
int verbose;
int mount_quiet;
int readonly;
int readwrite;
static int nomtab;
static int devro;
static int fake;

/* global variables */
const char fstype[] = NILFS2_FS_NAME;
char *progname = "mount." NILFS2_FS_NAME;

const char gcpid_opt_fmt[] = PIDOPT_NAME "=%d";
typedef int gcpid_opt_t;

const char pp_opt_fmt[] = PPOPT_NAME "=%lu";
typedef unsigned long pp_opt_t;

const char nogc_opt_fmt[] = NOGCOPT_NAME;
typedef int nogc_opt_t;

struct mount_options {
	char *fstype;
	char *opts;
	char *extra_opts;
	int flags;
};
struct mount_options options;


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

/* Report on a single mount.  */
static void print_one(const struct my_mntent *me)
{
	if (mount_quiet)
		return;
	printf("%s on %s", me->mnt_fsname, me->mnt_dir);
	if (me->mnt_type != NULL && *(me->mnt_type) != '\0')
		printf(" type %s", me->mnt_type);
	if (me->mnt_opts != NULL)
		printf(" (%s)", me->mnt_opts);
#if 0  /* XXX: volume label */
	if (list_with_volumelabel) {
		const char *label;

		label = mount_get_volume_label_by_spec(me->mnt_fsname);
		if (label) {
			printf(" [%s]", label);
			/* free(label); */
		}
	}
#endif
	printf("\n");
}

/*
 * Other routines
 */
static void handle_signal(int sig)
{
	switch (sig) {
	case SIGTERM:
	case SIGINT:
		die(EX_USER, _("\n%s: interrupted"), progname);
	}
}

static int device_is_readonly(const char *device, int *ro)
{
	int fd, res;

	fd = open(device, O_RDONLY);
	if (fd < 0)
		return errno;

	res = ioctl(fd, BLKROGET, ro);
	if (res < 0)
		return errno;

	close(fd);
	return 0;
}

static void show_version(void)
{
	printf("%s (%s %s)\n", progname, PACKAGE, PACKAGE_VERSION);
}

static void parse_options(int argc, char *argv[], struct mount_options *opts)
{
	int c, show_version_only = 0;

	while ((c = getopt(argc, argv, "fvnt:o:rwV")) != EOF) {
		switch (c) {
		case 'f':
			fake++;
			break;
		case 'v':
			verbose++;
			break;
		case 'n':
			nomtab++;
			break;
		case 't':
			opts->fstype = optarg;
			break;
		case 'o':
			opts->opts = append_opt(opts->opts, optarg, NULL);
			break;
		case 'r':
			readonly++;
			break;
		case 'w':
			readwrite++;
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

	parse_opts(opts->opts, &opts->flags, &opts->extra_opts);
}

static struct mntentchn *find_rw_mount(const char *device)
{
	struct mntentchn *mc;
	char *fsname = canonicalize(device);

	mc = getmntdevbackward(fsname, NULL);
	while (mc) {
		if (strcmp(mc->m.mnt_type, fstype) == 0 &&
		    find_opt(mc->m.mnt_opts, "rw", NULL) >= 0)
			break;
		mc = getmntdevbackward(fsname, mc);
	}
	free(fsname);
	return mc;
}

static int mounted(const char *spec, const char *node)
{
	struct mntentchn *mc;
	char *fsname = canonicalize(spec);
	char *dir = canonicalize(node);
	int ret = 0;

	mc = getmntdirbackward(dir, NULL);
	while (mc) {
		if (strcmp(mc->m.mnt_type, fstype) == 0 &&
		    (fsname == NULL || strcmp(mc->m.mnt_fsname, fsname) == 0)) {
			ret = 1;
			break;
		}
		mc = getmntdirbackward(dir, mc);
	}
	free(fsname);
	free(dir);
	return ret;
}

static char *fix_extra_opts_string(const char *exopts, gcpid_opt_t gcpid,
				   pp_opt_t protection_period)
{
	char *s = xstrdup(exopts); /* NULL will be set if exopts == NULL */
	pp_opt_t oldpp;
	gcpid_opt_t oldpid;

	s = replace_drop_opt(s, gcpid_opt_fmt, &oldpid, gcpid, gcpid != 0);
	s = replace_drop_opt(s, pp_opt_fmt, &oldpp, protection_period,
			     protection_period != ULONG_MAX);
	return s;
}

/*
 * based on similar function in util-linux-2.12r/mount/mount.c
 */
static void
update_mtab_entry(const char *spec, const char *node, const char *type,
		  const char *opts, int freq, int pass, int addnew)
{
	struct my_mntent mnt;

	mnt.mnt_fsname = canonicalize(spec);
	mnt.mnt_dir = canonicalize(node);
	mnt.mnt_type = xstrdup(type);
	mnt.mnt_opts = xstrdup(opts);
	mnt.mnt_freq = freq;
	mnt.mnt_passno = pass;

	/* We get chatty now rather than after the update to mtab since the
	   mount succeeded, even if the write to /etc/mtab should fail.  */
	if (verbose)
		print_one(&mnt);

	if (!addnew)
		update_mtab(mnt.mnt_dir, &mnt);
	else {
		mntFILE *mfp;

		lock_mtab();
		mfp = my_setmntent(_PATH_MOUNTED, "a+");
		if (mfp == NULL || mfp->mntent_fp == NULL) {
			int errsv = errno;

			error(_("%s: can't open %s, %s"),
			      progname, _PATH_MOUNTED, strerror(errsv));
		} else {
			if ((my_addmntent(mfp, &mnt)) == 1) {
				int errsv = errno;

				error(_("%s: error writing %s, %s"),
				      progname, _PATH_MOUNTED, strerror(errsv));
			}
		}
		my_endmntent(mfp);
		unlock_mtab();
	}
	free(mnt.mnt_fsname);
	free(mnt.mnt_dir);
	free(mnt.mnt_type);
	free(mnt.mnt_opts);
}

enum remount_type {
	NORMAL_MOUNT,
	RW2RO_REMOUNT,
	RW2RW_REMOUNT,
};

static int check_remount_dir(struct mntentchn *mc, const char *mntdir)
{
	char *dir = canonicalize(mntdir);
	int res = 0;

	if (strcmp(dir, mc->m.mnt_dir) != 0) {
		error(_("%s: different mount point (%s). remount failed."),
		      progname, mntdir);
		res = -1;
	}
	free(dir);
	return res;
}

struct nilfs_mount_info {
	char *device;
	char *mntdir;
	char *optstr;
	pid_t gcpid;
	int type;
	int mounted;
	pp_opt_t protperiod;
	nogc_opt_t nogc;
};

static int check_mtab(void)
{
	int res = 0;

	if (!nomtab) {
		if (mtab_is_writable())
			res++;
		else
			error(_("%s: cannot modify %s.\n"
				"Please remount the partition with -f option after making %s writable."),
			      progname, _PATH_MOUNTED, _PATH_MOUNTED);
	}
	return res;
}

static int
prepare_mount(struct nilfs_mount_info *mi, const struct mount_options *mo)
{
	struct mntentchn *mc;
	gcpid_opt_t pid;
	pp_opt_t prot_period;
	int res = -1;

	if (!(mo->flags & MS_REMOUNT) && mounted(NULL, mi->mntdir)) {
		error(_("%s: %s is already mounted."), progname, mi->mntdir);
		goto failed;
	}

	mi->type = NORMAL_MOUNT;
	mi->gcpid = 0;
	mi->optstr = NULL;
	mi->mounted = mounted(mi->device, mi->mntdir);
	mi->protperiod = ULONG_MAX;

	if (mo->flags & MS_BIND)
		return 0;

	mc = find_rw_mount(mi->device);
	if (mc == NULL)
		return 0; /* no previous rw-mount */

	/* get the value of previous pp option if exists */
	prot_period = ULONG_MAX;
	if (find_opt(mc->m.mnt_opts, pp_opt_fmt, &prot_period) >= 0)
		mi->protperiod = prot_period;

	mi->nogc = (find_opt(mc->m.mnt_opts, nogc_opt_fmt, NULL) >= 0);

	switch (mo->flags & (MS_RDONLY | MS_REMOUNT)) {
	case 0: /* overlapping rw-mount */
		error(_("%s: the device already has a rw-mount on %s.\n"
			"\t\tmultiple rw-mount is not allowed."),
		      progname, mc->m.mnt_dir);
		goto failed;
	case MS_RDONLY: /* ro-mount (a rw-mount exists) */
		break;
	case MS_REMOUNT | MS_RDONLY: /* rw->ro remount */
	case MS_REMOUNT: /* rw->rw remount */
		mi->type = (mo->flags & MS_RDONLY) ?
			RW2RO_REMOUNT : RW2RW_REMOUNT;

		if (check_remount_dir(mc, mi->mntdir) < 0)
			goto failed;
		pid = 0;
		if (find_opt(mc->m.mnt_opts, gcpid_opt_fmt, &pid) >= 0 &&
		    nilfs_shutdown_cleanerd(mi->device, (pid_t)pid) < 0) {
			error(_("%s: remount failed due to %s shutdown failure"),
				progname, NILFS_CLEANERD_NAME);
			goto failed;
		}
		mi->gcpid = pid;
		mi->optstr = xstrdup(mc->m.mnt_opts); /* previous opts */
		break;
	}

	res = 0;
 failed:
	return res;
}

static int
do_mount_one(struct nilfs_mount_info *mi, const struct mount_options *mo)
{
	int res, errsv, mtab_ok;
	char *exopts = xstrdup(mo->extra_opts);
	pp_opt_t oldpp;

	/*
	 * Get rid of pp option and nogc option.  We do not have to
	 * remove gcpid option because it is not given by command line
	 * of the program.
	 */
	exopts = replace_opt(exopts, pp_opt_fmt, &oldpp, NULL);
	exopts = replace_opt(exopts, nogc_opt_fmt, NULL, NULL);

	res = mount(mi->device, mi->mntdir, fstype, mo->flags & ~MS_NOSYS,
		    exopts);
	if (!res)
		goto out;

	errsv = errno;
	switch (errsv) {
	case ENODEV:
		error(_("%s: cannot find or load %s filesystem"),
		      progname, fstype);
		break;
	default:
		error(_("%s: Error while mounting %s on %s: %s"),
		      progname, mi->device, mi->mntdir, strerror(errsv));
		break;
	}
	if (mi->type != RW2RO_REMOUNT && mi->type != RW2RW_REMOUNT)
		goto out;

	mtab_ok = check_mtab();

	/* Cleaner daemon was stopped and it needs to run */
	/* because filesystem is still mounted */
	if (!mi->nogc && mtab_ok) {
		/* Restarting cleaner daemon */
		if (nilfs_launch_cleanerd(mi->device, mi->mntdir,
					  mi->protperiod, &mi->gcpid) == 0) {
			gcpid_opt_t oldpid;

			if (verbose)
				printf(_("%s: restarted %s\n"),
				       progname, NILFS_CLEANERD_NAME);

			mi->optstr = replace_drop_opt(
				mi->optstr, gcpid_opt_fmt, &oldpid, mi->gcpid,
				mi->gcpid != 0);

			update_mtab_entry(mi->device, mi->mntdir, fstype,
					  mi->optstr, 0, 0, !mi->mounted);
		} else {
			error(_("%s: failed to restart %s"),
			      progname, NILFS_CLEANERD_NAME);
		}
	} else
		printf(_("%s not restarted\n"), NILFS_CLEANERD_NAME);
 out:
	free(exopts);
	return res;
}

static void update_mount_state(struct nilfs_mount_info *mi,
			       const struct mount_options *mo)
{
	pid_t pid = 0;
	pp_opt_t pp = ULONG_MAX;
	char *exopts;
	int rungc;

	rungc = find_opt(mo->extra_opts, nogc_opt_fmt, NULL) < 0 &&
		!(mo->flags & MS_RDONLY) && !(mo->flags & MS_BIND);

	if (rungc) {
		if (find_opt(mo->extra_opts, pp_opt_fmt, &pp) < 0)
			pp = mi->protperiod;
		if (nilfs_launch_cleanerd(mi->device, mi->mntdir, pp,
					  &pid) < 0)
			error(_("%s aborted"), NILFS_CLEANERD_NAME);
		else if (verbose)
			printf(_("%s: started %s\n"), progname,
			       NILFS_CLEANERD_NAME);
	}

	if (!check_mtab())
		return;

	free(mi->optstr);
	exopts = fix_extra_opts_string(mo->extra_opts, pid, pp);
	mi->optstr = fix_opts_string(((mo->flags & ~MS_NOMTAB) | MS_NETDEV),
				     exopts, NULL);

	update_mtab_entry(mi->device, mi->mntdir, fstype, mi->optstr, 0, 0,
			  !mi->mounted);
	free(exopts);
}

static int mount_one(char *device, char *mntdir,
		     const struct mount_options *opts)
{
	struct nilfs_mount_info mi = { .device = device, .mntdir = mntdir };
	int res, err = EX_FAIL;

	res = prepare_mount(&mi, opts);
	if (res)
		goto failed;

	if (!fake) {
		res = do_mount_one(&mi, opts);
		if (res)
			goto failed;
	}
	update_mount_state(&mi, opts);

#if defined(HAVE_LIBSELINUX) && defined(HAVE_SECURITY_GET_INITIAL_CONTEXT)
	if (verbose && is_selinux_enabled() > 0) {
		security_context_t raw = NULL, def = NULL;

		if (getfilecon(mntdir, &raw) > 0 &&
		    security_get_initial_context("file", &def) == 0) {

			if (!selinux_file_context_cmp(raw, def))
				printf(_("%s: %s does not contain SELinux labels.\n"
					 "       You just mounted an file system that supports labels which does not\n"
					 "       contain labels, onto an SELinux box. It is likely that confined\n"
					 "       applications will generate AVC messages and not be allowed access to\n"
					 "       this file system.  For more details see restorecon(8) and mount(8).\n"),
				       progname, mntdir);
		}
		freecon(raw);
		freecon(def);
	}
#endif

	err = 0;
 failed:
	free(mi.optstr);
	return err;
}

int main(int argc, char *argv[])
{
	struct mount_options *opts = &options;
	char *device, *mntdir;
	int res = 0;

	if (argc > 0) {
		char *cp = strrchr(argv[0], '/');

		progname = (cp ? cp + 1 : argv[0]);
	}

	nilfs_cleaner_logger = nilfs_mount_logger;

	parse_options(argc, argv, opts);

	umask(022);

	if (optind >= argc || !argv[optind])
		die(EX_USAGE, _("No device specified"));

	device = argv[optind++];

	if (optind >= argc || !argv[optind])
		die(EX_USAGE, _("No mountpoint specified"));

	mntdir = argv[optind++];

	if (opts->fstype && strncmp(opts->fstype, fstype, strlen(fstype)))
		die(EX_USAGE, _("Unknown filesystem (%s)"), opts->fstype);

	if (getuid() != geteuid())
		die(EX_USAGE,
		    _("%s: mount by non-root user is not supported yet"),
		    progname);

	if (!nomtab && mtab_does_not_exist())
		die(EX_USAGE, _("%s: no %s found - aborting"), progname,
		    _PATH_MOUNTED);

	if (!(opts->flags & MS_RDONLY) && !(opts->flags & MS_BIND)) {
		res = device_is_readonly(device, &devro);
		if (res)
			die(EX_USAGE, _("%s: device %s not accessible: %s"),
			    progname, device, strerror(res));
	}


	if (signal(SIGTERM, handle_signal) == SIG_ERR)
		die(EX_SYSERR, _("Could not set SIGTERM"));

	if (signal(SIGINT, handle_signal) == SIG_ERR)
		die(EX_SYSERR, _("Could not set SIGINT"));

	atexit(unlock_mtab);

	block_signals(SIG_BLOCK);
	res = mount_one(device, mntdir, opts);
	block_signals(SIG_UNBLOCK);

	free(opts->opts);
	free(opts->extra_opts);
	return res;
}
