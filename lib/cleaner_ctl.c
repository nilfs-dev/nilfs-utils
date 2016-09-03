/*
 * cleaner_ctl.c - cleanerd controlling routine
 *
 * Licensed under LGPLv2: the complete text of the GNU Lesser General
 * Public License can be found in COPYING file of the nilfs-utils
 * package.
 *
 * Copyright (C) 2007-2012 Nippon Telegraph and Telephone Corporation.
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

#if HAVE_SYS_TIME_H
#include <sys/time.h>	/* timeradd */
#endif	/* HAVE_SYS_TIME */

#if HAVE_TIME_H
#include <time.h>
#endif	/* HAVE_TIME_H */

#if HAVE_SYS_WAIT_H
#include <sys/wait.h>
#endif	/* HAVE_SYS_WAIT_H */

#if HAVE_MNTENT_H && HAVE_GETMNTENT_R
#include <mntent.h>
#endif  /* HAVE_GETMNT_H && HAVE_GETMNTENT_R */

#if HAVE_SYSLOG_H
#include <syslog.h>
#endif	/* HAVE_SYSLOG_H */

#if HAVE_MQUEUE_H
#include <mqueue.h>
#endif	/* HAVE_MQUEUE_H */

#if HAVE_POLL_H
#include <poll.h>
#endif	/* HAVE_POLL_H */

#include <signal.h>
#include <stdarg.h>
#include <errno.h>
#include <assert.h>
#include <uuid/uuid.h>

#include "nilfs_cleaner.h"
#include "cleaner_exec.h"
#include "cleaner_msg.h"
#include "nls.h"
#include "pathnames.h"
#include "realpath.h"

struct nilfs_cleaner {
	pid_t cleanerd_pid;
	char *device;
	char *mountdir;
	dev_t dev_id;
	ino_t dev_ino; /* optional (in case of directory or regular file) */
	mqd_t sendq;
	mqd_t recvq;
	char *recvq_name;
	uuid_t client_uuid;
};

#ifndef LINE_MAX
#define LINE_MAX	2048
#endif	/* LINE_MAX */

#ifndef MNTTYPE_NILFS
#define MNTTYPE_NILFS	"nilfs2"
#endif

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

	fp = fopen(_PATH_MOUNTED, "r");
	if (fp == NULL) {
		nilfs_cleaner_logger(LOG_ERR, _("Error: cannot open "
						_PATH_MOUNTED "."));
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
				     _("Error: no valid nilfs mountpoint found."));
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
	nilfs_cleaner_logger(LOG_ERR,  _("Error: failed to find fs: %s."),
			     strerror(errno));
abort:
	free(last_match_dir); /* free(NULL) is just ignored */
	free(last_match_dev);
	if (fp)
		endmntent(fp);
	return -1;
}

static int nilfs_cleaner_open_queue(struct nilfs_cleaner *cleaner)
{
	char nambuf[NAME_MAX - 4];
	char uuidbuf[36 + 1];
	struct mq_attr attr = {
		.mq_maxmsg = 3,
		.mq_msgsize = sizeof(struct nilfs_cleaner_response)
	};
	int ret;

	uuid_generate(cleaner->client_uuid);
	uuid_unparse_lower(cleaner->client_uuid, uuidbuf);

	/* receive queue */
	ret = snprintf(nambuf, sizeof(nambuf), "/nilfs-cleanerq-%s", uuidbuf);
	if (ret < 0)
		goto error;

	assert(ret < sizeof(nambuf));
	cleaner->recvq_name = strdup(nambuf);
	if (!cleaner->recvq_name)
		goto error;

	cleaner->recvq = mq_open(nambuf, O_RDONLY | O_CREAT | O_EXCL, 0600,
				 &attr);
	if (cleaner->recvq < 0) {
		nilfs_cleaner_logger(LOG_ERR,
				     _("Error: cannot create receive queue: %s."),
				     strerror(errno));
		free(cleaner->recvq_name);
		goto abort;
	}
	/* send queue */
	if (cleaner->dev_ino == 0) {
		ret = snprintf(nambuf, sizeof(nambuf),
			       "/nilfs-cleanerq-%llu",
			       (unsigned long long)cleaner->dev_id);
	} else {
		ret = snprintf(nambuf, sizeof(nambuf),
			       "/nilfs-cleanerq-%llu-%llu",
			       (unsigned long long)cleaner->dev_id,
			       (unsigned long long)cleaner->dev_ino);
	}
	if (ret < 0)
		goto error;

	assert(ret < sizeof(nambuf));

	cleaner->sendq = mq_open(nambuf, O_WRONLY);
	if (cleaner->sendq < 0) {
		if (errno == ENOENT) {
			nilfs_cleaner_logger(LOG_NOTICE,
					     _("No cleaner found on %s."),
					     cleaner->device);
		} else {
			nilfs_cleaner_logger(
				LOG_ERR,
				_("Error: cannot open cleaner on %s: %s."),
				cleaner->device, strerror(errno));
		}
		goto abort;
	}

	return 0;

error:
	nilfs_cleaner_logger(LOG_ERR,
			     _("Error: fatal error during queue setting: %s."),
			     strerror(errno));
abort:
	if (cleaner->recvq >= 0) {
		mq_close(cleaner->recvq);
		cleaner->recvq = -1;
		mq_unlink(cleaner->recvq_name);
		free(cleaner->recvq_name);
	}
	return -1;
}

