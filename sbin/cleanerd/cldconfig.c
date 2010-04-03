/*
 * cldconfig.c - Configuration file of NILFS cleaner daemon.
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif	/* HAVE_CONFIG_H */

#include <stdio.h>

#if HAVE_STDLIB_H
#include <stdlib.h>
#endif	/* HAVE_STDLIB_H */

#include <ctype.h>

#if HAVE_STRING_H
#include <string.h>
#endif	/* HAVE_STRING_H */

#if HAVE_LIMITS_H
#include <limits.h>
#endif	/* HAVE_LIMITS_H */

#include <errno.h>

#if HAVE_SYSLOG_H
#include <syslog.h>
#endif	/* HAVE_SYSLOG_H */

#include "cldconfig.h"


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

	if (check_tokens(tokens, ntoks, 2, 2) < 0)
		return -1;

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

static int nilfs_cldconfig_get_ullong_argument(char **tokens, size_t ntoks,
					       unsigned long long *nump)
{
	unsigned long long num;
	char *endptr;

	if (check_tokens(tokens, ntoks, 2, 2) < 0)
		return -1;

	errno = 0;
	num = strtoull(tokens[1], &endptr, 10);
	if (*endptr != '\0') {
		syslog(LOG_WARNING, "%s: %s: not a number",
		       tokens[0], tokens[1]);
		return -1;
	}
	if (num == ULLONG_MAX && errno == ERANGE) {
		syslog(LOG_WARNING, "%s: %s: number too large",
		       tokens[0], tokens[1]);
		return -1;
	}
	*nump = num;
	return 0;
}

static int
nilfs_cldconfig_handle_protection_period(struct nilfs_cldconfig *config,
					 char **tokens, size_t ntoks)
{
	time_t period;
	char *endptr;

	if (check_tokens(tokens, ntoks, 2, 2) < 0)
		return 0;

	errno = 0;
	period = strtoul(tokens[1], &endptr, 10);
	if (*endptr != '\0') {
		syslog(LOG_WARNING, "%s: %s: not a number",
		       tokens[0], tokens[1]);
		return 0;
	}
	if ((period == ULONG_MAX) && (errno == ERANGE)) {
		syslog(LOG_WARNING, "%s: %s: number too large",
		       tokens[0], tokens[1]);
		return 0;
	}

	config->cf_protection_period = period;
	return 0;
}

static int
nilfs_cldconfig_handle_min_clean_segments(struct nilfs_cldconfig *config,
					  char **tokens, size_t ntoks)
{
	unsigned long long n;

	if (nilfs_cldconfig_get_ullong_argument(tokens, ntoks, &n) == 0)
		config->cf_min_clean_segments = n;
	return 0;
}

static int
nilfs_cldconfig_handle_max_clean_segments(struct nilfs_cldconfig *config,
					  char **tokens, size_t ntoks)
{
	unsigned long long n;

	if (nilfs_cldconfig_get_ullong_argument(tokens, ntoks, &n) == 0)
		config->cf_max_clean_segments = n;
	return 0;
}

static int
nilfs_cldconfig_handle_clean_check_interval(struct nilfs_cldconfig *config,
					    char **tokens, size_t ntoks)
{
	time_t period;
	char *endptr;

	if (check_tokens(tokens, ntoks, 2, 2) < 0)
		return 0;

	errno = 0;
	period = strtoul(tokens[1], &endptr, 10);
	if (*endptr != '\0') {
		syslog(LOG_WARNING, "%s: %s: not a number",
		       tokens[0], tokens[1]);
		return 0;
	}
	if ((period == ULONG_MAX) && (errno == ERANGE)) {
		syslog(LOG_WARNING, "%s: %s: number too large",
		       tokens[0], tokens[1]);
		return 0;
	}

	config->cf_clean_check_interval = period;
	return 0;
}

static unsigned long long
nilfs_cldconfig_selection_policy_timestamp(const struct nilfs_suinfo *si)
{
	return si->sui_lastmod;
}

