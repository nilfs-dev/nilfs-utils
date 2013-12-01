/*
 * mount_attrs.c - NILFS mount attribute functions for libmount version
 *
 * Copyright (C) 2011-2012 Nippon Telegraph and Telephone Corporation.
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

#if HAVE_STRING_H
#include <string.h>
#endif	/* HAVE_STRING_H */

#if HAVE_LIBMOUNT_LIBMOUNT_H
#include <libmount/libmount.h>
#endif	/* HAVE_LIBMOUNT_H */

#include <stdarg.h>
#include <errno.h>
#include <assert.h>

#include "sundries.h"
#include "mount.nilfs2.h"
#include "mount_attrs.h"
#include "cleaner_exec.h"	/* PIDOPT_NAME */
#include "nls.h"

extern char *progname;

void nilfs_mount_attrs_init(struct nilfs_mount_attrs *mattrs)
{
	memset(mattrs, 0, sizeof(*mattrs));
	mattrs->pp = ULONG_MAX; /* no protection period */
}

int nilfs_mount_attrs_parse(struct nilfs_mount_attrs *mattrs,
			    const char *optstr, char **found, char **rest,
			    int mtab)
{
	char *name, *val, *str, *p, *endptr, *prev = NULL;
	size_t namesz, valsz;
	int res;

	if (rest)
		*rest = NULL;
	if (found)
		*found = NULL;

	str = strdup(optstr);
	if (!str)
		return -1;

	p = str;
	while (!mnt_optstr_next_option(&p, &name, &namesz, &val, &valsz)) {
		if (!strncmp(name, PPOPT_NAME, namesz)) {
			if (!val || valsz == 0)
				goto out_inval;

			mattrs->pp = strtoul(val, &endptr, 10);
			if (endptr != val + valsz)
				goto out_inval;

		} else if (!strncmp(name, NOGCOPT_NAME, namesz)) {
			if (val)
				goto out_inval;

			mattrs->nogc = 1;

		} else if (!strncmp(name, PIDOPT_NAME, namesz)) {
			if (!val || valsz == 0 || !mtab)
				goto out_inval;

			mattrs->gcpid = strtoul(val, &endptr, 10);
			if (endptr != val + valsz)
				goto out_inval;

		} else if (!strncmp(name, NOATTR_NAME, namesz)) {
			if (val || !mtab)
				goto out_inval;
			/* ignore */
		} else {
			if (!prev)
				prev = name;
			continue;
		}

		if (prev && rest) {
			*(name - 1) = '\0';
			res = mnt_optstr_append_option(rest, prev, NULL);
			if (res < 0)
				goto failed;
			prev = NULL;
		}
		if (found) {
			name[namesz] = '\0';
			if (val)
				val[valsz] = '\0';
			res = mnt_optstr_append_option(found, name, val);
			if (res < 0)
				goto failed;
		}
	}

	if (prev && rest) {
		res = mnt_optstr_append_option(rest, prev, NULL);
		if (res < 0)
			goto failed;
	}

	free(str);
	return 0;

out_inval:
	error(_("%s: invalid options (%s)."), progname, optstr);
	res = -1;
failed:
	if (rest) {
		free(*rest);
		*rest = NULL;
	}
	if (found) {
		free(*found);
		*found = NULL;
	}

	free(str);

	return res;
}

void nilfs_mount_attrs_update(struct nilfs_mount_attrs *old_attrs,
			      struct nilfs_mount_attrs *new_attrs,
			      struct libmnt_context *cxt)
{
	struct libmnt_fs *fs;

	if (!new_attrs && !old_attrs)
		return;

	fs = mnt_context_get_fs(cxt);
	mnt_fs_set_attributes(fs, NULL);
	if (!new_attrs)
		return;

	if (new_attrs->nogc) {
		mnt_fs_append_attributes(fs, NOGCOPT_NAME);
	} else if (new_attrs->gcpid) {
		char abuf[100];

		snprintf(abuf, sizeof(abuf), PIDOPT_NAME "=%lu",
			 (unsigned long)new_attrs->gcpid);
		mnt_fs_append_attributes(fs, abuf);
		if (new_attrs->pp != ULONG_MAX) {
			snprintf(abuf, sizeof(abuf), PPOPT_NAME "=%lu",
				 new_attrs->pp);
			mnt_fs_append_attributes(fs, abuf);
		}
	} else if (old_attrs) {
		/*
		 * The following dummy attribute is required to handle
		 * removal of attributes on remount.
		 */
		mnt_fs_set_attributes(fs, NOATTR_NAME);
	}
}
