/*
 * mount.nilfs2.c - NILFS mounter (mount.nilfs2)
 *
 * Copyright (C) 2007 Nippon Telegraph and Telephone Corporation.
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
 * Written by Ryusuke Konishi <ryusuke@osrg.net>
 *         using examples from util-linux-2.12r/mount.c.
 *
 * The following functions are extracted from util-linux-2.12r/mount.c:
 *  - print_one()
 *  - update_mtab_entry()
 *  - my_free()
 */

#define _LARGEFILE64_SOURCE
#define _XOPEN_SOURCE 600

#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <strings.h>
#include <stdarg.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <signal.h>
#include <errno.h>

#include "fstab.h"
#include "paths.h"
#include "sundries.h"
#include "xmalloc.h"
#include "mntent.h"
#include "mount_constants.h"
#include "mount_opts.h"
#include "mount.nilfs2.h"
#include "nls.h"

int verbose = 0;
int mount_quiet = 0;
int readonly = 0;
int readwrite = 0;
static int nomtab = 0;
static int devro = 0;

extern char *optarg;
extern int optind;

const char fstype[] = NILFS2_FS_NAME;
char *progname = "mount." NILFS2_FS_NAME;

const char gcpid_opt_fmt[] = PIDOPT_NAME "=%d";
typedef int gcpid_opt_t;

struct mount_options {
	char *fstype;
	char *opts;
	char *extra_opts;
	int flags;
};
struct mount_options options;


