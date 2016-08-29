/*
 * cldconfig.c - Configuration file of NILFS cleaner daemon.
 *
 * Copyright (C) 2007-2012 Nippon Telegraph and Telephone Corporation.
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
 * Written by Koji Sato.
 *
 * Maintained by Ryusuke Konishi <konishi.ryusuke@lab.ntt.co.jp> from 2008.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif	/* HAVE_CONFIG_H */

#include <stdio.h>

#if HAVE_STDLIB_H
#include <stdlib.h>
#endif	/* HAVE_STDLIB_H */

#if HAVE_UNISTD_H
#include <unistd.h>
#endif	/* HAVE_UNISTD_H */

#include <ctype.h>

#if HAVE_STRING_H
#include <string.h>
#endif	/* HAVE_STRING_H */

#if HAVE_LIMITS_H
#include <limits.h>
#endif	/* HAVE_LIMITS_H */

#if HAVE_SYS_TYPES_H
#include <sys/types.h>
#endif	/* HAVE_SYS_TYPES_H */

#if HAVE_SYS_STAT_H
#include <sys/stat.h>
#endif	/* HAVE_SYS_STAT_H */

#if HAVE_TIME_H
#include <time.h>	/* timespec */
#endif	/* HAVE_TIME_H */

#if HAVE_SYSLOG_H
#include <syslog.h>
#endif	/* HAVE_SYSLOG_H */

#include <errno.h>
#include <assert.h>
#include "nilfs.h"
#include "util.h"
#include "cldconfig.h"


#define NILFS_CLDCONFIG_COMMENT_CHAR	'#'

/**
 * struct nilfs_cldconfig_keyword - keyword entry for conffile
 * @ck_text: keyword text
 * @ck_minargs: minimum number of arguments
 * @ck_maxargs: maximum number of arguments
 * @ck_handler: parser routine
 */
struct nilfs_cldconfig_keyword {
	const char *ck_text;
	size_t ck_minargs;
	size_t ck_maxargs;
	int (*ck_handler)(struct nilfs_cldconfig *, char **, size_t,
			  struct nilfs *);
};

/**
 * struct nilfs_cldconfig_polhandle - GC policy entry for conffile
 * @cp_name: policy name
 * @cp_handler: parser routine
 */
struct nilfs_cldconfig_polhandle {
	const char *cp_name;
	int (*cp_handler)(struct nilfs_cldconfig *, char **, size_t);
};

/**
 * struct nilfs_cldconfig_log_priority - log priority entry
 * @cl_name: priority name
 * @cl_priority: priority value
 */
struct nilfs_cldconfig_log_priority {
	const char *cl_name;
	int cl_priority;
};


static int check_tokens(char **tokens, size_t ntoks,
			size_t ntoksmin, size_t ntoksmax)
{
	if (ntoks < ntoksmin) {
		syslog(LOG_WARNING, "%s: too few arguments", tokens[0]);
		return -1;
	}
	if (ntoks > ntoksmax)
		syslog(LOG_WARNING, "%s: too many arguments", tokens[0]);
	return 0;
}

static int nilfs_cldconfig_get_ulong_argument(char **tokens, size_t ntoks,
					      unsigned long *nump)
{
	unsigned long num;
	char *endptr;

	errno = 0;
	num = strtoul(tokens[1], &endptr, 10);
	if (*endptr != '\0') {
		syslog(LOG_WARNING, "%s: %s: not a number",
		       tokens[0], tokens[1]);
		return -1;
	}
	if (num == ULONG_MAX && errno == ERANGE) {
		syslog(LOG_WARNING, "%s: %s: number too large",
		       tokens[0], tokens[1]);
		return -1;
	}
	*nump = num;
	return 0;
}

static int nilfs_cldconfig_get_time_argument(char **tokens, size_t ntoks,
					     struct timespec *ts)
{
	unsigned long num;
	double fnum;
	char *endptr;

	errno = 0;
	num = strtoul(tokens[1], &endptr, 10);
	if (endptr == tokens[1])
		goto failed;
	if (*endptr == '.') {
		errno = 0;
		fnum = strtod(tokens[1], &endptr);
		if (endptr == tokens[1] || *endptr != '\0')
			goto failed;
		if (errno == ERANGE)
			goto failed_too_large;
		ts->tv_sec = num;
		ts->tv_nsec = (fnum - num) * 1000000000L;
	} else if (*endptr != '\0') {
		goto failed;
	} else {
		if (num == ULONG_MAX)
			goto failed_too_large;
		ts->tv_sec = num;
		ts->tv_nsec = 0;
	}
	return 0;

failed_too_large:
	syslog(LOG_WARNING, "%s: %s: number too large", tokens[0], tokens[1]);
	return -1;
failed:
	syslog(LOG_WARNING, "%s: %s: not a time value", tokens[0], tokens[1]);
	return -1;
}

