/*
 * cleaner_ctl.c - cleanerd controlling routine
 *
 * Licensed under LGPLv2: the complete text of the GNU Lesser General
 * Public License can be found in COPYING file of the nilfs-utils
 * package.
 *
 * Copyright (C) 2007-2011 Nippon Telegraph and Telephone Corporation.
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

#if HAVE_STRING_H
#include <string.h>
#endif	/* HAVE_STRING_H */

#if HAVE_LIMITS_H
#include <limits.h>	/* ULONG_MAX */
#endif	/* HAVE_LIMITS_H */

#if HAVE_SYS_STAT_H
#include <sys/stat.h>
#endif	/* HAVE_SYS_STAT_H */

#if HAVE_SYS_WAIT_H
#include <sys/wait.h>
#endif	/* HAVE_SYS_WAIT_H */

#if HAVE_MNTENT_H && HAVE_GETMNTENT_R
#include <mntent.h>
#endif  /* HAVE_GETMNT_H && HAVE_GETMNTENT_R */

#if HAVE_SYSLOG_H
#include <syslog.h>
#endif	/* HAVE_SYSLOG_H */

#include <signal.h>
#include <stdarg.h>
#include <errno.h>

#include "nilfs_cleaner.h"
#include "nls.h"
#include "realpath.h"

struct nilfs_cleaner {
	pid_t cleanerd_pid;
	char *device;
	char *mountdir;
	dev_t dev_id;
	ino_t dev_ino; /* optional (in case of directory or regular file) */
};

#define CLEANERD_WAIT_RETRY_COUNT	3
#define CLEANERD_WAIT_RETRY_INTERVAL	2  /* in seconds */

#ifndef LINE_MAX
#define LINE_MAX	2048
#endif	/* LINE_MAX */

#ifndef MTAB
#define MTAB  "/etc/mtab"
#endif

#ifndef MNTTYPE_NILFS
#define MNTTYPE_NILFS	"nilfs2"
#endif

static const char cleanerd[] = "/sbin/" NILFS_CLEANERD_NAME;
static const char cleanerd_nofork_opt[] = "-n";
static const char cleanerd_protperiod_opt[] = "-p";

static void default_logger(int priority, const char *fmt, ...)
{
	va_list args;

	if (priority >= LOG_INFO)
		return;
	va_start(args, fmt);
	vfprintf(stderr, fmt, args);
	fputs(_("\n"), stderr);
	va_end(args);
}

static void default_printf(const char *fmt, ...)
{
	va_list args;

	va_start(args, fmt);
	vprintf(fmt, args);
	va_end(args);
}

static void default_flush(void)
{
	fflush(stdout);
}

void (*nilfs_cleaner_logger)(int priority, const char *fmt, ...)
	= default_logger;
void (*nilfs_cleaner_printf)(const char *fmt, ...) = default_printf;
void (*nilfs_cleaner_flush)(void) = default_flush;


static inline int process_is_alive(pid_t pid)
{
	return (kill(pid, 0) == 0);
}

static int nilfs_cleaner_get_device_id(struct nilfs_cleaner *cleaner)
{
	struct stat stbuf;
	int ret;

	ret = stat(cleaner->device, &stbuf);
	if (ret < 0)
		goto error;

	if (S_ISBLK(stbuf.st_mode)) {
		cleaner->dev_id = stbuf.st_rdev;
		cleaner->dev_ino = 0;
	} else if (S_ISREG(stbuf.st_mode) || S_ISDIR(stbuf.st_mode)) {
		cleaner->dev_id = stbuf.st_dev;
		cleaner->dev_ino = stbuf.st_ino;
	} else {
		nilfs_cleaner_logger(
			LOG_ERR, _("Error: invalid device %s"),
			cleaner->device);
		errno = EINVAL;
		goto abort;
	}
	return 0;
error:
	nilfs_cleaner_logger(LOG_ERR,  _("Error: %s"), strerror(errno));
abort:
	return -1;
}

static int nilfs_find_gcpid_opt(const char *opts, pid_t *pid)
{
	char *opts2, *opt;
	char *saveptr;
	long pid2;
	int ret = -1;

	if (!opts)
		goto out;

	opts2 = strdup(opts);
	if (!opts2)
		goto out;

	ret = 0;
	opt = strtok_r(opts2, ",", &saveptr);
	while (opt) {
		if (sscanf(opt, PIDOPT_NAME "=%ld", &pid2) == 1) {
			*pid = pid2;
			ret = 1;
			break;
		}
		opt = strtok_r(NULL, ",", &saveptr);
	}
	free(opts2);
out:
	return ret;
}

