/*
 * nilfs-tune.c - adjust tunable filesystem parameters on NILFS filesystem
 *
 * Copyright (C) 2010 Jiro SEKIBA <jir@unicus.jp>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif	/* HAVE_CONFIG_H */

#include <stdio.h>

#if HAVE_STDLIB_H
#include <stdlib.h>
#endif	/* HAVE_STDLIB_H */

#if HAVE_ERR_H
#include <err.h>
#endif	/* HAVE_ERR_H */

#if HAVE_STRINGS_H
#include <strings.h>
#endif	/* HAVE_STRINGS_H */

#if HAVE_SYS_IOCTL_H
#include <sys/ioctl.h>
#endif	/* HAVE_SYS_IOCTL_H */

#if HAVE_STRING_H
#include <string.h>
#endif	/* HAVE_STRING_H */

#if HAVE_LIMITS_H
#include <limits.h>
#endif	/* HAVE_LIMITS_H */

#if HAVE_GRP_H
#include <grp.h>
#endif	/* HAVE_GRP_H */

#if HAVE_PWD_H
#include <pwd.h>
#endif	/* HAVE_PWD_H */

#include <sys/stat.h>
#include <ctype.h>
#include <assert.h>
#include <errno.h>
#include "nilfs.h"
#include "nilfs_feature.h"


extern int check_mount(const char *device);

struct nilfs_tune_options {
	int flags;
	int display;
	int mask;
	int force;
	__u32 c_interval;
	__u32 c_block_max;
	char label[80];
	__u8 uuid[16];
	char *fs_features;
};

static void nilfs_tune_usage(void)
{
	printf("Usage: nilfs-tune [-h] [-l] [-i interval] [-L volume_name]\n"
	       "                  [-m block_max] [-O [^]feature[,...]]\n"
	       "                  [-U UUID] device\n");
}

static const __u64 ok_features[NILFS_MAX_FEATURE_TYPES] = {
	/* Compat */
	0,
	/* Read-only compat */
	NILFS_FEATURE_COMPAT_RO_BLOCK_COUNT,
	/* Incompat */
	0
};

static const __u64 clear_ok_features[NILFS_MAX_FEATURE_TYPES] = {
	/* Compat */
	0,
	/* Read-only compat */
	NILFS_FEATURE_COMPAT_RO_BLOCK_COUNT,
	/* Incompat */
	0
};

static int parse_uuid(const char *uuid_string, __u8 *uuid)
{
	int i;
	char p[3];

	if (strlen(uuid_string) != 36)
		return -1;

	for (i = 0, p[2] = '\0'; i < 36; i++) {
		if ((i == 8) || (i == 13) || (i == 18) || (i == 23)) {
			if (uuid_string[i] == '-')
				continue;
			else
				return -1;
		}
		if (!isxdigit(uuid_string[i]) || !isxdigit(uuid_string[i+1]))
			return -1;

		p[0] = uuid_string[i++];
		p[1] = uuid_string[i];
		*uuid = strtoul(p, NULL, 16);
		uuid++;
	}
	return 0;
}

static void parse_options(int argc, char *argv[],
		struct nilfs_tune_options *opts)
{
	int c;

	opts->flags = O_RDONLY;
	opts->display = 0;
	opts->mask = 0;
	opts->force = 0;
	opts->fs_features = NULL;

	while ((c = getopt(argc, argv, "flhi:L:m:O:U:")) != EOF) {
		switch (c) {
		case 'f':
			opts->force = 1;
			break;
		case 'h':
			nilfs_tune_usage();
			exit(EXIT_SUCCESS);
			break;
		case 'i':
			opts->c_interval = atol(optarg);
			opts->mask |= NILFS_SB_COMMIT_INTERVAL;
			opts->flags = O_RDWR;
			break;
		case 'L':
			strncpy(opts->label, optarg, sizeof(opts->label));
			opts->mask |= NILFS_SB_LABEL;
			opts->flags = O_RDWR;
			break;
		case 'm':
			opts->c_block_max = atol(optarg);
			opts->mask |= NILFS_SB_BLOCK_MAX;
			opts->flags = O_RDWR;
			break;
		case 'l':
			opts->display = 1;
			break;
		case 'O':
			if (opts->fs_features) {
				warnx("-O may only be specified once");
				nilfs_tune_usage();
				exit(EXIT_FAILURE);
			}
			opts->fs_features = optarg;
			opts->mask |= NILFS_SB_FEATURES;
			opts->flags = O_RDWR;
			break;
		case 'U':
			if (parse_uuid(optarg, opts->uuid))
				errx(EXIT_FAILURE, "Invalid UUID format");
			opts->mask |= NILFS_SB_UUID;
			opts->flags = O_RDWR;
			break;
		default:
			nilfs_tune_usage();
			exit(EXIT_FAILURE);
		}
	}