static int nilfs_parse_size_suffix(const char *suffix)
{
	const char *cp = suffix;
	int ret = -1;

	if (*cp == 'k') {
		if (*++cp == 'B') { /* kB (SI suffix) */
			ret = NILFS_SIZE_UNIT_KB;
			cp++;
		}
	} else {
		switch (*cp) {
		case 'K':
			ret = NILFS_SIZE_UNIT_KB;
			break;
		case 'M':
			ret = NILFS_SIZE_UNIT_MB;
			break;
		case 'G':
			ret = NILFS_SIZE_UNIT_GB;
			break;
		case 'T':
			ret = NILFS_SIZE_UNIT_TB;
			break;
		case 'P':
			ret = NILFS_SIZE_UNIT_PB;
			break;
		case 'E':
			ret = NILFS_SIZE_UNIT_EB;
			break;
		}
		if (ret > 0) {
			cp++;
			if (*cp == 'B') { /* ?B (SI) */
				cp++;
			} else if (*cp == '\0') { /* K, M, G, T, ... (IEC) */
				ret++;
			} else if (*cp == 'i' && cp[1] == 'B') { /* ?iB (IEC) */
				ret++;
				cp += 2;
			} else { /* error */
				ret = -1;
			}
		}
	}
	if (ret >= 0 && *cp != '\0')
		ret = -1;
	return ret;
}

static int nilfs_cldconfig_get_size_argument(char **tokens, size_t ntoks,
					     struct nilfs_param *param)
{
	unsigned long num;
	char *endptr;
	int unit;

	errno = 0;
	num = strtoul(tokens[1], &endptr, 10);
	if (endptr == tokens[1]) {
		syslog(LOG_WARNING, "%s: %s: not a number",
		       tokens[0], tokens[1]);
		return -1;
	}
	if (num == ULONG_MAX && errno == ERANGE) {
		syslog(LOG_WARNING, "%s: %s: number too large",
		       tokens[0], tokens[1]);
		return -1;
	}

	if (*endptr == '\0') {
		unit = NILFS_SIZE_UNIT_NONE;
	} else if (endptr[0] == '%' && endptr[1] == '\0') {
		if (num > 100) {
			syslog(LOG_WARNING, "%s: %s: too large ratio",
			       tokens[0], tokens[1]);
			return -1;
		}
		unit = NILFS_SIZE_UNIT_PERCENT;
	} else {
		unit = nilfs_parse_size_suffix(endptr);
	}
	if (unit < 0) {
		syslog(LOG_WARNING, "%s: %s: bad expression",
		       tokens[0], tokens[1]);
		return -1;
	}
	param->unit = unit;
	param->num = num;
	return 0;
}

static int
nilfs_cldconfig_handle_protection_period(struct nilfs_cldconfig *config,
					 char **tokens, size_t ntoks,
					 struct nilfs *nilfs)
{
	return nilfs_cldconfig_get_time_argument(
		tokens, ntoks, &config->cf_protection_period);
}

static unsigned long long
nilfs_convert_units_to_bytes(const struct nilfs_param *param)
{
	unsigned long long bytes = param->num;

	switch (param->unit) {
	case NILFS_SIZE_UNIT_KB:
		bytes *= 1000ULL;
		break;
	case NILFS_SIZE_UNIT_KIB:
		bytes <<= 10;
		break;
	case NILFS_SIZE_UNIT_MB:
		bytes *= 1000000ULL;
		break;
	case NILFS_SIZE_UNIT_MIB:
		bytes <<= 20;
		break;
	case NILFS_SIZE_UNIT_GB:
		bytes *= 1000000000ULL;
		break;
	case NILFS_SIZE_UNIT_GIB:
		bytes <<= 30;
		break;
	case NILFS_SIZE_UNIT_TB:
		bytes *= 1000000000000ULL;
		break;
	case NILFS_SIZE_UNIT_TIB:
		bytes <<= 40;
		break;
	case NILFS_SIZE_UNIT_PB:
		bytes *= 1000000000000000ULL;
		break;
	case NILFS_SIZE_UNIT_PIB:
		bytes <<= 50;
		break;
	case NILFS_SIZE_UNIT_EB:
		bytes *= 1000000000000000000ULL;
		break;
	case NILFS_SIZE_UNIT_EIB:
		bytes <<= 60;
		break;
	default:
		assert(0);
	}

	return bytes;
}

