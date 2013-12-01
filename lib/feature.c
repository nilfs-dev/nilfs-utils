/*
 * feature.c - NILFS features set routines
 *
 * Copyright (C) 2011-2012 Nippon Telegraph and Telephone Corporation.
 *
 * This program can be redistributed under the terms of the GNU Lesser
 * General Public License.
 *
 * This file is based on e2fsprogs lib/e2p/feature.c
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif	/* HAVE_CONFIG_H */

#include <stdio.h>

#if HAVE_STDLIB_H
#include <stdlib.h>
#endif	/* HAVE_STDLIB_H */

#if HAVE_STRING_H
#include <string.h>
#endif	/* HAVE_STRING_H */

#if HAVE_STRINGS_H
#include <strings.h>
#endif	/* HAVE_STRINGS_H */

#if HAVE_CTYPE_H
#include <ctype.h>
#endif	/* HAVE_CTYPE_H */

#if HAVE_SYS_TYPES_H
#include <sys/types.h>
#endif	/* HAVE_SYS_TYPES_H */

#if HAVE_LINUX_TYPES_H
#include <linux/types.h>
#endif	/* HAVE_LINUX_TYPES_H */

#include <linux/fs.h>

#include <errno.h>
#include <assert.h>
#include "nilfs.h"
#include "nilfs2_fs.h"
#include "nilfs_feature.h"

struct nilfs_feature {
	int	type;
	__u64	mask;
	char   *name;
};

static const struct nilfs_feature features[] = {
	/* Compat features */
	/* Read-only compat features */
	{ NILFS_FEATURE_TYPE_COMPAT_RO,
	  NILFS_FEATURE_COMPAT_RO_BLOCK_COUNT, "block_count" },
	/* Incompat features */
	/* End */
	{ 0, 0, NULL }
};

/**
 * nilfs_feature2string - convert a feature to a string
 * @compat_type: compatibility type of the feature
 * @mask: bit mask of the feature
 */
const char *nilfs_feature2string(int compat_type, __u64 mask)
{
	const struct nilfs_feature *feature;
	static char buf[32];
	char tchar;
	int i;

	for (feature = features; feature->name != NULL; feature++) {
		if (feature->type == compat_type && feature->mask == mask)
			return feature->name;
	}
	switch (compat_type) {
	case NILFS_FEATURE_TYPE_COMPAT:
		tchar = 'C';
		break;
	case NILFS_FEATURE_TYPE_COMPAT_RO:
		tchar = 'R';
		break;
	case NILFS_FEATURE_TYPE_INCOMPAT:
		tchar = 'I';
		break;
	default:
		tchar = '?';
		break;
	}

	for (i = 0; mask >>= 1; i++)
		;
	snprintf(buf, sizeof(buf), "FEATURE_%c%d", tchar, i);
	return buf;
}

/**
 * nilfs_string2feature - convert a string to a feature
 * @str: string to be parsed
 * @compat_type: buffer to store a compatibility type of the feature
 * @mask: buffer to store a bit mask of the feature
 *
 * Return Value: On success, zero is returned.  On error, minus one is
 * returned.
 */
int nilfs_string2feature(const char *str, int *compat_type, __u64 *mask)
{
	const struct nilfs_feature *feature;
	char *endptr;
	unsigned long num;
	int tchar, type;

	for (feature = features; feature->name != NULL; feature++) {
		if (strcasecmp(str, feature->name) == 0) {
			*compat_type = feature->type;
			*mask = feature->mask;
			return 0;
		}
	}

	if (strncasecmp(str, "FEATURE_", 8) != 0 || str[8] == '\0')
		return -1;

	tchar = toupper(str[8]);
	if (tchar == 'C')
		type = NILFS_FEATURE_TYPE_COMPAT;
	else if (tchar == 'R')
		type = NILFS_FEATURE_TYPE_COMPAT_RO;
	else if (tchar == 'I')
		type = NILFS_FEATURE_TYPE_INCOMPAT;
	else
		return -1;

	if (!isdigit(str[9]))
		return -1; /* non-digit including '\0', '-' and '+' */

	num = strtoul(&str[9], &endptr, 10);
	if (num >= 64 || *endptr != '\0')
		return -1;

	*mask = 1ULL << num;
	*compat_type = type;
	return 0;
}

static char *skip_over_blanks(char *cp)
{
	while (*cp && isspace(*cp))
		cp++;
	return cp;
}

static char *skip_over_word(char *cp)
{
	while (*cp && !isspace(*cp) && *cp != ',')
		cp++;
	return cp;
}

/**
 * nilfs_string2feature - convert a string to a feature
 * @str: string to be parsed
 * @compat_array: array to store resultant compat flags
 * @ok_array: array of bit masks that caller allows bet set
 * @clear_ok_array: array of bit masks that caller allows to be cleared
 * @bad_type: buffer to store a type of erroneous feature
 * @bad_mask: buffer to store a bit mask of erroneous feature
 *
 * Return Value: On success, zero is returned.  On error, minus one is
 * returned.
 */
int nilfs_edit_feature(const char *str, __u64 *compat_array,
		       const __u64 *ok_array, const __u64 *clear_ok_array,
		       int *bad_type, __u64 *bad_mask)
{
	char *cp, *next, *buf;
	__u64 mask;
	int type;
	int neg;
	int ret = 0;

	if (!clear_ok_array)
		clear_ok_array = ok_array;

	if (bad_type)
		*bad_type = 0;
	if (bad_mask)
		*bad_mask = 0;

	buf = malloc(strlen(str) + 1);
	if (!buf)
		return -1;

	strcpy(buf, str);
	for (cp = buf; cp && *cp; cp = next ? next + 1 : NULL) {
		neg = 0;
		cp = skip_over_blanks(cp);
		next = skip_over_word(cp);

		if (*next == '\0')
			next = NULL;
		else
			*next = '\0';

		if (strcasecmp(cp, "none") == 0) {
			compat_array[0] = 0;
			compat_array[1] = 0;
			compat_array[2] = 0;
			continue;
		}

		if (*cp == '^') {
			neg++;
			cp++;
		}

		if (nilfs_string2feature(cp, &type, &mask) < 0) {
			ret = -1;
			break;
		}
		if (neg) {
			if (clear_ok_array && !(clear_ok_array[type] & mask)) {
				if (bad_type)
					*bad_type = type |
						NILFS_FEATURE_TYPE_NEGATE_FLAG;
				if (bad_mask)
					*bad_mask = mask;
				ret = -1;
				break;
			}
			compat_array[type] &= ~mask;
		} else {
			if (ok_array && !(ok_array[type] & mask)) {
				if (bad_type)
					*bad_type = type;
				if (bad_mask)
					*bad_mask = mask;
				ret = -1;
				break;
			}
			compat_array[type] |= mask;
		}
	}
	free(buf);
	return ret;
}
