/*
 * cleaner_exec.c - old cleaner control routines
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
#include <sys/time.h>
#endif	/* HAVE_SYS_TIME_H */

#if HAVE_TIME_H
#include <time.h>
#endif	/* HAVE_TIME_H */

#if HAVE_SYS_WAIT_H
#include <sys/wait.h>
#endif	/* HAVE_SYS_WAIT_H */

#if HAVE_SYSLOG_H
#include <syslog.h>
#endif	/* HAVE_SYSLOG_H */

#include <signal.h>
#include <stdarg.h>
#include <errno.h>
#include <assert.h>

#include "cleaner_exec.h"
#include "compat.h"
#include "nls.h"

/* Intervals for binary exponential backoff */
#define WAIT_CLEANERD_MIN_BACKOFF_TIME	5000	/* in micro-seconds */
#define WAIT_CLEANERD_MAX_BACKOFF_TIME	2	/* in seconds */

/* Intervals for periodic wait retry */
#define WAIT_CLEANERD_RETRY_INTERVAL	2	/* in seconds */
#define WAIT_CLEANERD_RETRY_TIMEOUT	8	/* in seconds */

static const char cleanerd[] = "/sbin/" NILFS_CLEANERD_NAME;
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

static int receive_pid(int fd, pid_t *ppid)
{
	char buf[100];
	unsigned long pid;
	FILE *fp;
	int ret;

	fp = fdopen(fd, "r");
	if (!fp) {
		nilfs_cleaner_logger(
			LOG_ERR, _("Error: fdopen failed: %m"));
		close(fd);
		goto fail;
	}

	/* read process ID of cleanerd through the given fd */
	while (fgets(buf, sizeof(buf), fp) != NULL) {
		/*
		 * fgets() is blocked during no child processes
		 * respond, but it will escape returning a NULL value
		 * and terminate the loop when all child processes
		 * close the given pipe (fd) including call of exit().
		 */
		ret = sscanf(buf, "NILFS_CLEANERD_PID=%lu", &pid);
		if (ret == 1) {
			*ppid = pid;
			fclose(fp); /* this also closes fd */
			return 0;
		}
	}
	fclose(fp);
fail:
	nilfs_cleaner_logger(
		LOG_WARNING, _("Warning: cannot get pid of cleanerd"));
	return -1;
}

int nilfs_launch_cleanerd(const char *device, const char *mntdir,
			  unsigned long protperiod, pid_t *ppid)
{
	const char *dargs[6];
	struct stat statbuf;
	sigset_t sigs;
	int i = 0;
	int ret;
	char buf[256];
	int pipes[2];

	if (stat(cleanerd, &statbuf) != 0) {
		nilfs_cleaner_logger(LOG_ERR,  _("Error: %s not found"),
				     NILFS_CLEANERD_NAME);
		return -1;
	}

	ret = pipe(pipes);
	if (ret < 0) {
		nilfs_cleaner_logger(
			LOG_ERR, _("Error: failed to create pipe: %m"));
		return -1;
	}

	ret = fork();
	if (ret == 0) {
		/* child */
		if (setgid(getgid()) < 0) {
			nilfs_cleaner_logger(
				LOG_ERR,
				_("Error: failed to drop setgid privileges"));
			nilfs_cleaner_flush();
			_exit(EXIT_FAILURE);
		}
		if (setuid(getuid()) < 0) {
			nilfs_cleaner_logger(
				LOG_ERR,
				_("Error: failed to drop setuid privileges"));
			nilfs_cleaner_flush();
			_exit(EXIT_FAILURE);
		}
		dargs[i++] = cleanerd;
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

		close(pipes[0]);
		ret = dup2(pipes[1], STDOUT_FILENO);
		if (ret < 0) {
			nilfs_cleaner_logger(
				LOG_ERR,
				_("Error: failed to duplicate pipe: %m"));
			nilfs_cleaner_flush();
			_exit(EXIT_FAILURE);
		}
		close(pipes[1]);

		execv(cleanerd, (char **)dargs);
		_exit(EXIT_FAILURE);   /* reach only if failed */
	} else if (ret > 0) {
		/* parent */
		close(pipes[1]);

		/*
		 * fork() returns a pid of the child process, but we
		 * cannot use it because cleanerd internally fork()s
		 * and changes pid.
		 */
		ret = receive_pid(pipes[0], ppid);
		if (ret < 0)
			*ppid = 0;
		/*
		 * always return a success code because cleanerd has
		 * already started.
		 */
		return 0;
	}

	nilfs_cleaner_logger(LOG_ERR, _("Error: could not fork: %m"));
	close(pipes[0]);
	close(pipes[1]);
	return -1;
}