static unsigned long long
nilfs_convert_size_to_nsegments(struct nilfs *nilfs, struct nilfs_param *param)
{
	unsigned long long ret, segment_size, bytes;

	if (param->unit == NILFS_SIZE_UNIT_NONE) {
		ret = param->num;
	} else if (param->unit == NILFS_SIZE_UNIT_PERCENT) {
		ret = (nilfs_get_nsegments(nilfs) * param->num + 99) / 100;
	} else {
		bytes = nilfs_convert_units_to_bytes(param);
		segment_size = nilfs_get_block_size(nilfs) *
			nilfs_get_blocks_per_segment(nilfs);
		ret = (bytes + segment_size - 1) / segment_size;
	}
	return ret;
}

static int
nilfs_cldconfig_handle_min_clean_segments(struct nilfs_cldconfig *config,
					  char **tokens, size_t ntoks,
					  struct nilfs *nilfs)
{
	struct nilfs_param param;

	if (nilfs_cldconfig_get_size_argument(tokens, ntoks, &param) == 0)
		config->cf_min_clean_segments =
			nilfs_convert_size_to_nsegments(nilfs, &param);
	return 0;
}

static int
nilfs_cldconfig_handle_max_clean_segments(struct nilfs_cldconfig *config,
					  char **tokens, size_t ntoks,
					  struct nilfs *nilfs)
{
	struct nilfs_param param;

	if (nilfs_cldconfig_get_size_argument(tokens, ntoks, &param) == 0)
		config->cf_max_clean_segments =
			nilfs_convert_size_to_nsegments(nilfs, &param);
	return 0;
}

static int
nilfs_cldconfig_handle_clean_check_interval(struct nilfs_cldconfig *config,
					    char **tokens, size_t ntoks,
					    struct nilfs *nilfs)
{
	return nilfs_cldconfig_get_time_argument(
		tokens, ntoks, &config->cf_clean_check_interval);
}

static int
nilfs_cldconfig_handle_selection_policy_timestamp(struct nilfs_cldconfig *cf,
						  char **tokens, size_t ntoks)
{
	cf->cf_selection_policy = NILFS_SELECTION_POLICY_TIMESTAMP;
	return 0;
}

static const struct nilfs_cldconfig_polhandle
nilfs_cldconfig_polhandle_table[] = {
	{"timestamp",	nilfs_cldconfig_handle_selection_policy_timestamp},
};

#define NILFS_CLDCONFIG_NPOLHANDLES			\
	(sizeof(nilfs_cldconfig_polhandle_table) /		\
	 sizeof(nilfs_cldconfig_polhandle_table[0]))

static int
nilfs_cldconfig_handle_selection_policy(struct nilfs_cldconfig *config,
					char **tokens, size_t ntoks,
					struct nilfs *nilfs)
{
	int i;

	for (i = 0; i < NILFS_CLDCONFIG_NPOLHANDLES; i++) {
		if (strcmp(tokens[1],
			   nilfs_cldconfig_polhandle_table[i].cp_name) == 0)
			return nilfs_cldconfig_polhandle_table[i].cp_handler(
				config, tokens, ntoks);
	}

	syslog(LOG_WARNING, "%s: %s: unknown policy", tokens[0], tokens[1]);
	return 0;
}

static int
nilfs_cldconfig_handle_nsegments_per_clean(struct nilfs_cldconfig *config,
					   char **tokens, size_t ntoks,
					   struct nilfs *nilfs)
{
	unsigned long n;

	if (nilfs_cldconfig_get_ulong_argument(tokens, ntoks, &n) < 0)
		return 0;

	if (n > NILFS_CLDCONFIG_NSEGMENTS_PER_CLEAN_MAX) {
		syslog(LOG_WARNING, "%s: %s: too large, use the maximum value",
		       tokens[0], tokens[1]);
		n = NILFS_CLDCONFIG_NSEGMENTS_PER_CLEAN_MAX;
	}

	config->cf_nsegments_per_clean = n;
	return 0;
}

