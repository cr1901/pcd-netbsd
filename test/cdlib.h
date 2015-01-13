/*
 * cdlib.h
 */
#ifndef _CDLIB_H
#define _CDLIB_H

#include <sys/types.h>
#include <linux/cdrom.h>

#define CD_DEFAULT_DEVICE	"/dev/cdrom"

#define CD_OPENMODE_IOCTL	0
#define CD_OPENMODE_DATA	1

int cd_open_device(char *devname, int mode);

#define CD_SPEED_DEFAULT	0

int cd_set_speed(int f, int speed);

int cd_eject_tray(int f);
int cd_close_tray(int f);

int cd_get_drive_status(int f);
int cd_get_disk_status(int f);

int cd_get_options(int f);
int cd_set_options(int f, int flags);
int cd_clear_options(int f, int flags);

#endif /* _CDLIB_H */

