/*
 * mount_ctl.c - NILFS cleanerd control routine
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

#include <stdarg.h>

#if HAVE_STRING_H
#include <string.h>
#endif	/* HAVE_STRING_H */

#if HAVE_SYS_MOUNT_H
#include <sys/mount.h>
#endif	/* HAVE_SYS_MOUNT_H */

#if HAVE_SYS_STAT_H
#include <sys/stat.h>
#endif	/* HAVE_SYS_STAT_H */

#if HAVE_SYS_WAIT_H
#include <sys/wait.h>
#endif	/* HAVE_SYS_WAIT_H */

#include <signal.h>
#include <errno.h>

#include "sundries.h"
#include "mount.nilfs2.h"
#include "nls.h"


const char cleanerd[] = "/sbin/" CLEANERD_NAME;
const char cleanerd_nofork_opt[] = "-n";
const char cleanerd_protperiod_opt[] = "-p";

extern char *progname;


static inline int process_is_alive(pid_t pid)
{
	return (kill(pid, 0) == 0);
}

int start_cleanerd(const char *device, const char *mntdir,
		   unsigned long protperiod, pid_t *ppid)
{
	const char *dargs[7];
	struct stat statbuf;
	int i = 0;
	int res;
	char buf[256];

	if (stat(cleanerd, &statbuf) != 0) {
		error(_("Warning: %s not found"), CLEANERD_NAME);
		return -1;
	}

	res = fork();
	if (res == 0) {
		if (setgid(getgid()) < 0) {
			error(_("%s: failed to drop setgid privileges"),
			      progname);
			exit(1);
		}
		if (setuid(getuid()) < 0) {
			error(_("%s: failed to drop setuid privileges"),
			      progname);
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
		block_signals(SIG_UNBLOCK);
		execv(cleanerd, (char **)dargs);
		exit(1);   /* reach only if failed */
	} else if (res != -1) {
		*ppid = res;
		return 0; /* cleanerd started */
	} else {
		int errsv = errno;
		error(_("%s: Could not fork: %s"), progname, strerror(errsv));
	}
	return -1;
}

static int wait_cleanerd(pid_t pid, const char *device)
{
	int cnt = CLEANERD_WAIT_RETRY_COUNT;
	int res;

	sleep(0);
	if (!process_is_alive(pid))
		return 0;
	sleep(1);
	if (!process_is_alive(pid))
		return 0;

	printf(_("%s: cleanerd (pid=%ld) still exists on %s. waiting."),
	       progname, (long)pid, device);
	fflush(stdout);

	for (;;) {
		if (cnt-- < 0) {
			printf(_("failed\n"));
			fflush(stdout);
			res = -1; /* wait failed */
			break;
		}
		sleep(CLEANERD_WAIT_RETRY_INTERVAL);
		if (!process_is_alive(pid)) {
			printf(_("done\n"));
			fflush(stdout);
			res = 0;
			break;
		}
		putchar('.');
		fflush(stdout);
	}
	return res;
}

int stop_cleanerd(const char *spec, pid_t pid)
{
	int res;

	if (verbose)
		printf(_("%s: kill cleanerd (pid=%ld) on %s\n"),
		       progname, (long)pid, spec);

	if (kill(pid, SIGTERM) < 0) {
		int errsv = errno;
		if (errsv == ESRCH)
			return 0;
		else
			die(EX_USAGE, "%s: cannot kill cleanerd: %s",
			    progname, strerror(errsv));
	}
	res = wait_cleanerd(pid, spec);
	if (verbose) {
		if (res < 0)
			error("%s: wait timeout", progname);
		else
			printf(_("%s: cleanerd (pid=%ld) stopped\n"),
			       progname, (long)pid);
	}
	return res;
}

int check_cleanerd(const char *spec, pid_t pid)
{
	return process_is_alive(pid);
}