/* Report on a single mount.  */
static void print_one (const struct my_mntent *me)
{
	if (mount_quiet)
		return;
	printf ("%s on %s", me->mnt_fsname, me->mnt_dir);
	if (me->mnt_type != NULL && *(me->mnt_type) != '\0')
		printf (" type %s", me->mnt_type);
	if (me->mnt_opts != NULL)
		printf (" (%s)", me->mnt_opts);
#if 0  /* XXX: volume label */
	if (list_with_volumelabel) {
		const char *label;
		label = mount_get_volume_label_by_spec(me->mnt_fsname);
		if (label) {
			printf (" [%s]", label);
			/* free(label); */
		}
	}
#endif
	printf ("\n");
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

static inline void my_free(const void *ptr)
{
	/* free(NULL) is ignored; the check below is just to be sure */
	if (ptr)
		free((void *)ptr);
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

static void parse_options(int argc, char *argv[], struct mount_options *opts)
{
	int c;

	while ((c = getopt(argc, argv, "vnt:o:rw")) != EOF) {
		switch (c) {
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
			if (opts->opts)
				opts->opts = xstrconcat3(opts->opts, ",",
							 optarg);
			else
				opts->opts = xstrdup(optarg);
			break;
		case 'r':
			readonly++;
			break;
		case 'w':
			readwrite++;
			break;
		default:
			break;
		}
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
	my_free(fsname);
	return mc;
}

static void update_gcpid_opt(char **opts, pid_t newpid)
{
	char buf[256], *newopts;
	gcpid_opt_t oldpid;

	snprintf(buf, sizeof(buf), gcpid_opt_fmt, (int)newpid);
	newopts = change_opt(*opts, gcpid_opt_fmt, &oldpid, buf);
	my_free(*opts);
	*opts = newopts;
}

static char *fix_extra_opts_string(const char *extra_opts, pid_t gcpid)
{
	char buf[256];
	gcpid_opt_t id;

	buf[0] = '\0';
	if (gcpid)
		snprintf(buf, sizeof(buf), gcpid_opt_fmt, (int)gcpid);
	/* The gcpid option will be removed if gcpid == 0 */
	return change_opt(extra_opts, gcpid_opt_fmt, &id, buf);
}

/*
 * based on similar function in util-linux-2.12r/mount/mount.c
 */
static void
update_mtab_entry(const char *spec, const char *node, const char *type,
		  const char *opts, int freq, int pass, int addnew)
{
	struct my_mntent mnt;

	mnt.mnt_fsname = canonicalize (spec);
	mnt.mnt_dir = canonicalize (node);
	mnt.mnt_type = xstrdup(type);
	mnt.mnt_opts = xstrdup(opts);
	mnt.mnt_freq = freq;
	mnt.mnt_passno = pass;

	/* We get chatty now rather than after the update to mtab since the
	   mount succeeded, even if the write to /etc/mtab should fail.  */
	if (verbose)
		print_one (&mnt);

	if (!addnew)
		update_mtab (mnt.mnt_dir, &mnt);
	else {
		mntFILE *mfp;

		lock_mtab();
		mfp = my_setmntent(MOUNTED, "a+");
		if (mfp == NULL || mfp->mntent_fp == NULL) {
			int errsv = errno;
			error(_("%s: can't open %s, %s"),
			      progname, MOUNTED, strerror(errsv));
		} else {
			if ((my_addmntent (mfp, &mnt)) == 1) {
				int errsv = errno;
				error(_("%s: error writing %s, %s"),
				      progname, MOUNTED, strerror(errsv));
			}
		}
		my_endmntent(mfp);
		unlock_mtab();
	}
	my_free(mnt.mnt_fsname);
	my_free(mnt.mnt_dir);
	my_free(mnt.mnt_type);
	my_free(mnt.mnt_opts);
}

enum remount_type {
	NORMAL_MOUNT,
	RW2RO_REMOUNT,
	RW2RW_REMOUNT,
};

static int check_remount_dir(struct mntentchn *mc, const char *mntdir)
{
	const char *dir = canonicalize(mntdir);
	int res = 0;

	if (strcmp(dir, mc->m.mnt_dir) != 0) {
		error(_("%s: different mount point (%s). remount failed."),
		      progname, mntdir);
		res = -1;
	}
	my_free(dir);
	return res;
}

struct nilfs_mount_info {
	char *device;
	char *mntdir;
	char *optstr;
	pid_t gcpid;
	int type;
};

static int check_mtab(void)
{
	int res = 0;

	if (!nomtab) {
		if (mtab_is_writable())
			res++;
		else
			error(_("%s: cannot modify " MOUNTED ".\n"
				"Please remount the partition with -f option"
				" after making " MOUNTED " writable."),
			       progname);
	}
	return res;
}

static int
prepare_mount(struct nilfs_mount_info *mi, const struct mount_options *mo)
{
	struct mntentchn *mc;
	gcpid_opt_t pid;
	int res = -1;

	mi->type = NORMAL_MOUNT;
	mi->gcpid = 0;
	mi->optstr = NULL;

	if (!(mo->flags & MS_RDONLY)) { /* rw-mount */
		if ((mc = find_rw_mount(mi->device)) == NULL)
			goto out;
		if (!(mo->flags & MS_REMOUNT)) {
			error(_("%s: the device already has a rw-mount on %s."
				"\n\t\tmultiple rw-mount is not supported."),
			      progname, mc->m.mnt_dir);
			goto failed;
		}

		/* rw->rw remount */
		if (check_remount_dir(mc, mi->mntdir) < 0)
			goto failed;
		
		if (find_opt(mc->m.mnt_opts, gcpid_opt_fmt, &pid) >= 0)
			mi->gcpid = pid;
		mi->optstr = xstrdup(mc->m.mnt_opts); /* previous opts */
		mi->type = RW2RW_REMOUNT;
	} else if (mo->flags & MS_REMOUNT) { /* ro-remount */
		if ((mc = find_rw_mount(mi->device)) == NULL)
			goto out;

		/* rw->ro remount */
		if (check_remount_dir(mc, mi->mntdir) < 0)
			goto failed;
		pid = 0;
		if (find_opt(mc->m.mnt_opts, gcpid_opt_fmt, &pid) >= 0 &&
		    stop_cleanerd(mi->device, (pid_t)pid) < 0) {
			error(_("%s: remount failed due to %s shutdown "
				"failure"), progname, CLEANERD_NAME);
			goto failed;
		}
		mi->gcpid = pid;
		mi->optstr = xstrdup(mc->m.mnt_opts); /* previous opts */
		mi->type = RW2RO_REMOUNT;
	}
 out:
	res = 0;
 failed:
	return res;
}

static int
do_mount_one(struct nilfs_mount_info *mi, const struct mount_options *mo)
{
	int res, errsv;

	res = mount(mi->device, mi->mntdir, fstype, mo->flags & ~MS_NOSYS,
		    mo->extra_opts);
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
	if (mi->type != RW2RO_REMOUNT)
		goto out;
	if (check_mtab()) {
		/* Restarting cleaner daemon */
		if (start_cleanerd(mi->device, mi->mntdir, &mi->gcpid) == 0) {
			if (verbose)
				printf(_("%s: restarted %s\n"),
				       progname, CLEANERD_NAME);
			update_gcpid_opt(&mi->optstr, mi->gcpid);
			update_mtab_entry(mi->device, mi->mntdir, fstype,
					  mi->optstr, 0, 0, 0);
		} else {
			error(_("%s: failed to restart %s"),
			      progname, CLEANERD_NAME);
		}
	} else
		printf(_("%s not restarted\n"), CLEANERD_NAME);
 out:
	return res;
}

static void update_mount_state(struct nilfs_mount_info *mi,
			       const struct mount_options *mo)
{
	pid_t pid = mi->gcpid;
	char *exopts;
	int rungc;

	rungc = !(mo->flags & MS_RDONLY) &&
		(mi->type != RW2RW_REMOUNT || mi->gcpid == 0);
	if (!check_mtab()) {
		if (rungc)
			printf(_("%s not started\n"), CLEANERD_NAME);
		return;
	}
	if (rungc) {
		if (start_cleanerd(mi->device, mi->mntdir, &pid) < 0)
			error(_("%s aborted"), CLEANERD_NAME);
		else if (verbose)
			printf(_("%s: started %s\n"), progname, CLEANERD_NAME);
	}
	my_free(mi->optstr);
	exopts = fix_extra_opts_string(mo->extra_opts, pid);
	mi->optstr = fix_opts_string(((mo->flags & ~MS_NOMTAB) | MS_NETDEV),
				     exopts, NULL);
		
	update_mtab_entry(mi->device, mi->mntdir, fstype, mi->optstr, 0, 0,
			  !(mo->flags & MS_REMOUNT));
	my_free(exopts);
}

static int mount_one(char *device, char *mntdir,
		     const struct mount_options *opts)
{
	struct nilfs_mount_info mi = { .device = device, .mntdir = mntdir };
	int res, err = EX_FAIL;

	res = prepare_mount(&mi, opts);
	if (res)
		goto failed;

	res = do_mount_one(&mi, opts);
	if (res)
		goto failed;

	update_mount_state(&mi, opts);
	err = 0;
 failed:
	my_free(mi.optstr);
	return err;
}

int main(int argc, char *argv[])
{
	struct mount_options *opts = &options;
	char *device, *mntdir;
	int res = 0; 

	parse_options(argc, argv, opts);

	if (argc > 0) {
		char *cp = strrchr(argv[0], '/');

		progname = (cp ? cp + 1 : argv[0]);
	}

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
		    MOUNTED);

	if (opts->flags & MS_RDONLY) {
		res = device_is_readonly(device, &devro);
		if (res)
			die(EX_USAGE, _("%s: device %s not accessible: %s"),
			    progname, device, strerror(res));
	}


	if (signal(SIGTERM, handle_signal) == SIG_ERR)
		die(EX_SYSERR, _("Could not set SIGTERM"));

	if (signal(SIGINT, handle_signal) == SIG_ERR)
		die(EX_SYSERR, _("Could not set SIGINT"));

	block_signals(SIG_BLOCK);
	res = mount_one(device, mntdir, opts);
	block_signals(SIG_UNBLOCK);

	my_free(opts->opts);
	my_free(opts->extra_opts);
	return res;
}

/* Local Variables:		*/
/* eval: (c-set-style "linux")	*/
/* End:				*/