static int
nilfs_cldconfig_handle_mc_nsegments_per_clean(struct nilfs_cldconfig *config,
					      char **tokens, size_t ntoks,
					      struct nilfs *nilfs)
{
	unsigned long n;

	if (nilfs_cldconfig_get_ulong_argument(tokens, ntoks, &n) < 0)
		return 0;

	if (n > NILFS_CLDCONFIG_NSEGMENTS_PER_CLEAN_MAX) {
		syslog(LOG_WARNING, "%s: %s: too large, use the maximum value",
		       tokens[0], tokens[1]);
		n = NILFS_CLDCONFIG_NSEGMENTS_PER_CLEAN_MAX;
	}

	config->cf_mc_nsegments_per_clean = n;
	return 0;
}

static unsigned long long
nilfs_convert_size_to_blocks_per_segment(struct nilfs *nilfs,
					 struct nilfs_param *param)
{
	unsigned long long ret, segment_size, block_size, bytes;

	if (param->unit == NILFS_SIZE_UNIT_NONE) {
		ret = param->num;
	} else if (param->unit == NILFS_SIZE_UNIT_PERCENT) {
		ret = (nilfs_get_blocks_per_segment(nilfs) * param->num + 99)
				/ 100;
	} else {
		block_size = nilfs_get_block_size(nilfs);
		segment_size = block_size *
				nilfs_get_blocks_per_segment(nilfs);
		bytes = nilfs_convert_units_to_bytes(param) % segment_size;
		ret = (bytes + block_size - 1) / block_size;
	}
	return ret;
}

static int
nilfs_cldconfig_handle_min_reclaimable_blocks(struct nilfs_cldconfig *config,
					      char **tokens, size_t ntoks,
					      struct nilfs *nilfs)
{
	struct nilfs_param param;

	if (nilfs_cldconfig_get_size_argument(tokens, ntoks, &param) == 0)
		config->cf_min_reclaimable_blocks =
			nilfs_convert_size_to_blocks_per_segment(nilfs, &param);
	return 0;
}

static int
nilfs_cldconfig_handle_mc_min_reclaimable_blocks(struct nilfs_cldconfig *config,
						 char **tokens, size_t ntoks,
						 struct nilfs *nilfs)
{
	struct nilfs_param param;

	if (nilfs_cldconfig_get_size_argument(tokens, ntoks, &param) == 0)
		config->cf_mc_min_reclaimable_blocks =
			nilfs_convert_size_to_blocks_per_segment(nilfs, &param);
	return 0;
}

static int
nilfs_cldconfig_handle_cleaning_interval(struct nilfs_cldconfig *config,
					 char **tokens, size_t ntoks,
					 struct nilfs *nilfs)
{
	return nilfs_cldconfig_get_time_argument(
		tokens, ntoks, &config->cf_cleaning_interval);
}

static int
nilfs_cldconfig_handle_mc_cleaning_interval(struct nilfs_cldconfig *config,
					    char **tokens, size_t ntoks,
					    struct nilfs *nilfs)
{
	return nilfs_cldconfig_get_time_argument(
		tokens, ntoks, &config->cf_mc_cleaning_interval);
}

static int
nilfs_cldconfig_handle_retry_interval(struct nilfs_cldconfig *config,
				      char **tokens, size_t ntoks,
				      struct nilfs *nilfs)
{
	return nilfs_cldconfig_get_time_argument(
		tokens, ntoks, &config->cf_retry_interval);
}

static int nilfs_cldconfig_handle_use_mmap(struct nilfs_cldconfig *config,
					   char **tokens, size_t ntoks,
					   struct nilfs *nilfs)
{
	config->cf_use_mmap = 1;
	return 0;
}

static int nilfs_cldconfig_handle_use_set_suinfo(struct nilfs_cldconfig *config,
						 char **tokens, size_t ntoks,
						 struct nilfs *nilfs)
{
	config->cf_use_set_suinfo = 1;
	return 0;
}

static const struct nilfs_cldconfig_log_priority
nilfs_cldconfig_log_priority_table[] = {
	{"emerg",	LOG_EMERG},
	{"alert",	LOG_ALERT},
	{"crit",	LOG_CRIT},
	{"err",		LOG_ERR},
	{"warning",	LOG_WARNING},
	{"notice",	LOG_NOTICE},
	{"info",	LOG_INFO},
	{"debug",	LOG_DEBUG},
};

