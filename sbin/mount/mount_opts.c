/*
 * mount_opts.c - Parse options for mount program
 *
 * This code has been extracted from util-linux-2.12r/mount/mount.c
 */
/*
 * A mount(8) for Linux 0.99.
 *
 * Modifications by many people. Distributed under GPL.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif	/* HAVE_CONFIG_H */

#include <stdio.h>
#include <stdarg.h>

#if HAVE_STRING_H
#include <string.h>
#endif	/* HAVE_STRING_H */

#include <ctype.h>		/* for isdigit */

#include <pwd.h>
#include <grp.h>

#include "sundries.h"
#include "xmalloc.h"
#include "mount_constants.h"
#include "mount_opts.h"
#include "nls.h"


#ifdef MS_SILENT
extern int verbose, mount_quiet;
#endif
extern int readonly, readwrite;

/* Map from -o and fstab option strings to the flag argument to mount(2).  */
struct opt_map {
	const char *opt;		/* option name */
	int  skip;			/* skip in mtab option string */
	int  inv;			/* true if flag value should be inverted */
	int  mask;			/* flag mask value */
};

static const struct opt_map opt_map[] = {
	{ "defaults",	0, 0, 0		},	/* default options */
	{ "ro",	1, 0, MS_RDONLY	},	/* read-only */
	{ "rw",	1, 1, MS_RDONLY	},	/* read-write */
	{ "exec",	0, 1, MS_NOEXEC	},	/* permit execution of binaries */
	{ "noexec",	0, 0, MS_NOEXEC	},	/* don't execute binaries */
	{ "suid",	0, 1, MS_NOSUID	},	/* honor suid executables */
	{ "nosuid",	0, 0, MS_NOSUID	},	/* don't honor suid executables */
	{ "dev",	0, 1, MS_NODEV	},	/* interpret device files  */
	{ "nodev",	0, 0, MS_NODEV	},	/* don't interpret devices */
	{ "sync",	0, 0, MS_SYNCHRONOUS},	/* synchronous I/O */
	{ "async",	0, 1, MS_SYNCHRONOUS},	/* asynchronous I/O */
	{ "dirsync",	0, 0, MS_DIRSYNC},	/* synchronous directory modifications */
	{ "remount",  0, 0, MS_REMOUNT},      /* Alter flags of mounted FS */
	{ "bind",	0, 0, MS_BIND   },	/* Remount part of tree elsewhere */
	{ "rbind",	0, 0, MS_BIND|MS_REC }, /* Idem, plus mounted subtrees */
	{ "auto",	0, 1, MS_NOAUTO	},	/* Can be mounted using -a */
	{ "noauto",	0, 0, MS_NOAUTO	},	/* Can  only be mounted explicitly */
	{ "users",	0, 0, MS_USERS	},	/* Allow ordinary user to mount */
	{ "nousers",	0, 1, MS_USERS	},	/* Forbid ordinary user to mount */
	{ "user",	0, 0, MS_USER	},	/* Allow ordinary user to mount */
	{ "nouser",	0, 1, MS_USER	},	/* Forbid ordinary user to mount */
	{ "owner",	0, 0, MS_OWNER  },	/* Let the owner of the device mount */
	{ "noowner",	0, 1, MS_OWNER  },	/* Device owner has no special privs */
	{ "group",	0, 0, MS_GROUP  },	/* Let the group of the device mount */
	{ "nogroup",	0, 1, MS_GROUP  },	/* Device group has no special privs */
	{ "_netdev",	0, 0, MS_COMMENT},	/* Device requires network */
	{ "comment",	0, 0, MS_COMMENT},	/* fstab comment only (kudzu,_netdev)*/

	/* add new options here */
#ifdef MS_NOSUB
	{ "sub",	0, 1, MS_NOSUB	},	/* allow submounts */
	{ "nosub",	0, 0, MS_NOSUB	},	/* don't allow submounts */
#endif
#ifdef MS_SILENT
	{ "quiet",	0, 0, MS_SILENT    },	/* be quiet  */
	{ "loud",	0, 1, MS_SILENT    },	/* print out messages. */
#endif
#ifdef MS_MANDLOCK
	{ "mand",	0, 0, MS_MANDLOCK },	/* Allow mandatory locks on this FS */
	{ "nomand",	0, 1, MS_MANDLOCK },	/* Forbid mandatory locks on this FS */
#endif
	{ "loop",	1, 0, MS_LOOP	},	/* use a loop device */
#ifdef MS_NOATIME
	{ "atime",	0, 1, MS_NOATIME },	/* Update access time */
	{ "noatime",	0, 0, MS_NOATIME },	/* Do not update access time */
#endif
#ifdef MS_NODIRATIME
	{ "diratime",	0, 1, MS_NODIRATIME },	/* Update dir access times */
	{ "nodiratime", 0, 0, MS_NODIRATIME },	/* Do not update dir access times */
#endif
#ifdef MS_RELATIME
  { "relatime",	0, 0, MS_RELATIME },   /* Update access times relative to
					  mtime/ctime */
  { "norelatime", 0, 1, MS_RELATIME }, /* Update access time without regard
					  to mtime/ctime */
#endif
	{ NULL,	0, 0, 0		}
};