static int
nilfs_cldconfig_handle_selection_policy_timestamp(struct nilfs_cldconfig *config,
						  char **tokens, size_t ntoks)
{
	if (check_tokens(tokens, ntoks, 2, 2) < 0)
		return 0;

	config->cf_selection_policy.p_importance =
		NILFS_CLDCONFIG_SELECTION_POLICY_IMPORTANCE;
	config->cf_selection_policy.p_threshold =
		NILFS_CLDCONFIG_SELECTION_POLICY_THRESHOLD;
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
					char **tokens, size_t ntoks)
{
	int i;

	if (check_tokens(tokens, ntoks, 2, 3) < 0)
		return 0;

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
					   char **tokens, size_t ntoks)
{
	char *endptr;
	int n;

	if (check_tokens(tokens, ntoks, 2, 2) < 0)
		return 0;

	errno = 0;
	n = strtoul(tokens[1], &endptr, 10);
	if (*endptr != '\0') {
		syslog(LOG_WARNING, "%s: %s: not a number",
		       tokens[0], tokens[1]);
		return 0;
	}
	if (n == 0) {
		syslog(LOG_WARNING, "%s: %s: invalid number",
		       tokens[0], tokens[1]);
		return 0;
	}
	if (n > NILFS_CLDCONFIG_NSEGMENTS_PER_CLEAN_MAX) {
		syslog(LOG_WARNING, "%s: %s: too large, use the maximum value",
		       tokens[0], tokens[1]);
		n = NILFS_CLDCONFIG_NSEGMENTS_PER_CLEAN_MAX;
	}

	config->cf_nsegments_per_clean = n;
	return 0;
}

static int
nilfs_cldconfig_handle_cleaning_interval(struct nilfs_cldconfig *config,
					 char **tokens, size_t ntoks)
{
	unsigned long sec;

	if (nilfs_cldconfig_get_ulong_argument(tokens, ntoks, &sec) == 0)
		config->cf_cleaning_interval = sec;
	return 0;
}

static int
nilfs_cldconfig_handle_retry_interval(struct nilfs_cldconfig *config,
				      char **tokens, size_t ntoks)
{
	unsigned long sec;

	if (nilfs_cldconfig_get_ulong_argument(tokens, ntoks, &sec) == 0)
		config->cf_retry_interval = sec;
	return 0;
}