static int nilfs_cleaner_find_fs(struct nilfs_cleaner *cleaner,
				 const char *device, const char *mntdir)
{
	struct mntent *mntent, mntbuf;
	char buf[LINE_MAX];
	char canonical[PATH_MAX + 2];
	char *cdev = NULL, *cdir = NULL;
	char *mdir, *mdev;
	char *last_match_dev = NULL, *last_match_dir = NULL;
	pid_t last_match_pid = 0;
	FILE *fp = NULL;
	int ret = -1, nfound = 0;

	if (device && myrealpath(device, canonical, sizeof(canonical))) {
		cdev = strdup(canonical);
		if (!cdev)
			goto error;
		cleaner->device = cdev;
	}

	if (mntdir && myrealpath(mntdir, canonical, sizeof(canonical))) {
		cdir = strdup(canonical);
		if (!cdir)
			goto error;
		cleaner->mountdir = cdir;
	}

	fp = fopen(MTAB, "r");
	if (fp == NULL) {
		nilfs_cleaner_logger(LOG_ERR, _("Error: cannot open " MTAB));
		goto abort;
	}

	while ((mntent = getmntent_r(fp, &mntbuf, buf, sizeof(buf))) != NULL) {
		if (strcmp(mntent->mnt_type, MNTTYPE_NILFS) != 0)
			continue;

		if (cleaner->mountdir != NULL) {
			mdir = mntent->mnt_dir;
			if (myrealpath(mdir, canonical, sizeof(canonical)))
				mdir = canonical;
			if (strcmp(mdir, cleaner->mountdir) != 0)
				continue;
		}
			
		if (cleaner->device != NULL) {
			mdev = mntent->mnt_fsname;
			if (myrealpath(mdev, canonical, sizeof(canonical)))
				mdev = canonical;
			if (strcmp(mdev, cleaner->device) != 0)
				continue;
		}

		if (hasmntopt(mntent, MNTOPT_RW)) {
			/* we found a candidate */
			nfound++;

			if (cleaner->device == NULL) {
				mdev = mntent->mnt_fsname;
				if (myrealpath(mdev, canonical,
					       sizeof(canonical))) {
					mdev = canonical;
				}
				if (last_match_dev)
					free(last_match_dev);
				last_match_dev = strdup(mdev);
				if (!last_match_dev)
					goto error;
			}
			if (cleaner->mountdir == NULL) {
				mdir = mntent->mnt_dir;
				if (myrealpath(mdir, canonical,
					       sizeof(canonical))) {
					mdir = canonical;
				}
				if (last_match_dir)
					free(last_match_dir);
				last_match_dir = strdup(mdir);
				if (!last_match_dir)
					goto error;
			}
			last_match_pid = 0;
			ret = nilfs_find_gcpid_opt(mntent->mnt_opts,
						   &last_match_pid);
			if (ret < 0)
				goto error;
		}
	}
	if (nfound == 0) {
		nilfs_cleaner_logger(LOG_ERR,
				     _("Error: cannot find valid mount"));
		goto abort;
	}
	if (last_match_dir)
		cleaner->mountdir = last_match_dir;
	if (last_match_dev)
		cleaner->device = last_match_dev;
	if (last_match_pid)
		cleaner->cleanerd_pid = last_match_pid;

	endmntent(fp);
	return 0;
error:
	nilfs_cleaner_logger(LOG_ERR,  _("Error: %s"), strerror(errno));
abort:
	free(last_match_dir); /* free(NULL) is just ignored */
	free(last_match_dev);
	if (fp)
		endmntent(fp);
	return -1;
}

int nilfs_launch_cleanerd(const char *device, const char *mntdir,
			  unsigned long protperiod, pid_t *ppid)
{
	const char *dargs[7];
	struct stat statbuf;
	sigset_t sigs;
	int i = 0;
	int ret;
	char buf[256];

	if (stat(cleanerd, &statbuf) != 0) {
		nilfs_cleaner_logger(LOG_ERR,  _("Error: %s not found"),
				     NILFS_CLEANERD_NAME);
		return -1;
	}

	ret = fork();
	if (ret == 0) {
		if (setgid(getgid()) < 0) {
			nilfs_cleaner_logger(
				LOG_ERR,
				_("Error: failed to drop setgid privileges"));
			exit(1);
		}
		if (setuid(getuid()) < 0) {
			nilfs_cleaner_logger(
				LOG_ERR,
				_("Error: failed to drop setuid privileges"));
			exit(1);
		}
		dargs[i++] = cleanerd;
		dargs[i++] = cleanerd_nofork_opt;
		if (protperiod != ULONG_MAX) {
			dargs[i++] = cleanerd_protperiod_opt;
			snprintf(buf, sizeof(buf), "%lu", protperiod);
			dargs[i++] = buf;
		}
		dargs[i++] = device;
		dargs[i++] = mntdir;
		dargs[i] = NULL;

		sigfillset(&sigs);
		sigdelset(&sigs, SIGTRAP);
		sigdelset(&sigs, SIGSEGV);
		sigprocmask(SIG_UNBLOCK, &sigs, NULL);

		execv(cleanerd, (char **)dargs);
		exit(1);   /* reach only if failed */
	} else if (ret != -1) {
		*ppid = ret;
		return 0; /* cleanerd started */
	} else {
		int errsv = errno;
		nilfs_cleaner_logger(LOG_ERR, _("Error: could not fork: %s"),
				     strerror(errsv));
	}
	return -1;
}

int nilfs_ping_cleanerd(pid_t pid)
{
	return process_is_alive(pid);
}