static int
nilfs_cldconfig_handle_log_priority(struct nilfs_cldconfig *config,
				    char **tokens, size_t ntoks,
				    struct nilfs *nilfs)
{
	const struct nilfs_cldconfig_log_priority *clp;
	int i;

	clp = nilfs_cldconfig_log_priority_table;
	for (i = 0; i < ARRAY_SIZE(nilfs_cldconfig_log_priority_table);
	     i++, clp++) {
		if (strcmp(tokens[1], clp->cl_name) == 0) {
			config->cf_log_priority = clp->cl_priority;
			return 0;
		}
	}
	syslog(LOG_WARNING, "%s: %s: unknown log priority",
	       tokens[0], tokens[1]);
	return 0;
}

static const struct nilfs_cldconfig_keyword
nilfs_cldconfig_keyword_table[] = {
	{
		"protection_period", 2, 2,
		nilfs_cldconfig_handle_protection_period
	},
	{
		"min_clean_segments", 2, 2,
		nilfs_cldconfig_handle_min_clean_segments
	},
	{
		"max_clean_segments", 2, 2,
		nilfs_cldconfig_handle_max_clean_segments
	},
	{
		"clean_check_interval", 2, 2,
		nilfs_cldconfig_handle_clean_check_interval
	},
	{
		"selection_policy", 2, 3,
		nilfs_cldconfig_handle_selection_policy
	},
	{
		"nsegments_per_clean", 2, 2,
		nilfs_cldconfig_handle_nsegments_per_clean
	},
	{
		"mc_nsegments_per_clean", 2, 2,
		nilfs_cldconfig_handle_mc_nsegments_per_clean
	},
	{
		"cleaning_interval", 2, 2,
		nilfs_cldconfig_handle_cleaning_interval
	},
	{
		"mc_cleaning_interval", 2, 2,
		nilfs_cldconfig_handle_mc_cleaning_interval
	},
	{
		"retry_interval", 2, 2,
		nilfs_cldconfig_handle_retry_interval
	},
	{
		"use_mmap", 1, 1,
		nilfs_cldconfig_handle_use_mmap
	},
	{
		"log_priority", 2, 2,
		nilfs_cldconfig_handle_log_priority
	},
	{
		"min_reclaimable_blocks", 2, 2,
		nilfs_cldconfig_handle_min_reclaimable_blocks
	},
	{
		"mc_min_reclaimable_blocks", 2, 2,
		nilfs_cldconfig_handle_mc_min_reclaimable_blocks
	},
	{
		"use_set_suinfo", 1, 1,
		nilfs_cldconfig_handle_use_set_suinfo
	},
};

static int nilfs_cldconfig_handle_keyword(struct nilfs_cldconfig *config,
					  char **tokens, size_t ntoks,
					  struct nilfs *nilfs)
{
	const struct nilfs_cldconfig_keyword *kw;
	int i;

	if (ntoks == 0)
		return 0;

	kw = nilfs_cldconfig_keyword_table;
	for (i = 0; i < ARRAY_SIZE(nilfs_cldconfig_keyword_table); i++, kw++) {
		if (strcmp(tokens[0], kw->ck_text) == 0) {
			if (check_tokens(tokens, ntoks, kw->ck_minargs,
					 kw->ck_maxargs) < 0)
				return 0;
			return kw->ck_handler(config, tokens, ntoks, nilfs);
		}
	}

	syslog(LOG_WARNING, "%s: unknown keyword", tokens[0]);
	return 0;
}

static void nilfs_cldconfig_set_default(struct nilfs_cldconfig *config,
					struct nilfs *nilfs)
{
	struct nilfs_param param;

	config->cf_selection_policy = NILFS_SELECTION_POLICY_TIMESTAMP;

	config->cf_protection_period.tv_sec = NILFS_CLDCONFIG_PROTECTION_PERIOD;
	config->cf_protection_period.tv_nsec = 0;

	param.num = NILFS_CLDCONFIG_MIN_CLEAN_SEGMENTS;
	param.unit = NILFS_CLDCONFIG_MIN_CLEAN_SEGMENTS_UNIT;
	config->cf_min_clean_segments =
		nilfs_convert_size_to_nsegments(nilfs, &param);

	param.num = NILFS_CLDCONFIG_MAX_CLEAN_SEGMENTS;
	param.unit = NILFS_CLDCONFIG_MAX_CLEAN_SEGMENTS_UNIT;
	config->cf_max_clean_segments =
		nilfs_convert_size_to_nsegments(nilfs, &param);

