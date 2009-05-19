/*
 * cleanerd.h - NILFS cleaner daemon.
 *
 * Copyright (C) 2007 Nippon Telegraph and Telephone Corporation.
 *
 * This file is part of NILFS.
 *
 * NILFS is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * NILFS is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with NILFS; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * Written by Koji Sato <koji@osrg.net>.
 */

#ifndef CLEANERD_H
#define CLEANERD_H

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include "nilfs.h"
#include "cldconfig.h"


/**
 * struct nilfs_cleanerd - nilfs cleaner daemon
 * @c_nilfs: nilfs
 * @c_config: config structure
 * @c_conffile: configuration file name
 * @c_running: running state
 * @c_fallback: fallback state
 * @c_ncleansegs: number of semgents cleaned per cycle
 * @c_protcno: the minimum of checkpoint numbers within protection period
 * @c_prottime: start time of protection period
 * @c_target: target time for sleeping
 */
struct nilfs_cleanerd {
	struct nilfs *c_nilfs;
	struct nilfs_cldconfig c_config;
	char *c_conffile;
	int c_running;
	int c_fallback;
	int c_ncleansegs;
	nilfs_cno_t c_protcno;
	__u64 c_prottime;
	struct timeval c_target;
};

#ifndef SYSCONFDIR
#define SYSCONFDIR		"/etc"
#endif	/* SYSCONFDIR */
#define NILFS_CLEANERD_CONFFILE	SYSCONFDIR "/nilfs_cleanerd.conf"

/**
 * struct nilfs_segimp - segment importance
 * @si_segnum: segment number
 * @si_importance: importance of segment
 */
struct nilfs_segimp {
	__u64 si_segnum;
	unsigned long long si_importance;
};

#endif	/* CLEANERD_H */