static void nilfs_cleaner_close_queue(struct nilfs_cleaner *cleaner)
{
	if (cleaner->recvq >= 0) {
		mq_close(cleaner->recvq);
		mq_unlink(cleaner->recvq_name);
		free(cleaner->recvq_name);
		cleaner->recvq = -1;
		cleaner->recvq_name = NULL;
	}
	if (cleaner->sendq >= 0) {
		mq_close(cleaner->sendq);
		cleaner->sendq = -1;
	}
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
	cleaner->sendq = -1;
	cleaner->recvq = -1;

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
	cleaner->sendq = -1;
	cleaner->recvq = -1;

	if (nilfs_cleaner_find_fs(cleaner, device, mntdir) < 0)
		goto abort;

	if (nilfs_cleaner_get_device_id(cleaner) < 0)
		goto abort;

	if ((oflag & NILFS_CLEANER_OPEN_GCPID) && cleaner->cleanerd_pid == 0) {
		nilfs_cleaner_logger(LOG_ERR,
				     _("Error: cannot get cleanerd pid"));
		goto abort;
	}

	if ((oflag & NILFS_CLEANER_OPEN_QUEUE) &&
	    nilfs_cleaner_open_queue(cleaner) < 0)
		goto abort;

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
	nilfs_cleaner_close_queue(cleaner);
	free(cleaner->device);
	free(cleaner->mountdir);
	free(cleaner);
}

static int nilfs_cleaner_clear_queueu(struct nilfs_cleaner *cleaner)
{
	struct nilfs_cleaner_response res;
	struct mq_attr attr;
	unsigned count;

	assert(cleaner->recvq >= 0);

	if (mq_getattr(cleaner->recvq, &attr) < 0)
		goto failed;

	while (attr.mq_curmsgs > 0) {
		count = attr.mq_curmsgs;
		do {
			if (mq_receive(cleaner->recvq, (char *)&res,
				       sizeof(res), NULL) < 0)
				goto failed;
		} while (--count > 0);

		if (mq_getattr(cleaner->recvq, &attr) < 0)
			goto failed;
	}
	return 0;

failed:
	nilfs_cleaner_logger(LOG_ERR,
			     _("Error: cannot clear message queue: %s"),
			     strerror(errno));
	return -1;
}

static int nilfs_cleaner_command(struct nilfs_cleaner *cleaner, int cmd)
{
	struct nilfs_cleaner_request req;
	struct nilfs_cleaner_response res;
	int bytes, ret = -1;

	if (cleaner->sendq < 0 || cleaner->recvq < 0) {
		errno = EBADF;
		goto out;
	}
	if (nilfs_cleaner_clear_queueu(cleaner) < 0)
		goto out;

	req.cmd = cmd;
	req.argsize = 0;
	uuid_copy(req.client_uuid, cleaner->client_uuid);

	ret = mq_send(cleaner->sendq, (char *)&req, sizeof(req),
		      NILFS_CLEANER_PRIO_NORMAL);
	if (ret < 0)
		goto out;

	bytes = mq_receive(cleaner->recvq, (char *)&res, sizeof(res), NULL);
	if (bytes < sizeof(res)) {
		if (bytes >= 0)
			errno = EIO;
		ret = -1;
		goto out;
	}
	if (res.result == NILFS_CLEANER_RSP_NACK) {
		ret = -1;
		errno = res.err;
	}
out:
	return ret;
}

