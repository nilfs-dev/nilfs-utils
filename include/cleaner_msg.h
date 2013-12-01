/*
 * cleaner_msg.h - NILFS cleaner message format
 *
 * Licensed under LGPLv2: the complete text of the GNU Lesser General
 * Public License can be found in COPYING file of the nilfs-utils
 * package.
 *
 * Copyright (C) 2011-2012 Nippon Telegraph and Telephone Corporation.
 */

#ifndef NILFS_CLEANER_MSG_H
#define NILFS_CLEANER_MSG_H

#include <stdint.h>
#include <uuid/uuid.h>
#include "nilfs_cleaner.h"

#define NILFS_CLEANER_PRIO_HIGH		1
#define NILFS_CLEANER_PRIO_NORMAL	9

#define NILFS_CLEANER_MSG_MAX_PATH	4064 /* max pathname length */
#define NILFS_CLEANER_MSG_MAX_REQSZ	4096 /* max request size */

enum {
	NILFS_CLEANER_CMD_GET_STATUS,	/* get status */
	NILFS_CLEANER_CMD_RUN,		/* run gc */
	NILFS_CLEANER_CMD_SUSPEND,	/* suspend */
	NILFS_CLEANER_CMD_RESUME,	/* resume */
	NILFS_CLEANER_CMD_TUNE,		/* set parameter */
	NILFS_CLEANER_CMD_RELOAD,	/* reload configuration file */
	NILFS_CLEANER_CMD_WAIT,		/* wait for completion of a job */
	NILFS_CLEANER_CMD_STOP,		/* stop running gc */
	NILFS_CLEANER_CMD_SHUTDOWN,	/* shutdown daemon */
};


struct nilfs_cleaner_request {
	int32_t cmd;
	uint32_t argsize;
	uuid_t client_uuid;
	/* must be aligned to 64-bit size boundary */

	char buf[0]; /* payload */
};

struct nilfs_cleaner_request_with_args {
	struct nilfs_cleaner_request hdr;
	struct nilfs_cleaner_args args;
};

struct nilfs_cleaner_request_with_path {
	struct nilfs_cleaner_request hdr;
	char pathname[NILFS_CLEANER_MSG_MAX_PATH];
};

struct nilfs_cleaner_request_with_jobid {
	struct nilfs_cleaner_request hdr;
	uint32_t jobid;
};

enum {
	NILFS_CLEANER_RSP_ACK,
	NILFS_CLEANER_RSP_NACK,
};

struct nilfs_cleaner_response {
	int16_t result;
	int16_t status; /* cleanerd status */
	int32_t err;
	uint32_t jobid;
	uint32_t pad;
};

#endif /* NILFS_CLEANER_MSG_H */
