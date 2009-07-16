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
#include <sys/wait.h>
#include <signal.h>
#include <errno.h>

#include "sundries.h"
#include "mount.nilfs2.h"
#include "nls.h"


const char cleanerd[] = "/sbin/" CLEANERD_NAME;
const char cleanerd_nofork_opt[] = "-n";

extern char *progname;


static inline int process_is_alive(pid_t pid)
{
	return (kill(pid, 0) == 0);
}

int start_cleanerd(const char *device, const char *mntdir, pid_t *ppid)
{
	const char *dargs[5];
	struct stat statbuf;
	int i = 0;
	int res;

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