int nilfs_cleaner_get_status(struct nilfs_cleaner *cleaner, int *status)
{
	struct nilfs_cleaner_request req;
	struct nilfs_cleaner_response res;
	int bytes, ret = -1;

	if (cleaner->sendq < 0 || cleaner->recvq < 0) {
		errno = EBADF;
		goto out;
	}
	if (nilfs_cleaner_clear_queueu(cleaner) < 0)
		goto out;

	req.cmd = NILFS_CLEANER_CMD_GET_STATUS;
	req.argsize = 0;
	uuid_copy(req.client_uuid, cleaner->client_uuid);

	ret = mq_send(cleaner->sendq, (char *)&req, sizeof(req),
		      NILFS_CLEANER_PRIO_NORMAL);
	if (ret < 0)
		goto out;

	bytes = mq_receive(cleaner->recvq, (char *)&res, sizeof(res), NULL);
	if (bytes < sizeof(res)) {
		if (bytes >= 0)
			errno = EIO;
		ret = -1;
		goto out;
	}
	if (res.result == NILFS_CLEANER_RSP_ACK) {
		*status = res.status;
	} else if (res.result == NILFS_CLEANER_RSP_NACK) {
		ret = -1;
		errno = res.err;
	}
out:
	return ret;
}

int nilfs_cleaner_run(struct nilfs_cleaner *cleaner,
		      const struct nilfs_cleaner_args *args,
		      uint32_t *jobid)
{
	struct nilfs_cleaner_request_with_args req;
	struct nilfs_cleaner_response res;
	int bytes, ret = -1;

	if (cleaner->sendq < 0 || cleaner->recvq < 0) {
		errno = EBADF;
		goto out;
	}
	if (nilfs_cleaner_clear_queueu(cleaner) < 0)
		goto out;

	req.hdr.cmd = NILFS_CLEANER_CMD_RUN;
	req.hdr.argsize = sizeof(req.args);
	uuid_copy(req.hdr.client_uuid, cleaner->client_uuid);
	memcpy(&req.args, args, sizeof(req.args));

	ret = mq_send(cleaner->sendq, (char *)&req, sizeof(req),
		      NILFS_CLEANER_PRIO_NORMAL);
	if (ret < 0)
		goto out;

	bytes = mq_receive(cleaner->recvq, (char *)&res, sizeof(res), NULL);
	if (bytes < sizeof(res)) {
		if (bytes >= 0)
			errno = EIO;
		ret = -1;
		goto out;
	}
	if (res.result == NILFS_CLEANER_RSP_ACK) {
		if (jobid)
			*jobid = res.jobid;
	} else if (res.result == NILFS_CLEANER_RSP_NACK) {
		ret = -1;
		errno = res.err;
	}
out:
	return ret;
}

int nilfs_cleaner_suspend(struct nilfs_cleaner *cleaner)
{
	return nilfs_cleaner_command(cleaner, NILFS_CLEANER_CMD_SUSPEND);
}

int nilfs_cleaner_resume(struct nilfs_cleaner *cleaner)
{
	return nilfs_cleaner_command(cleaner, NILFS_CLEANER_CMD_RESUME);
}

int nilfs_cleaner_tune(struct nilfs_cleaner *cleaner,
		       const struct nilfs_cleaner_args *args)
{
	struct nilfs_cleaner_request_with_args req;
	struct nilfs_cleaner_response res;
	int bytes, ret = -1;

	if (cleaner->sendq < 0 || cleaner->recvq < 0) {
		errno = EBADF;
		goto out;
	}
	if (nilfs_cleaner_clear_queueu(cleaner) < 0)
		goto out;

	req.hdr.cmd = NILFS_CLEANER_CMD_TUNE;
	req.hdr.argsize = sizeof(req.args);
	uuid_copy(req.hdr.client_uuid, cleaner->client_uuid);
	memcpy(&req.args, args, sizeof(req.args));

	ret = mq_send(cleaner->sendq, (char *)&req, sizeof(req),
		      NILFS_CLEANER_PRIO_NORMAL);
	if (ret < 0)
		goto out;

	bytes = mq_receive(cleaner->recvq, (char *)&res, sizeof(res), NULL);
	if (bytes < sizeof(res)) {
		if (bytes >= 0)
			errno = EIO;
		ret = -1;
		goto out;
	}
	if (res.result == NILFS_CLEANER_RSP_NACK) {
		ret = -1;
		errno = res.err;
	}
out:
	return ret;
}