static const char *opt_loopdev, *opt_vfstype, *opt_offset, *opt_encryption,
	*opt_speed, *opt_comment, *opt_uhelper;

static struct string_opt_map {
	char *tag;
	int skip;
	const char **valptr;
} string_opt_map[] = {
	{ "loop=",	0, &opt_loopdev },
	{ "vfs=",	1, &opt_vfstype },
	{ "offset=",	0, &opt_offset },
	{ "encryption=", 0, &opt_encryption },
	{ "speed=", 0, &opt_speed },
	{ "comment=", 1, &opt_comment },
	{ "uhelper=", 0, &opt_uhelper },
	{ NULL, 0, NULL }
};

static void clear_string_opts(void)
{
	struct string_opt_map *m;

	for (m = &string_opt_map[0]; m->tag; m++)
		*(m->valptr) = NULL;
}

static int parse_string_opt(char *s)
{
	struct string_opt_map *m;
	int lth;

	for (m = &string_opt_map[0]; m->tag; m++) {
		lth = strlen(m->tag);
		if (!strncmp(s, m->tag, lth)) {
			*(m->valptr) = xstrdup(s + lth);
			return 1;
		}
	}
	return 0;
}

/* reallocates its first arg */
char *append_opt(char *s, const char *opt, const char *val)
{
	if (!opt)
		return s;
	if (!s) {
		if (!val)
		       return xstrdup(opt);		/* opt */

		return xstrconcat3(NULL, opt, val);	/* opt=val */
	}
	if (!val)
		return xstrconcat3(s, ",", opt);	/* s,opt */

	return xstrconcat4(s, ",", opt, val);		/* s,opt=val */
}

char *append_numopt(char *s, const char *opt, long num)
{
	char buf[32];

	snprintf(buf, sizeof(buf), "%ld", num);
	return append_opt(s, opt, buf);
}

/*
 * Look for OPT in opt_map table and return mask value.
 * If OPT isn't found, tack it onto extra_opts (which is non-NULL).
 * For the options uid= and gid= replace user or group name by its value.
 */
static inline void
parse_opt(const char *opt, int *mask, char **extra_opts)
{
	const struct opt_map *om;

	for (om = opt_map; om->opt != NULL; om++)
		if (streq (opt, om->opt)) {
			if (om->inv)
				*mask &= ~om->mask;
			else
				*mask |= om->mask;
			if ((om->mask == MS_USER || om->mask == MS_USERS)
			    && !om->inv)
				*mask |= MS_SECURE;
			if ((om->mask == MS_OWNER || om->mask == MS_GROUP)
			    && !om->inv)
				*mask |= MS_OWNERSECURE;
#ifdef MS_SILENT
			if (om->mask == MS_SILENT && om->inv)  {
				mount_quiet = 1;
				verbose = 0;
			}
#endif
			return;
		}

	/* convert nonnumeric ids to numeric */
	if (!strncmp(opt, "uid=", 4) && !isdigit(opt[4])) {
		struct passwd *pw = getpwnam(opt+4);

		if (pw) {
			*extra_opts = append_numopt(*extra_opts,
						"uid=", pw->pw_uid);
			return;
		}
	}
	if (!strncmp(opt, "gid=", 4) && !isdigit(opt[4])) {
		struct group *gr = getgrnam(opt+4);

		if (gr) {
			*extra_opts = append_numopt(*extra_opts,
						"gid=", gr->gr_gid);
			return;
		}
	}

	*extra_opts = append_opt(*extra_opts, opt, NULL);
}