	if (optind == argc) {
		nilfs_tune_usage();
		exit(EXIT_FAILURE);
	}
}

#define MINUTE	(60)
#define HOUR	(MINUTE * 60)
#define DAY	(HOUR * 24)
#define WEEK	(DAY * 7)
#define MONTH	(DAY * 30)

#define DIV_SECS(v, C)		\
do {				\
	if (secs > (C)) {	\
		v = secs / C;	\
		secs -= v * C;	\
	} else {		\
		v = 0;		\
	}			\
} while (0)

#define FORMAT_VARIABLE(v) \
do {				\
	if (v##s) {		\
		sprintf(tmp, "%s%d " #v "%s", buf[0] ? ", " : "", \
			v##s, (v##s > 1) ? "s" : ""); \
		strcat(buf, tmp);	\
	}			\
} while (0)

#if 0 /* filesystem check is not implemented yet */
static const char *interval_string(unsigned int secs)
{
	static char buf[512], tmp[128];
	int months, weeks, days, hours, minutes;

	if (secs == 0)
		return "none";

	buf[0] = 0;
	DIV_SECS(months, MONTH);
	DIV_SECS(weeks, WEEK);
	DIV_SECS(days, DAY);
	DIV_SECS(hours, HOUR);
	DIV_SECS(minutes, MINUTE);

	FORMAT_VARIABLE(month);
	FORMAT_VARIABLE(week);
	FORMAT_VARIABLE(day);
	FORMAT_VARIABLE(hour);
	FORMAT_VARIABLE(minute);
	FORMAT_VARIABLE(sec);

	return buf;
}
#endif

static const char *user_string(uid_t uid)
{
	static char tmp[LOGIN_NAME_MAX];
	static char buf[LOGIN_NAME_MAX + 8];
	struct passwd *pwd;

	strcpy(buf, "user ");

	pwd = getpwuid(uid);
	if (pwd)
		strncpy(tmp, pwd->pw_name, sizeof(tmp));
	else
		strcpy(tmp, "unknown");
	strcat(buf, tmp);
	return buf;
}

static const char *group_string(gid_t gid)
{
	static char tmp[LOGIN_NAME_MAX];
	static char buf[LOGIN_NAME_MAX + 8];
	struct group *grp;

	strcpy(buf, "group ");

	grp = getgrgid(gid);
	if (grp)
		strncpy(tmp, grp->gr_name, sizeof(tmp));
	else
		strcpy(tmp, "unknown");
	strcat(buf, tmp);
	return buf;
}

static const char *uuid_string(const unsigned char *uuid)
{
	static char buf[256];

	sprintf(buf, "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
		uuid[0], uuid[1], uuid[2], uuid[3], uuid[4], uuid[5], uuid[6],
		uuid[7], uuid[8], uuid[9], uuid[10], uuid[11], uuid[12],
		uuid[13], uuid[14], uuid[15]);
	return buf;
}

static const char *state_string(unsigned int state)
{
	static char buf[256];

	if (state & NILFS_VALID_FS)
		strcpy(buf, "valid");
	else
		strcpy(buf, "invalid or mounted");
	if (state & NILFS_ERROR_FS)
		strcat(buf, ",error");
	if (state & NILFS_RESIZE_FS)
		strcat(buf, ",resize");
	return buf;
}

static const char *creator_os_string(unsigned int creator)
{
	static char buf[64];

	switch (creator) {
	case NILFS_OS_LINUX:
		strcpy(buf, "Linux");
		break;
	default:
		strcpy(buf, "Unknown");
		break;
	}

	return buf;
}

static char *time_string(time_t t)
{
	return ctime(&t);
}

static void print_features(FILE *f, struct nilfs_super_block *sbp)
{
	__le64 *feature_p[3] = {
		&sbp->s_feature_compat,
		&sbp->s_feature_compat_ro,
		&sbp->s_feature_incompat
	};
	int printed = 0;
	int i, j;

	fputs("Filesystem features:     ", f);
	for (i = 0; i < 3; i++) {
		__u64 b, mask = le64_to_cpu(*(feature_p[i]));

		for (j = 0, b = 1; j < 64; j++, b <<= 1) {
			if (mask & b) {
				fputc(' ', f);
				fputs(nilfs_feature2string(i, b), f);
				printed++;
			}
		}
	}
	if (!printed)
		fputs(" (none)", f);
	fputs("\n", f);
}

static void show_nilfs_sb(struct nilfs_super_block *sbp)
{
	char label[sizeof(sbp->s_volume_name) + 1];
	gid_t gid;
	uid_t uid;

	memset(label, 0, sizeof(label));
	memcpy(label, sbp->s_volume_name, sizeof(sbp->s_volume_name));
	if (!label[0])
		strcpy(label, "(none)");

	printf("Filesystem volume name:\t  %s\n", label);
	printf("Filesystem UUID:\t  %s\n", uuid_string(sbp->s_uuid));
	printf("Filesystem magic number:  0x%04x\n",
	       le16_to_cpu(sbp->s_magic));
	printf("Filesystem revision #:\t  %d.%d\n",
	       le32_to_cpu(sbp->s_rev_level),
	       le32_to_cpu(sbp->s_minor_rev_level));

	print_features(stdout, sbp);

	/* sbp->s_flags is not used */

	printf("Filesystem state:\t  %s\n",
	       state_string(le16_to_cpu(sbp->s_state)));

	/* sbp->s_errors is not used */

	printf("Filesystem OS type:\t  %s\n",
	       creator_os_string(le32_to_cpu(sbp->s_creator_os)));

	printf("Block size:\t\t  %u\n",
	       1 << (le32_to_cpu(sbp->s_log_block_size) + 10));

	printf("Filesystem created:\t  %s",
	       time_string(le64_to_cpu(sbp->s_ctime)));
	printf("Last mount time:\t  %s",
	       time_string(le64_to_cpu(sbp->s_mtime)));
	printf("Last write time:\t  %s",
	       time_string(le64_to_cpu(sbp->s_wtime)));
	printf("Mount count:\t\t  %u\n", le16_to_cpu(sbp->s_mnt_count));
	printf("Maximum mount count:\t  %u\n",
	       le16_to_cpu(sbp->s_max_mnt_count));

#if 0 /* filesystem check is not implemented yet */
	{
		time_t t;
		unsigned int interval;

		t = (time_t)le64_to_cpu(sbp->s_lastcheck);
		printf("Last checked:\t\t  %s", ctime(&t));

		interval = le32_to_cpu(sbp->s_checkinterval);
		printf("Check interval:\t\t  %u (%s)\n", interval,
		       interval_string(interval));

		if (interval)
			printf("Next check after:\t  %s",
			       time_string(t+interval));
	}
#endif

	uid = (uid_t)le16_to_cpu(sbp->s_def_resuid);
	printf("Reserve blocks uid:\t  %u (%s)\n", uid, user_string(uid));
	gid = (gid_t)le16_to_cpu(sbp->s_def_resgid);
	printf("Reserve blocks gid:\t  %u (%s)\n", gid, group_string(gid));

	printf("First inode:\t\t  %u\n", le32_to_cpu(sbp->s_first_ino));

	printf("Inode size:\t\t  %u\n", le16_to_cpu(sbp->s_inode_size));
	printf("DAT entry size:\t\t  %u\n", le16_to_cpu(sbp->s_dat_entry_size));
	printf("Checkpoint size:\t  %u\n",
	       le16_to_cpu(sbp->s_checkpoint_size));
	printf("Segment usage size:\t  %u\n",
	       le16_to_cpu(sbp->s_segment_usage_size));

	printf("Number of segments:\t  %llu\n", le64_to_cpu(sbp->s_nsegments));
	printf("Device size:\t\t  %llu\n", le64_to_cpu(sbp->s_dev_size));
	printf("First data block:\t  %llu\n",
	       le64_to_cpu(sbp->s_first_data_block));
	printf("# of blocks per segment:  %u\n",
	       le32_to_cpu(sbp->s_blocks_per_segment));
	printf("Reserved segments %%:\t  %u\n",
	       le32_to_cpu(sbp->s_r_segments_percentage));
	printf("Last checkpoint #:\t  %llu\n", le64_to_cpu(sbp->s_last_cno));
	printf("Last block address:\t  %llu\n", le64_to_cpu(sbp->s_last_pseg));
	printf("Last sequence #:\t  %llu\n", le64_to_cpu(sbp->s_last_seq));
	printf("Free blocks count:\t  %llu\n",
	       le64_to_cpu(sbp->s_free_blocks_count));

	printf("Commit interval:\t  %u\n", le32_to_cpu(sbp->s_c_interval));
	printf("# of blks to create seg:  %u\n",
	       le32_to_cpu(sbp->s_c_block_max));

	printf("CRC seed:\t\t  0x%08x\n", le32_to_cpu(sbp->s_crc_seed));
	printf("CRC check sum:\t\t  0x%08x\n", le32_to_cpu(sbp->s_sum));
	printf("CRC check data size:\t  0x%08x\n", le32_to_cpu(sbp->s_bytes));
}

static int update_feature_set(struct nilfs_super_block *sbp,
			      struct nilfs_tune_options *opts)
{
	__u64 features[NILFS_MAX_FEATURE_TYPES];
	__u64 bad_mask;
	int bad_type;
	int ret;

	features[NILFS_FEATURE_TYPE_COMPAT] =
		le64_to_cpu(sbp->s_feature_compat);
	features[NILFS_FEATURE_TYPE_COMPAT_RO] =
		le64_to_cpu(sbp->s_feature_compat_ro);
	features[NILFS_FEATURE_TYPE_INCOMPAT] =
		le64_to_cpu(sbp->s_feature_incompat);

	assert(opts->fs_features != NULL);
	ret = nilfs_edit_feature(opts->fs_features, features, ok_features,
				 clear_ok_features, &bad_type, &bad_mask);
	if (ret < 0) {
		if (!bad_type) {
			warn("cannot parse features");
		} else if (bad_type & NILFS_FEATURE_TYPE_NEGATE_FLAG) {
			bad_type &= NILFS_FEATURE_TYPE_MASK;
			warnx("feature %s is not allowed to be cleared",
			      nilfs_feature2string(bad_type, bad_mask));
		} else {
			warnx("feature %s is not allowed to be set",
			      nilfs_feature2string(bad_type, bad_mask));
		}
	} else {
		sbp->s_feature_compat =
			cpu_to_le64(features[NILFS_FEATURE_TYPE_COMPAT]);
		sbp->s_feature_compat_ro =
			cpu_to_le64(features[NILFS_FEATURE_TYPE_COMPAT_RO]);
		sbp->s_feature_incompat =
			cpu_to_le64(features[NILFS_FEATURE_TYPE_INCOMPAT]);
	}
	return ret;
}

static int modify_nilfs(const char *device, struct nilfs_tune_options *opts)
{
	int devfd;
	int ret = EXIT_SUCCESS;
	struct nilfs_super_block *sbp;
	__u64 features;

	errno = 0;
	devfd = open(device, opts->flags);

	if (devfd == -1) {
		warn("cannot open device %s", device);
		ret = EXIT_FAILURE;
		goto out;
	}

	sbp = nilfs_sb_read(devfd);
	if (!sbp) {
		warnx("%s: cannot open NILFS", device);
		ret = EXIT_FAILURE;
		goto close_fd;
	}

	features = le64_to_cpu(sbp->s_feature_incompat);
	if (features & ~NILFS_FEATURE_INCOMPAT_SUPP)
		warnx("Warning: %s: unknown incompatible features: 0x%llx",
		      device, features);

	features = le64_to_cpu(sbp->s_feature_compat_ro);
	if (opts->flags == O_RDWR &&
	    (features & ~NILFS_FEATURE_COMPAT_RO_SUPP))
		warnx("Warning: %s: unknown read-only compatible features: 0x%llx",
		      device, features);

	if (opts->mask & NILFS_SB_LABEL)
		memcpy(sbp->s_volume_name, opts->label,
		       sizeof(opts->label));
	if (opts->mask & NILFS_SB_UUID)
		memcpy(sbp->s_uuid, opts->uuid, sizeof(opts->uuid));
	if (opts->mask & NILFS_SB_COMMIT_INTERVAL)
		sbp->s_c_interval = cpu_to_le32(opts->c_interval);
	if (opts->mask & NILFS_SB_BLOCK_MAX)
		sbp->s_c_block_max = cpu_to_le32(opts->c_block_max);
	if (opts->mask & NILFS_SB_FEATURES) {
		if (update_feature_set(sbp, opts) < 0) {
			ret = EXIT_FAILURE;
			goto free_sb;
		}
	}

	if (opts->mask) {
		if (nilfs_sb_write(devfd, sbp, opts->mask) < 0) {
			warnx("%s: cannot write super blocks", device);
			ret = EXIT_FAILURE;
		}
	}
	if (opts->display)
		show_nilfs_sb(sbp);

free_sb:
	free(sbp);
close_fd:
	close(devfd);
out:
	return ret;
}

int main(int argc, char *argv[])
{
	struct nilfs_tune_options opts;
	const char *device;

	printf("nilfs-tune %s\n", VERSION);
	if (argc < 2) {
		nilfs_tune_usage();
		exit(EXIT_SUCCESS);
	}

	parse_options(argc, argv, &opts);

	device = argv[argc-1];

	if (!device) {
		nilfs_tune_usage();
		exit(EXIT_FAILURE);
	}

	if (!opts.force && opts.flags == O_RDWR && (check_mount(device) < 0)) {
		fprintf(stderr, "ERROR: %s is currently mounted.  Aborting execution.\n"
			"Running nilfs-tune on a mounted file system may cause SEVERE damage.\n"
			"You can use the \"-f\" option to force this operation.\n",
			device);
		exit(EXIT_SUCCESS);
	}

	return modify_nilfs(device, &opts);
}