static int nilfs_wait_cleanerd(const char *device, pid_t pid)
{
	int cnt = CLEANERD_WAIT_RETRY_COUNT;
	int ret;

	sleep(0);
	if (!process_is_alive(pid))
		return 0;
	sleep(1);
	if (!process_is_alive(pid))
		return 0;

	nilfs_cleaner_printf(_("cleanerd (pid=%ld) still exists on %d. "
			       "waiting."),
			     (long)pid, device);
	nilfs_cleaner_flush();

	for (;;) {
		if (cnt-- < 0) {
			nilfs_cleaner_printf(_("failed\n"));
			nilfs_cleaner_flush();
			ret = -1; /* wait failed */
			break;
		}
		sleep(CLEANERD_WAIT_RETRY_INTERVAL);
		if (!process_is_alive(pid)) {
			nilfs_cleaner_printf(_("done\n"));
			nilfs_cleaner_flush();
			ret = 0;
			break;
		}
		nilfs_cleaner_printf(_("."));
		nilfs_cleaner_flush();
	}
	return ret;
}

int nilfs_shutdown_cleanerd(const char *device, pid_t pid)
{
	int ret;

	nilfs_cleaner_logger(LOG_INFO, _("kill cleanerd (pid=%ld) on %s"),
			     (long)pid, device);

	if (kill(pid, SIGTERM) < 0) {
		int errsv = errno;

		ret = 0;
		if (errsv == ESRCH) {
			goto out;
		} else {
			nilfs_cleaner_logger(
				LOG_ERR, _("Error: cannot kill cleanerd: %s"),
				strerror(errsv));
			ret = -1;
			goto out;
		}
	}
	ret = nilfs_wait_cleanerd(device, pid);
	if (ret < 0)
		nilfs_cleaner_logger(LOG_INFO, _("wait timeout"));
	else
		nilfs_cleaner_logger(LOG_INFO, _("cleanerd (pid=%ld) stopped"),
				     pid);
out:
	return ret;
}

struct nilfs_cleaner *nilfs_cleaner_launch(const char *device,
					   const char *mntdir,
					   unsigned long protperiod)
{
	struct nilfs_cleaner *cleaner;

	cleaner = malloc(sizeof(*cleaner));
	if (!cleaner)
		goto error;
	memset(cleaner, 0, sizeof(*cleaner));

	cleaner->device = strdup(device);
	cleaner->mountdir = strdup(mntdir);
	if (!cleaner->device || !cleaner->mountdir)
		goto error;

	if (nilfs_launch_cleanerd(device, mntdir, protperiod,
				  &cleaner->cleanerd_pid) < 0)
		goto abort;

	if (nilfs_cleaner_get_device_id(cleaner) < 0)
		goto abort;

	return cleaner; /* cleanerd started */

error:
	nilfs_cleaner_logger(LOG_ERR,  _("Error: %s"), strerror(errno));
abort:
	if (cleaner) {
		free(cleaner->device); /* free(NULL) is just ignored */
		free(cleaner->mountdir);
		free(cleaner);
	}
	return NULL;
}

struct nilfs_cleaner *nilfs_cleaner_open(const char *device,
					 const char *mntdir, int oflag)
{
	struct nilfs_cleaner *cleaner;

	cleaner = malloc(sizeof(*cleaner));
	if (!cleaner)
		goto error;
	memset(cleaner, 0, sizeof(*cleaner));

	if (nilfs_cleaner_find_fs(cleaner, device, mntdir) < 0)
		goto abort;

	if (nilfs_cleaner_get_device_id(cleaner) < 0)
		goto abort;

	if ((oflag & NILFS_CLEANER_OPEN_GCPID) && cleaner->cleanerd_pid == 0) {
		nilfs_cleaner_logger(LOG_ERR,
				     _("Error: cannot get cleanerd pid"));
		goto abort;
	}

	return cleaner;

error:
	nilfs_cleaner_logger(LOG_ERR,  _("Error: %s"), strerror(errno));
abort:
	if (cleaner) {
		free(cleaner->device); /* free(NULL) is just ignored */
		free(cleaner->mountdir);
		free(cleaner);
	}
	return NULL;
}

int nilfs_cleaner_ping(struct nilfs_cleaner *cleaner)
{
	return process_is_alive(cleaner->cleanerd_pid);
}

pid_t nilfs_cleaner_pid(const struct nilfs_cleaner *cleaner)
{
	return cleaner->cleanerd_pid;
}

const char *nilfs_cleaner_device(const struct nilfs_cleaner *cleaner)
{
	return cleaner->device;
}

void nilfs_cleaner_close(struct nilfs_cleaner *cleaner)
{
	free(cleaner->device);
	free(cleaner->mountdir);
	free(cleaner);
}

int nilfs_cleaner_shutdown(struct nilfs_cleaner *cleaner)
{
	int ret = -1;

	if (!cleaner->cleanerd_pid) {
		nilfs_cleaner_logger(
			LOG_DEBUG, _("No valid pid is set -- skip killing"));
	} else {
		ret = nilfs_shutdown_cleanerd(cleaner->device,
					      cleaner->cleanerd_pid);
	}
	nilfs_cleaner_close(cleaner);
	return ret;
}
