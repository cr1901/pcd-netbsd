/*
 * cdlib.c
 */

#include <stdio.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include "cdlib.h"

int cd_open_device(char *devname, int mode)
{
	int f;
	int flags;

	if (devname == NULL || *devname == '\0')
		devname = CD_DEFAULT_DEVICE;
	flags = O_RDONLY | (mode == CD_OPENMODE_IOCTL ? O_NONBLOCK : 0);
	f = open(devname, flags);
	if (f == -1)
		return f;
	/*read_proc_cdrom_info();*/
	return f;
}

int cd_set_speed(int f, int speed)
{
	int r;

	/* XXX check info */
	r = ioctl(f, CDROM_SELECT_SPEED, speed);
	return r;
}

int cd_eject_tray(int f)
{
	int r;

	/* XXX check info */
	r = ioctl(f, CDROMEJECT);
	return r;
}

int cd_close_tray(int f)
{
	int r;

	/* XXX check info */
	r = ioctl(f, CDROMCLOSETRAY);
	return r;
}

int cd_get_drive_status(int f)
{
	int r;

	r = ioctl(f, CDROM_DRIVE_STATUS, CDSL_NONE);
	return r;
}

int cd_get_disk_status(int f)
{
	int r;

	r = ioctl(f, CDROM_DISC_STATUS);
	return r;
}

int cd_set_options(int f, int flags)
{
	int r;

	r = ioctl(f, CDROM_SET_OPTIONS, flags);
	return r;
}

int cd_clear_options(int f, int flags)
{
	int r;

	r = ioctl(f, CDROM_CLEAR_OPTIONS, flags);
	return r;
}

int cd_get_options(int f)
{
	int r;

	r = ioctl(f, CDROM_SET_OPTIONS, 0);
	return r;
}