int nilfs_cleaner_reload(struct nilfs_cleaner *cleaner, const char *conffile)
{
	struct nilfs_cleaner_request_with_path req;
	struct nilfs_cleaner_response res;
	size_t pathlen, reqsz;
	int bytes, ret = -1;

	if (cleaner->sendq < 0 || cleaner->recvq < 0) {
		errno = EBADF;
		goto out;
	}
	if (nilfs_cleaner_clear_queueu(cleaner) < 0)
		goto out;

	if (conffile) {
		if (myrealpath(conffile, req.pathname,
			       NILFS_CLEANER_MSG_MAX_PATH) == NULL)
			goto out;

		pathlen = strlen(req.pathname);
		req.hdr.argsize = pathlen + 1;
		reqsz = sizeof(req.hdr) + pathlen + 1;
	} else {
		req.hdr.argsize = 0;
		reqsz = sizeof(req.hdr);
	}
	req.hdr.cmd = NILFS_CLEANER_CMD_RELOAD;
	uuid_copy(req.hdr.client_uuid, cleaner->client_uuid);

	ret = mq_send(cleaner->sendq, (char *)&req, reqsz,
		      NILFS_CLEANER_PRIO_NORMAL);
	if (ret < 0)
		goto out;

	bytes = mq_receive(cleaner->recvq, (char *)&res, sizeof(res), NULL);
	if (bytes < sizeof(res)) {
		if (bytes >= 0)
			errno = EIO;
		ret = -1;
		goto out;
	}
	if (res.result == NILFS_CLEANER_RSP_NACK) {
		ret = -1;
		errno = res.err;
	}
out:
	return ret;
}

static int nilfs_cleaner_wait_common(struct nilfs_cleaner *cleaner,
				     uint32_t jobid)
{
	struct nilfs_cleaner_request_with_jobid req;
	int ret;

	if (cleaner->sendq < 0 || cleaner->recvq < 0) {
		ret = -1;
		errno = EBADF;
		goto out;
	}

	ret = nilfs_cleaner_clear_queueu(cleaner);
	if (ret < 0)
		goto out;

	req.hdr.cmd = NILFS_CLEANER_CMD_WAIT;
	req.hdr.argsize = 0;
	uuid_copy(req.hdr.client_uuid, cleaner->client_uuid);
	req.jobid = jobid;

	ret = mq_send(cleaner->sendq, (char *)&req, sizeof(req),
		      NILFS_CLEANER_PRIO_NORMAL);
out:
	return ret;
}

int nilfs_cleaner_wait(struct nilfs_cleaner *cleaner, uint32_t jobid,
		       const struct timespec *abs_timeout)
{
	struct nilfs_cleaner_response res;
	int bytes, ret;

	ret = nilfs_cleaner_wait_common(cleaner, jobid);
	if (ret < 0)
		goto out;

	bytes = mq_timedreceive(cleaner->recvq, (char *)&res, sizeof(res),
				NULL, abs_timeout);
	if (bytes < sizeof(res)) {
		if (bytes >= 0)
			errno = EIO;
		ret = -1;
		goto out;
	}
	if (res.result == NILFS_CLEANER_RSP_NACK) {
		ret = -1;
		errno = res.err;
	}
out:
	return ret;
}

int nilfs_cleaner_wait_r(struct nilfs_cleaner *cleaner, uint32_t jobid,
			 const struct timespec *timeout)
{
	struct nilfs_cleaner_response res;
	struct pollfd pfd;
	int bytes, ret;

	ret = nilfs_cleaner_wait_common(cleaner, jobid);
	if (ret < 0)
		goto out;

	memset(&pfd, 0, sizeof(0));
	pfd.fd = cleaner->recvq;
	pfd.events = POLLIN;

	ret = ppoll(&pfd, 1, timeout, NULL);
	if (ret < 0)
		goto out;

	if (!(pfd.revents & POLLIN)) {
		ret = -1;
		errno = ETIMEDOUT;
		goto out;
	}

	bytes = mq_receive(cleaner->recvq, (char *)&res, sizeof(res), NULL);
	if (bytes < sizeof(res)) {
		if (bytes >= 0)
			errno = EIO;
		ret = -1;
		goto out;
	}
	if (res.result == NILFS_CLEANER_RSP_NACK) {
		ret = -1;
		errno = res.err;
	}
out:
	return ret;
}

int nilfs_cleaner_stop(struct nilfs_cleaner *cleaner)
{
	return nilfs_cleaner_command(cleaner, NILFS_CLEANER_CMD_STOP);
}

int nilfs_cleaner_shutdown(struct nilfs_cleaner *cleaner)
{
	return nilfs_cleaner_command(cleaner, NILFS_CLEANER_CMD_SHUTDOWN);
}
