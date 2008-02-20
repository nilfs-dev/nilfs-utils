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

#include <stdio.h>
//#include <stdlib.h>
#include <string.h>
#include <ctype.h>		/* for isdigit */
//#include <sys/mount.h>

#include <pwd.h>
#include <grp.h>

#include "sundries.h"
#include "mount_constants.h"
#include "mount_opts.h"
#include "nls.h"


#ifdef MS_SILENT
extern int verbose, mount_quiet;
#endif

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
	{ NULL,	0, 0, 0		}
};

static const char *opt_loopdev, *opt_vfstype, *opt_offset, *opt_encryption,
	*opt_speed, *opt_comment;

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

/*
 * Look for OPT in opt_map table and return mask value.
 * If OPT isn't found, tack it onto extra_opts (which is non-NULL).
 * For the options uid= and gid= replace user or group name by its value.
 */
static inline void
parse_opt(const char *opt, int *mask, char *extra_opts, int len)
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

	len -= strlen(extra_opts);

	if (*extra_opts && --len > 0)
		strcat(extra_opts, ",");

	/* convert nonnumeric ids to numeric */
	if (!strncmp(opt, "uid=", 4) && !isdigit(opt[4])) {
		struct passwd *pw = getpwnam(opt+4);
		char uidbuf[20];

		if (pw) {
			sprintf(uidbuf, "uid=%d", pw->pw_uid);
			if ((len -= strlen(uidbuf)) > 0)
				strcat(extra_opts, uidbuf);
			return;
		}
	}
	if (!strncmp(opt, "gid=", 4) && !isdigit(opt[4])) {
		struct group *gr = getgrnam(opt+4);
		char gidbuf[20];

		if (gr) {
			sprintf(gidbuf, "gid=%d", gr->gr_gid);
			if ((len -= strlen(gidbuf)) > 0)
				strcat(extra_opts, gidbuf);
			return;
		}
	}

	if ((len -= strlen(opt)) > 0)
		strcat(extra_opts, opt);
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
		int len = strlen(opts) + 20;

		*extra_opts = xmalloc(len); 
		**extra_opts = '\0';

		for (opt = strtok(opts, ","); opt; opt = strtok(NULL, ","))
			if (!parse_string_opt(opt))
				parse_opt(opt, flags, *extra_opts, len);

		free(opts);
	}

#if 0  /* XXX: should make them external variables? */
	if (readonly)
		*flags |= MS_RDONLY;
	if (readwrite)
		*flags &= ~MS_RDONLY;
	*flags |= mounttype;
#endif
}

/* Try to build a canonical options string.  */
char *fix_opts_string(int flags, const char *extra_opts, const char *user)
{
	const struct opt_map *om;
	const struct string_opt_map *m;
	char *new_opts;

	new_opts = xstrdup((flags & MS_RDONLY) ? "ro" : "rw");
	for (om = opt_map; om->opt != NULL; om++) {
		if (om->skip)
			continue;
		if (om->inv || !om->mask || (flags & om->mask) != om->mask)
			continue;
		new_opts = xstrconcat3(new_opts, ",", om->opt);
		flags &= ~om->mask;
	}
	for (m = &string_opt_map[0]; m->tag; m++) {
		if (!m->skip && *(m->valptr))
			new_opts = xstrconcat4(new_opts, ",",
					       m->tag, *(m->valptr));
	}
	if (extra_opts && *extra_opts) {
		new_opts = xstrconcat3(new_opts, ",", extra_opts);
	}
	if (user) {
		new_opts = xstrconcat3(new_opts, ",user=", user);
	}
	return new_opts;
}

/* Local Variables:		*/
/* eval: (c-set-style "linux")	*/
/* End:				*/