	config->cf_clean_check_interval.tv_sec =
		NILFS_CLDCONFIG_CLEAN_CHECK_INTERVAL;
	config->cf_clean_check_interval.tv_nsec = 0;
	config->cf_nsegments_per_clean = NILFS_CLDCONFIG_NSEGMENTS_PER_CLEAN;
	config->cf_mc_nsegments_per_clean =
		NILFS_CLDCONFIG_MC_NSEGMENTS_PER_CLEAN;
	config->cf_cleaning_interval.tv_sec = NILFS_CLDCONFIG_CLEANING_INTERVAL;
	config->cf_cleaning_interval.tv_nsec = 0;
	config->cf_mc_cleaning_interval.tv_sec =
		NILFS_CLDCONFIG_MC_CLEANING_INTERVAL;
	config->cf_mc_cleaning_interval.tv_nsec = 0;
	config->cf_retry_interval.tv_sec = NILFS_CLDCONFIG_RETRY_INTERVAL;
	config->cf_retry_interval.tv_nsec = 0;
	config->cf_use_mmap = NILFS_CLDCONFIG_USE_MMAP;
	config->cf_use_set_suinfo = NILFS_CLDCONFIG_USE_SET_SUINFO;
	config->cf_log_priority = NILFS_CLDCONFIG_LOG_PRIORITY;

	param.num = NILFS_CLDCONFIG_MIN_RECLAIMABLE_BLOCKS;
	param.unit = NILFS_CLDCONFIG_MIN_RECLAIMABLE_BLOCKS_UNIT;
	config->cf_min_reclaimable_blocks =
		nilfs_convert_size_to_blocks_per_segment(nilfs, &param);

	param.num = NILFS_CLDCONFIG_MC_MIN_RECLAIMABLE_BLOCKS;
	param.unit = NILFS_CLDCONFIG_MC_MIN_RECLAIMABLE_BLOCKS_UNIT;
	config->cf_mc_min_reclaimable_blocks =
		nilfs_convert_size_to_blocks_per_segment(nilfs, &param);
}

static inline int iseol(int c)
{
	return (c == '\n' || c == '\0' || c == NILFS_CLDCONFIG_COMMENT_CHAR);
}

static size_t tokenize(char *line, char **tokens, size_t ntoks)
{
	char *p;
	size_t n;

	p = line;
	for (n = 0; n < ntoks; n++) {
		while (isspace(*p))
			p++;
		if (iseol(*p))
			break;
		tokens[n] = p++;
		while (!isspace(*p) && !iseol(*p))
			p++;
		if (isspace(*p))
			*p++ = '\0';
		else
			*p = '\0';
	}
	return n;
}

#ifndef LINE_MAX
#define LINE_MAX	2048
#endif	/* LINE_MAX */
#define NTOKENS_MAX	16

static int nilfs_cldconfig_do_read(struct nilfs_cldconfig *config,
				   const char *conffile, struct nilfs *nilfs)
{
	FILE *fp;
	char line[LINE_MAX], *tokens[NTOKENS_MAX];
	int ntoks, c, ret;

	fp = fopen(conffile, "r");
	if (!fp)
		return -1;

	ret = 0;
	while (fgets(line, sizeof(line), fp) != NULL) {
		if (line[strlen(line) - 1] != '\n') {
			syslog(LOG_WARNING, "too long line");
			while (((c = fgetc(fp)) != '\n') && (c != EOF))
				;
		}
		ntoks = tokenize(line, tokens, NTOKENS_MAX);
		if (ntoks == 0)
			continue;
		ret = nilfs_cldconfig_handle_keyword(config, tokens, ntoks,
						     nilfs);
		if (ret < 0)
			break;
	}

	fclose(fp);
	return ret;
}

/**
 * nilfs_cldconfig_read -
 * @config: config
 * @conffile: configuration file path
 * @nilfs: nilfs object
 */
int nilfs_cldconfig_read(struct nilfs_cldconfig *config, const char *path,
			 struct nilfs *nilfs)
{
	struct stat stbuf;

	if (stat(path, &stbuf) < 0 || !S_ISREG(stbuf.st_mode)) {
		syslog(LOG_ERR, "%s: bad configuration file", path);
		errno = EINVAL;
		return -1;
	}

	nilfs_cldconfig_set_default(config, nilfs);
	if (nilfs_cldconfig_do_read(config, path, nilfs) < 0)
		syslog(LOG_WARNING, "%s: cannot read", path);
	return 0;
}