int nilfs_ping_cleanerd(pid_t pid)
{
	return process_is_alive(pid);
}

static void recalc_backoff_time(struct timespec *interval)
{
	/* binary exponential backoff */
	interval->tv_sec <<= 1;
	interval->tv_nsec <<= 1;
	if (interval->tv_nsec >= 1000000000) {
		ldiv_t q = ldiv(interval->tv_nsec, 1000000000);

		interval->tv_sec += q.quot;
		interval->tv_nsec = q.rem;
	}
}

static int nilfs_wait_cleanerd(const char *device, pid_t pid)
{
	struct timespec waittime;
	struct timespec start, end, now;
	int ret;

	if (!process_is_alive(pid))
		return 0;

	ret = clock_gettime(CLOCK_MONOTONIC, &start);
	if (ret < 0) {
		nilfs_cleaner_logger(LOG_ERR,
				     _("failed to get monotonic clock: %s"),
				     strerror(errno));
		return -1;
	}

	waittime.tv_sec = 0;
	waittime.tv_nsec = WAIT_CLEANERD_MIN_BACKOFF_TIME * 1000;
	end.tv_sec = start.tv_sec + WAIT_CLEANERD_MAX_BACKOFF_TIME;
	end.tv_nsec = start.tv_nsec;

	for (;;) {
		ret = clock_nanosleep(CLOCK_MONOTONIC, 0, &waittime, NULL);
		if (ret < 0 && errno == EINTR)
			return -1;

		if (!process_is_alive(pid))
			return 0;

		ret = clock_gettime(CLOCK_MONOTONIC, &now);
		if (ret < 0 || !timespeccmp(&now, &end, <))
			break;

		recalc_backoff_time(&waittime);
	}

	nilfs_cleaner_printf(_("cleanerd (pid=%ld) still exists on %s. waiting."),
			     (long)pid, device);
	nilfs_cleaner_flush();

	waittime.tv_sec = WAIT_CLEANERD_RETRY_INTERVAL;
	waittime.tv_nsec = 0;
	end.tv_sec = start.tv_sec + WAIT_CLEANERD_RETRY_TIMEOUT;
	end.tv_nsec = start.tv_nsec;

	for (;;) {
		ret = clock_gettime(CLOCK_MONOTONIC, &now);
		if (ret < 0 || !timespeccmp(&now, &end, <))
			break;

		ret = clock_nanosleep(CLOCK_MONOTONIC, 0, &waittime, NULL);
		if (ret < 0 && errno == EINTR) {
			nilfs_cleaner_printf(_("interrupted\n"));
			nilfs_cleaner_flush();
			return -1;
		}

		if (!process_is_alive(pid)) {
			nilfs_cleaner_printf(_("done\n"));
			nilfs_cleaner_flush();
			return 0;
		}
		nilfs_cleaner_printf(_("."));
		nilfs_cleaner_flush();
	}
	nilfs_cleaner_printf(_("failed\n"));
	nilfs_cleaner_flush();
	return -1; /* wait failed */
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
		nilfs_cleaner_logger(LOG_INFO,
				     _("cleanerd (pid=%ld) stopped"),
				     pid);
out:
	return ret;
}