/* Take -o options list and compute 4th and 5th args to mount(2).  flags
   gets the standard options (indicated by bits) and extra_opts all the rest */
void parse_opts(const char *options, int *flags, char **extra_opts)
{
	*flags = 0;
	*extra_opts = NULL;

	clear_string_opts();

	if (options != NULL) {
		char *opts = xstrdup(options);
		char *opt;

		for (opt = strtok(opts, ","); opt; opt = strtok(NULL, ","))
			if (!parse_string_opt(opt))
				parse_opt(opt, flags, extra_opts);

		free(opts);
	}

	if (readonly)
		*flags |= MS_RDONLY;
	if (readwrite)
		*flags &= ~MS_RDONLY;
#if 0
	*flags |= mounttype;
#endif
}

/* Try to build a canonical options string.  */
char *fix_opts_string(int flags, const char *extra_opts, const char *user)
{
	const struct opt_map *om;
	const struct string_opt_map *m;
	char *new_opts;

	new_opts = append_opt(NULL, (flags & MS_RDONLY) ? "ro" : "rw", NULL);
	for (om = opt_map; om->opt != NULL; om++) {
		if (om->skip)
			continue;
		if (om->inv || !om->mask || (flags & om->mask) != om->mask)
			continue;
		new_opts = append_opt(new_opts, om->opt, NULL);
		flags &= ~om->mask;
	}
	for (m = &string_opt_map[0]; m->tag; m++) {
		if (!m->skip && *(m->valptr))
			new_opts = append_opt(new_opts, m->tag, *(m->valptr));
	}
	if (extra_opts && *extra_opts)
		new_opts = append_opt(new_opts, extra_opts, NULL);

	if (user)
		new_opts = append_opt(new_opts, "user=", user);

	return new_opts;
}

/*
 * Following part was appended by Ryusuke Konishi <konishi.ryusuke@lab.ntt.co.jp>
 */
int find_opt(const char *opts, const char *token, void *varp)
{
	char *opts2, *opt;
	int res = -1;

	if (!opts)
		return res;

	opts2 = xstrdup(opts);
	opt = strtok(opts2, ",");
	if (varp) {
		while (opt) {
			if (sscanf(opt, token, varp) == 1) {
				res = opt - opts2;
				break;
			}
			opt = strtok(NULL, ",");
		}
	} else {
		int cmplen = strlen(token) + 1;
		while (opt) {
			if (!strncmp(opt, token, cmplen)) {
				res = opt - opts2;
				break;
			}
			opt = strtok(NULL, ",");
		}
	}
	free(opts2);
	return res;
}

char *replace_opt(char *s, const char *fmt, void *varp, const char *instead)
{
	char *ep, *sp;
	size_t oldopt_len, newopt_len, rest_len;
	int ind;

	if (!s || *s == '\0' || (ind = find_opt(s, fmt, varp)) < 0)
		return append_opt(s, instead, NULL);

	if (!instead)
		instead = "";

	/* Scan end of the option */
	ep = sp = s + ind;
	while (*ep != ',' && *ep != '\0')
		ep++;

	if (*instead == '\0') { /* Remove the option */
		if (*ep == ',') {
			/* Get rid of ',' at the end position */
			ep++;
		} else if (ind > 0) {
			/*
			 * (*ep) was a null char - get rid of ','
			 * immediately preceding the start position.
			 */
			ind--; sp--;
		}
	}

	oldopt_len = ep - sp;
	newopt_len = strlen(instead);
	rest_len = strlen(ep);

	if (newopt_len < oldopt_len) {
		/* move remaining options forward */
		memmove(sp + newopt_len, ep, rest_len + 1 /* '\0' */);
	}

	s = xrealloc(s, ind + newopt_len + rest_len + 1 /* '\0' */);
	memcpy(sp, instead, newopt_len);

	if (newopt_len > oldopt_len) {
		/* move remaining options backward */
		memmove(sp + newopt_len, ep, rest_len + 1 /* '\0' */);
	}
	return s;
}

char *replace_optval(char *s, const char *fmt, void *varp, ...)
{
	char buf[128];
	va_list args;
	int len;

	va_start(args, varp);
	len = vsnprintf(buf, sizeof(buf), fmt, args);
	va_end(args);

	if (len < 0 || len >= sizeof(buf))
		die(EX_SOFTWARE, _("%s: too long option"), __func__);

	return replace_opt(s, fmt, varp, buf);
}