static int nilfs_cldconfig_handle_use_mmap(struct nilfs_cldconfig *config,
					   char **tokens, size_t ntoks)
{
	if (check_tokens(tokens, ntoks, 1, 1) < 0)
		return 0;

	config->cf_use_mmap = 1;
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

#define NILFS_CLDCONFIG_NLOGPRIORITIES			\
	(sizeof(nilfs_cldconfig_log_priority_table) /	\
	 sizeof(nilfs_cldconfig_log_priority_table[0]))

static int
nilfs_cldconfig_handle_log_priority(struct nilfs_cldconfig *config,
				    char **tokens, size_t ntoks)
{
	int i;

	if (check_tokens(tokens, ntoks, 2, 2) < 0)
		return 0;

	for (i = 0; i < NILFS_CLDCONFIG_NLOGPRIORITIES; i++) {
		if (strcmp(tokens[1],
			   nilfs_cldconfig_log_priority_table[i].cl_name) == 0) {
			config->cf_log_priority =
				nilfs_cldconfig_log_priority_table[i].cl_priority;
			return 0;
		}
	}
	syslog(LOG_WARNING, "%s: %s: unknown log priority",
	       tokens[0], tokens[1]);
	return 0;
}

static int
nilfs_cldconfig_handle_unknown_keyword(struct nilfs_cldconfig *config,
				       char **tokens, size_t ntoks)
{
	syslog(LOG_WARNING, "%s: unknown keyword", tokens[0]);
	return 0;
}

static const struct nilfs_cldconfig_keyword
nilfs_cldconfig_keyword_table[] = {
	{"protection_period",	nilfs_cldconfig_handle_protection_period},
	{"min_clean_segments",	nilfs_cldconfig_handle_min_clean_segments},
	{"max_clean_segments",	nilfs_cldconfig_handle_max_clean_segments},
	{"clean_check_interval",nilfs_cldconfig_handle_clean_check_interval},
	{"selection_policy",	nilfs_cldconfig_handle_selection_policy},
	{"nsegments_per_clean",	nilfs_cldconfig_handle_nsegments_per_clean},
	{"cleaning_interval",	nilfs_cldconfig_handle_cleaning_interval},
	{"retry_interval",	nilfs_cldconfig_handle_retry_interval},
	{"use_mmap",		nilfs_cldconfig_handle_use_mmap},
	{"log_priority",	nilfs_cldconfig_handle_log_priority},
};

#define NILFS_CLDCONFIG_NKEYWORDS			\
	(sizeof(nilfs_cldconfig_keyword_table) /	\
	 sizeof(nilfs_cldconfig_keyword_table[0]))

static int nilfs_cldconfig_handle_keyword(struct nilfs_cldconfig *config,
					  char **tokens, size_t ntoks)
{
	int i;

	if (ntoks == 0)
		return 0;

	for (i = 0; i < NILFS_CLDCONFIG_NKEYWORDS; i++)
		if (strcmp(tokens[0],
			   nilfs_cldconfig_keyword_table[i].ck_text) == 0)
			return (*nilfs_cldconfig_keyword_table[i].ck_handler)(
				config, tokens, ntoks);

	return nilfs_cldconfig_handle_unknown_keyword(config, tokens, ntoks);
}

static void nilfs_cldconfig_set_default(struct nilfs_cldconfig *config)
{
	config->cf_selection_policy.p_importance =
		NILFS_CLDCONFIG_SELECTION_POLICY_IMPORTANCE;
	config->cf_selection_policy.p_threshold =
		NILFS_CLDCONFIG_SELECTION_POLICY_THRESHOLD;
	config->cf_protection_period = NILFS_CLDCONFIG_PROTECTION_PERIOD;
	config->cf_min_clean_segments = NILFS_CLDCONFIG_MIN_CLEAN_SEGMENTS;
	config->cf_max_clean_segments = NILFS_CLDCONFIG_MAX_CLEAN_SEGMENTS;
	config->cf_clean_check_interval = NILFS_CLDCONFIG_CLEAN_CHECK_INTERVAL;
	config->cf_nsegments_per_clean = NILFS_CLDCONFIG_NSEGMENTS_PER_CLEAN;
	config->cf_cleaning_interval = NILFS_CLDCONFIG_CLEANING_INTERVAL;
	config->cf_retry_interval = NILFS_CLDCONFIG_RETRY_INTERVAL;
	config->cf_use_mmap = NILFS_CLDCONFIG_USE_MMAP;
	config->cf_log_priority = NILFS_CLDCONFIG_LOG_PRIORITY;
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
				   const char *conffile)
{
	FILE *fp;
	char line[LINE_MAX], *tokens[NTOKENS_MAX];
	int ntoks, c, ret;

	if ((fp = fopen(conffile, "r")) == NULL)
		return -1;

	ret = 0;
	while (fgets(line, sizeof(line), fp) != NULL) {
		if (line[strlen(line) - 1] != '\n') {
			syslog(LOG_WARNING, "too long line");
			while (((c = fgetc(fp)) != '\n') && (c != EOF)) ;
		}
		if ((ntoks = tokenize(line, tokens, NTOKENS_MAX)) == 0)
			continue;
		if ((ret = nilfs_cldconfig_handle_keyword(
			     config, tokens, ntoks)) < 0)
			break;
	}

	fclose(fp);
	return ret;
}

/**
 * nilfs_cldconfig_read -
 * @config: config
 * @conffile: configuration file path
 */
int nilfs_cldconfig_read(struct nilfs_cldconfig *config, const char *path)
{
	nilfs_cldconfig_set_default(config);
	if (nilfs_cldconfig_do_read(config, path) < 0)
		syslog(LOG_WARNING, "%s: cannot read", path);
	return 0;
}
