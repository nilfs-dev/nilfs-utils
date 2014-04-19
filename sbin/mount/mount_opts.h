/*
 * mount_opts.h - Parse options for mount program, declarations
 *
 * Code extracted from util-linux-2.12r/mount/mount.c
 *
 * modified by Ryusuke Konishi <konishi.ryusuke@lab.ntt.co.jp>
 */
#ifndef _MOUNT_OPTS_H
#define _MOUNT_OPTS_H


/* Custom mount options for our own purposes.  */
/* Maybe these should now be freed for kernel use again */
#define MS_NOAUTO	0x80000000
#define MS_USERS	0x40000000
#define MS_USER		0x20000000
#define MS_OWNER	0x10000000
#define MS_GROUP	0x08000000
#define MS_PAMCONSOLE	0x04000000
#define MS_NETDEV	0x00040000
#define MS_COMMENT	0x00020000
#define MS_LOOP		0x00010000

/* Options that we keep the mount system call from seeing.  */
#define MS_NOSYS	(MS_NOAUTO|MS_USERS|MS_USER|MS_COMMENT|MS_LOOP|MS_PAMCONSOLE|MS_NETDEV)

/* Options that we keep from appearing in the options field in the mtab.  */
#define MS_NOMTAB	(MS_REMOUNT|MS_NOAUTO|MS_USERS|MS_USER|MS_PAMCONSOLE)

/* Options that we make ordinary users have by default.  */
#define MS_SECURE	(MS_NOEXEC|MS_NOSUID|MS_NODEV)

/* Options that we make owner-mounted devices have by default */
#define MS_OWNERSECURE	(MS_NOSUID|MS_NODEV)


char *append_opt(char *s, const char *opt, const char *val);
char *append_numopt(char *s, const char *opt, long num);
void parse_opts(const char *options, int *flags, char **extra_opts);
char *fix_opts_string(int flags, const char *extra_opts, const char *user);

int find_opt(const char *opts, const char *token, void *varp);
char *replace_opt(char *s, const char *fmt, void *varp, const char *instead);
char *replace_optval(char *s, const char *fmt, void *varp, ...);

/**
 * replace_drop_opt() - replace or drop mount option string
 * @str: comma-separated string
 * @fmt: format string of target option such as "pid=%lu"
 * @oldvalp: pointer to old value
 * @newval: new value of option
 * @cond: condition of mount option manipulation
 *
 * replace_drop_opt() finds out an option string matching @fmt in
 * @str, and replaces value of the option with @newval if @cond is
 * true or otherwise deletes the whole option string from @str.  In
 * both cases, old value of the option is saved in @oldvalp, where
 * @oldvalp must have the type of pointer to a value specified in
 * @fmt.  If replacement or removal occurs, old @str is automatically
 * freed.
 *
 * Return Value: new options string is returned.
 */
#define replace_drop_opt(str, fmt, oldvalp, newval, cond)	\
({								\
	char *ret;						\
	if (cond)						\
		ret = replace_optval(				\
			str, fmt, oldvalp, newval);		\
	else							\
		ret = replace_opt(str, fmt, oldvalp, NULL);	\
	ret;							\
})

#endif /* _MOUNT_OPTS_H */
