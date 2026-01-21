/*
 * lookup_device.h - Internal header for device lookup helper
 *
 * This file is not exported to users.
 */
#ifndef NILFS_LOOKUP_DEVICE_H
#define NILFS_LOOKUP_DEVICE_H

int nilfs_lookup_device(const char *node, char **devpath);

#endif /* NILFS_LOOKUP_DEVICE_H */
