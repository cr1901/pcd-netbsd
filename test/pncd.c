#define PNCD_VERSION	"0.40"
/*
	pncd.c	single Matsushita/Panasonic CR56x CD-ROM driver for Linux

	Copyright (C) 1996-2002 VOROSBARANYI Zoltan

	pncd web site:	http://vbzo.li/en/pncd/

	e-mail:		pncd@vbzo.li

	address:	H-2145 Szilasliget
			Kiss J. u. 4.
			Hungary EUROPE


	Single Matsushita/Panasonic CR-56x CDROM driver.
	See the pncd.README for details.


	Thanks:

	Brian Candler who sent me important patches and encouragements.

	The many authors of The Kernel, from their code I
	got the most information I needed for my driver:

	David A. van Leeuwen, Erik Andersen, Jens Axboe for the Unified Driver.
	Eberhard Moenkeberg for sbpcd, Heiko Schlittermann, Martin Harriss, 
	Corey Minyard, Werner Zimmermann to name a few other CDROM 
	driver writers.

	Linus Torvalds and others for the kernel.

	Richard Stallman and others from the FSF for the development tools
	and GPL.

	And all others who write free programs.
	
	
	Change log:

	0.40
	- port to 2.4.x, renamed to pncd
	
	0.38
	- increased SEARCH_BACK in pcd_search_end
	- inserted a missing UP(cmd_s) in pcd_dev_ioctl
	- pcd.changed = 0 statement missed from pcd_open (UNIFORM_DRIVER)
	- pcd_open (UNIFORM_DRIVER) small fix in the first retry loop

	0.37
	- AUDIOCMDCLASS changed to DRIVECMDCLASS in pcd_volume, pcd_readvolume 
	- CDROMAUDIOBUFSIZ implemented
	- CDROMREADAUDIO now uses vmalloc'd memory area
	- ``wait for no DST_NODISK'' code inserted into pcd_drive_status

	0.36
	- operations protected by a semaphore (Uniform Driver)
	- pcd_delay changed (Uniform Driver)
	- prev_status handling changed
	- setting drive speed (Uniform Driver)
	- reading MCN (Uniform Driver)
	- reading audio frames (tested with cdda2wav)
	- pcd_ioctl changed

	0.35
	- adapted to the 2.2.x kernels
	- Uniform CDROM Driver support (for 2.2.x kernels)
	- compile time kernel version autodetection
	- many minor changes

	0.30
	- the device can now be opened without disk in drive
	- minor modifications in ioctl
	- authors e-mail changed to vbzoli@hbrt.hu

	0.29
	- added drive id hunting
	- works with 2.1.xx kernels

	0.211
	- pcd_select_drive added

	0.21 
	- All the changes below for version 0.21 by Brian Candler -- 
	  B.Candler@pobox.com
	- Fixed cache bug (pcd.bufaddr value used to invalidate cache)
	- Added module param "io=0xNNN" to set base address
	- Changed to call check_region BEFORE probing
	- Modified to allow 512-byte blocks (used by Macintosh HFS)
	- Made some used-once functions inline

	0.20
	- Sound Blaster interface support added
	  (Zoltan Pataki (zpataki@amon.omgk.hu) was kind to me lending 
	  his SoundBlaster card, thank you Zoli!)
	- autodetection added

	0.18
	- pcd_search_end now determines the size of the disk
	  (searches for the last readable block)
	  blk_size is now correctly set, dd won't return
	  with I/O errors.
	- minor changes in pcd_readcmd, pcd_init

	0.17:
	- tested with kernel 2.0.0 and kerneld
	- implemented new ioctl's: EJECT_SW, RESET, VOLREAD, CLOSETRAY
	- resetless initialization (useful for command line audio
	  players and kerneld)
	- new option: CLOSE_TRAY_ON_OPEN
	- compiles for the 2.0.x kernels by default (no need for -DNEW)
	
	
	This program is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 2, or (at your option)
	any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with this program; if not, write to the Free Software
	Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*/

/* Supported kernel versions:
     2.4.x
*/

#include <linux/version.h>


/* Do we use the Uniform CDROM Driver? */
/* I suppose it's possible with the 2.0.x kernel, too */

#include <linux/fs.h>
#include <linux/errno.h>
#include <linux/blkdev.h>
#include <linux/major.h>
#include <linux/config.h>
#include <linux/cdrom.h>
#include <linux/ioport.h>
#include <linux/mm.h>
#include <linux/timer.h>
#include <asm/semaphore.h>

#include <asm/io.h>
#include <asm/uaccess.h>

/* From timer.h (linux 2.2.0) */
#ifndef time_after
#define time_after(a,b)         ((long)(b) - (long)(a) < 0)
#define time_before(a,b)        time_after(b,a)
#define time_after_eq(a,b)      ((long)(a) - (long)(b) >= 0)
#define time_before_eq(a,b)     time_after_eq(b,a)
#endif

/* we'll use the same major number as sbpcd */
#define MAJOR_NR	MATSUSHITA_CDROM_MAJOR
#define DO_PNCD_REQUEST	do_sbpcd_request

#if defined(LINUX_12) || defined(LINUX_20)
#define copy_from_user	memcpy_fromfs
#define copy_to_user	memcpy_tofs
#endif

#include <linux/blk.h>
#include <linux/slab.h>

#ifdef MODULE
#include <linux/module.h>

#define MOD_INC_USE_COUNT
#define MOD_DEC_USE_COUNT
#endif

/*
 ***	User servicable part begins	***
 */

/*
 * if AUTOHUNT is defined, the driver will try to detect the base port
 * and drive id
 * if I/O address is given (io=0xNNN) on the command line autohunting is
 * disabled
 * (interface type is always auto-detected)
 */
#define AUTOHUNT

/*
 * base port of the interface card
 * this value is tried first during autodetection
 */
#define PNCD_BASEPORT	0x340

/*
 * jumper settable drive id, 0 in most cases
 */
#define PNCD_DRIVE_ID	0

/*
 * if PNCD_LOCK is defined, the driver tries to keep the tray locked
 * not for Uniform Driver
 */
#define PNCD_LOCK

/*
 * close tray on device open?
 * not for Uniform Driver
 */
#define CLOSE_TRAY_ON_OPEN

/*
 * invalidate buffer pages on close?
 * (if not defined it will invalidate buffer pages during
 * the pncd_open if media had changed)
 * not for Uniform Driver
 */
#define INVALIDATE_BUF_ON_CLOSE

/*
 ***	End of user servicable part	***
 */

/*
 * Max. drive id 
 */
#define	PNCD_MAX_ID	3

int pncd_ports[] = 
{ 
#ifdef PNCD_BASEPORT
	PNCD_BASEPORT,
#endif
	0x340, 0x320, 0x330, 0x360, 0x230, 0x250, 0x270, 0x290, 
	0x300, 0x310, 0x330, 0x350, 0x370, 0x240, 0x260, 
	0x630, 0x650, 0x670, 0x690, 
	0
};

/*
 * Buffer size in frames
 */
#define PNCD_BUFFER_SIZE	4
#define PNCD_READ_AHEAD	16

/*
 * time out constants
 */
#define BUSY_TO		1024
#define DRIVE_SLEEP_TO	(2 * HZ)
#define DISK_SLEEP_TO	(5 * HZ)
#define TRAY_SLEEP_TO	(12 * HZ)
#define READ_SLEEP_TO	(4 * HZ)
/* fast time out value -- useful when probing the device */
#define FAST_TO		HZ
#define TIMEOUT		1
/* don't change! */
#define	OK		0

/*
 * delay after reset command
 */
#define RESET_DELAY	(3 * HZ)

/*
 * retry constants for pncd_docmd
 */
#define RETRY_DELAY		(HZ / 2)
#define TRAY_RETRY_DELAY	2 * HZ
#define RETRY_N			40
#define TRAY_RETRY_N		4

/*
 * retry constants and macro for pncd_open
 */
#define OPEN_RETRY_N	10
#define OPEN_DELAY	HZ

/*
 * constants for pncd_tray
 */

#define TRAY_NODISK_RETRY	6
#define TRAY_NODISK_DELAY	(HZ / 4)
#define TRAY_BUSY_RETRY		30
#define TRAY_BUSY_DELAY		HZ
#define TRAY_OPEN		1
#define TRAY_CLOSE		0
#define TRAY_WAIT		1
#define TRAY_NOWAIT		0

#define OPEN_RETRYABLE(E)					\
	((E) == DST_TRAYOUT || (E) == DST_NODISK ||		\
         (E) == DST_BUSY    || (E) == DST_NOTREADY)

#define IOCTL_OK(E)						\
	((E) == DST_DISKCHANGED || (E) == DST_TRAYOUT ||	\
	 (E) == DST_NODISK      || (E) == DST_BUSY    ||	\
	 (E) == DST_AUDIOPLAY)

/*
 * command classes
 */
#define DRIVECMDCLASS	1	/* cmd involving only the drive */
#define TRAYCMDCLASS	2	/* long operations with tray (open, close) */
#define DISKCMDCLASS	3	/* audio or data operations ok */
#define DATACMDCLASS	4	/* data operations ok */
#define AUDIOCMDCLASS	5	/* audio operations ok */
#define STATUSCMDCLASS	6	/* commands returning status */

/*
 * bits in pncd.ready
 */
#define RDY_DATA	0x02	/* data arrived */
#define RDY_REPLY	0x04	/* cmd reply arrived */

/*
 * error/status codes
 * (Drive STatus codes)
 */
#define DST_OK		0x00
#define DST_SR_RETRY	0x01
#define DST_SR_EC	0x02
#define DST_NOTREADY	0x03
#define DST_HR		0x05
#define DST_SEEKNCOMPL	0x06
#define DST_DISKCHANGED	0x11
#define	DST_RESET	0x12
#define	DST_ILLCMD	0x14
#define	DST_DISKREMOVED	0x15
#define	DST_ILLREQ	0x17
#define DST_TIMEOUT	(-1)
#define DST_TRAYOUT	(-2)
#define DST_NODISK	(-3)
#define DST_AUDIOPLAY	(-4)
#define DST_BUSY	(-5)
#define DST_NOSPIN	(-6)

/*
 * command codes
 */
#define CMD_SPIN	0x02
#define CMD_STATUS	0x05
#define CMD_EJECT	0x06
#define CMD_CLOSE	0x07
#define CMD_STOP	0x08
#define CMD_SETMODE	0x09
#define CMD_LOCK	0x0c
#define CMD_PAUSE_RES	0x0d
#define CMD_PLAYMSF	0x0e
#define CMD_PLAYTI	0x0f
#define CMD_READ	0x10
#define CMD_GETERRCODE	0x82
#define CMD_VERSION	0x83
#define CMD_READVOLUME	0x84
#define CMD_READSUBQ	0x87
#define CMD_GETMCN	0x88
#define CMD_DISKINFO	0x8b
#define CMD_READTOC	0x8c
#define CMD_MULTI	0x8d

/*
 * for CMD_SPIN
 */
#define SPINUP_DELAY	(HZ / 2)

/*
 * values for CMD_SETMODE, CMD_READVOLUME
 */
#define MODE_VOL		5
#define MODE_SPEED		3
#define MODE_SETFRAMESIZE	0
#define VOL_SIZE		5
#define VOL_VALUE		2

/*
 * values for pncd_set_{audio|data}_frame_size
 */
#define AUDIO_SIZE_CONST	0x82
#define DATA_SIZE_CONST		0
#define AUDIO_SIZE_LSB		0x30
#define AUDIO_SIZE_MSB		0x09
#define DATA_SIZE_LSB		0x00
#define DATA_SIZE_MSB		0x08

/*
 * status bits
 */
#define STAT_READY	0x01
#define STAT_DOUBLE	0x02
#define STAT_LOCK	0x04
#define STAT_PLAY	0x08
#define STAT_ERROR	0x10
#define STAT_SPIN	0x20
#define STAT_DISK	0x40
#define STAT_TRAY	0x80

/*
 * for GETERRCODE cmd
 */
#define ERRSIZE		8
#define ERRBYTE		2

/*
 * for CMD_SUBQ command
 */
#define SUBQ_TYPE	1
#define SUBQ_TRACK	2
#define SUBQ_INDEX	3
#define SUBQ_ABS_M	4
#define SUBQ_ABS_S	5
#define SUBQ_ABS_F	6
#define SUBQ_REL_M	7
#define SUBQ_REL_S	8
#define SUBQ_REL_F	9
#define SUBQ_SIZE	11

/*
 * for READTOC command
 */
#define TOCBUF_SIZE	8
#define TOC_TYPE	1
#define TOC_NUM		2
#define TOC_DATAMODE	3
#define TOC_M		4
#define TOC_S		5
#define TOC_F		6
#define	TOC_SIZE	120

/*
 * for DISKINFO command
 */
#define DINFOBUF_SIZE	6
#define DINFO_TYPE	0
#define DINFO_STARTTRK	1
#define DINFO_ENDTRK	2
#define DINFO_M		3
#define DINFO_S		4
#define DINFO_F		5

/*
 * for CMD_MULTI 
 */
#define MULTI_SIZE	6
#define MULTI_FLAG	0x80
#define MULTI_B		0
#define MULTI_M		1
#define MULTI_S		2
#define MULTI_F		3

/*
 * for CMD_GETMCN
 */
#define MCNSTR_SIZE	8
#define MCN_SIZE	13
#define MCN_OK		0x80

/*
 * 2 s data offset in frames
 */
#define DATA_OFFSET	CD_MSF_OFFSET

/*
 * Data track ctrl value in the toc
 */
#define DATA_CTRL	CDROM_DATA_TRACK

/*
 * for CMD_PAUSE_RES command
 */
#define P_RESUME	0x80

/*
 * Size of a version string
 * returned by VERSION_CMD
 * and displayed
 */
#define VERSION_SIZE	10
#define VERS_DISP_SIZE	13
#define VERS_MODEL_LEN	6

/*
 *	Selection values for the SB interface
 */
#define PNCD_SEL_DATA	1
#define PNCD_SEL_STATUS	0

/*
 *	Speed values
 */
#define	PNCD_SPEED_AUTO	0xc0
#define PNCD_SPEED_1x	0x40
#define PNCD_SPEED_2x	0x80

/*
 *	Interface type values (pncd.if_type)
 */
#define PNCD_SB_IF	0
#define PNCD_NORMAL_IF	1

/*
 * Kernel sector and CD block sizes
 */
#define SECTOR_SIZE	512
#define CDBLOCK_SIZE	CD_FRAMESIZE

/*
 * Values for pncd_search_end
 */
#define SEARCH_BACK	150
#define SEARCH_N	16
#define SEARCH_ERROR(E)						\
	((E) == DST_TRAYOUT     || (E) == DST_TRAYOUT || 	\
	 (E) == DST_DISKREMOVED || (E) == DST_DISKCHANGED)

#define OUTB		outb
#define INB		inb

/*
 * Semaphore operations
 *
 * Only uniform driver allows multiple open
 */
#define SEMAPHORE(X)	struct semaphore X
#define SEMA_INIT(X)	init_MUTEX(&pncd . X)
#define DOWN(X)		down(&pncd . X)
#define UP(X)		up(&pncd . X)

/*
 * error messages
 *   > 0 returned by the drive
 *   < 0 other errors
 */
char *errstr[] = {
	"Drive not spinning",				/* -6 */
	"Drive busy",
	"Audio playing",
	"No disk in the drive",
	"Tray is out",
	"Time out (drive not responding)",
	"No error (driver error if displayed!)",	/*  0 */
	"Soft read error after retry",
	"Soft read error after error correction",
	"Drive not ready",
	"Cannot read TOC",
	"Hard read error",
	"Seek didn't complete",
	"Tracking servo failure",
	"Drive RAM error",
	"Self-test failed",
	"Focusing servo failure",
	"Spindle servo failure",
	"Data path failure",
	"Illegal logical block address",
	"Illegal field in CDB",
	"End of user encountered on this track ?",
	"Illegal data mode for this track",
	"Media changed",
	"Power-on or reset occured",
	"Drive ROM failure",
	"Illegal drive command from the host",
	"Disk removed during operation",
	"Drive hardware error",
	"Illegal request from host"
};

#define ERR_N		(sizeof(errstr) / sizeof(char *))
#define ERROFFS		6		/* error no. offset */
#define	ERRSTR(EC)	(errstr[(EC) + ERROFFS])

/*
 * minute-second-frame address type
 */
struct MSF {
	u_char m, s, f;
};

struct TOC_ENTRY {
	u_char num;
	u_char adr:4;
	u_char ctrl:4;
	u_char datamode;
	struct MSF start;
};

struct TOC {
	u_char type;
	u_char start_track;
	u_char end_track;
	int datasize;			/* size of the data track */
	struct MSF size;		/* disk size */
	struct TOC_ENTRY entry[TOC_SIZE];
};

struct SUBQ {
	u_char adr:4;
	u_char ctrl:4;
	u_char track;
	u_char index;
	struct MSF absaddr;
	struct MSF reladdr;
};

struct MULTI {
	int flag;
	struct MSF base;
};

struct PNCD_S {
	/*
	 * interface ports
	 */
	int base;		/* base port */
	int ready;
	int data;		/* data port */
	int reset;		/* reset port */
	int select;		/* selection port (for SB) */
	int enable;		/* enable port for multiple drives */

	int if_type;		/* interface type SB or normal */
	int id;			/* jumper settable drive id */

	int present;		/* drive present */
	u_char status;		/* last drive status */
	u_char prev_status;	/* previous status */
	u_char ready_value;	/* last seen ready value */
	int open_count;
	
	/*
	 * change flags; each flag is set when ``media changed'' status returned
	 */
	int changed;		/* disk changed (reset by close) */
	int change_flag;	/* flag for check_pncd_change 
				   (reset by check_pncd_change) */
	int update_toc;		/* toc needs updating (reset by pncd_readtoc) */

	int paused;		/* audio play paused */
	int locked;		/* drive is locked */

	int eject_sw;		/* eject on close */

	struct TOC toc;		/* table of contents */
	struct SUBQ subq;	/* subchannel info */
	struct MULTI multi;	/* multisession info (?) */

	int bufaddr;		/* address of first block in the buffer */
	char buffer[PNCD_BUFFER_SIZE * CDBLOCK_SIZE]; /* block buffer */

	u_char *abuf;		/* audio buffer */
	int abufsize;		/* audio buffer size */

	/* 
	 * semaphore ensuring mutual exclusion
	 */ 
	SEMAPHORE(cmd_s);
} pncd;

struct PNCD_CMD {
	u_char b1, b2, b3, b4, b5, b6;
};

#define MAX_PNCD 	4	/* for future use (only 1 drive at present) */
static int pncd_blk_size[MAX_PNCD] = { 0, 0, 0, 0 };
static int pncd_blocksizes[MAX_PNCD] = { 0, 0, 0, 0 };
static int io[MAX_PNCD] = { 0, 0, 0, 0 };	/* base addr from insmod */
static int id = 0;				/* drive id from insmod */

#ifdef	MODULE
MODULE_LICENSE("GPL");
MODULE_AUTHOR("VÖRÖSBARANYI Zoltán <pncd@vbzo.li>");
MODULE_DESCRIPTION("Panasonic CR56x CDROM driver");
MODULE_SUPPORTED_DEVICE("block-major-25");
MODULE_PARM(io, "i");
MODULE_PARM_DESC(io, "Base port address of the interface card");
MODULE_PARM(id, "i");
MODULE_PARM_DESC(id, "Jumper settable drive identifier (0-3, 0 in most cases)");
#endif

#ifdef PNCD_STAT
#define MAXTIMEOUT	10

struct PNCD_STATISTICS {
	unsigned busyloop[BUSY_TO];
	unsigned sleeploop[DISK_SLEEP_TO];
	unsigned sleeptime[DISK_SLEEP_TO];
	int timeout_index;
	unsigned timeout[MAXTIMEOUT];
	unsigned cmdretry[RETRY_N];
	unsigned totalreads;
	unsigned readcachemisses;
	unsigned readcachehits;
	unsigned delayloop;
} stat;

void printstats(void);
#endif

static void pncd_delay(unsigned int t);
static int wait_data(u_char ready_bit, int sleep_to);
static int pncd_docmd(int class, int cmd, struct PNCD_CMD *cs,
                     u_char *rbuf, int rn);

static int pncd_open(struct cdrom_device_info *cdi, int purpose);
static void pncd_release(struct cdrom_device_info *cdi);
static int pncd_drive_status(struct cdrom_device_info *cdi, int slot);
static int pncd_media_changed(struct cdrom_device_info *cdi, int disc_nr);
static int pncd_tray_move(struct cdrom_device_info *cdi, int position);
static int pncd_lock_door(struct cdrom_device_info *cdi, int lock);
static int pncd_select_speed(struct cdrom_device_info *cdi, int speed);
static int pncd_get_last_session(struct cdrom_device_info *cdi,
                                struct cdrom_multisession *cdmp);
static int pncd_get_mcn(struct cdrom_device_info *cdi, struct cdrom_mcn *mcn);
static int pncd_reset(struct cdrom_device_info *cdi);
static int pncd_audio_ioctl(struct cdrom_device_info *cdi,
                           unsigned int cmd, void *arg);
static int pncd_dev_ioctl(struct cdrom_device_info *cdi,
                         unsigned int cmd, unsigned long arg);
static void DO_PNCD_REQUEST(request_queue_t *q);

static wait_queue_head_t pncd_waitqueue;

static struct cdrom_device_ops pncd_dops = {
	open: pncd_open,
	release: pncd_release,
	drive_status: pncd_drive_status,
	media_changed: pncd_media_changed,
	tray_move: pncd_tray_move,
	lock_door: pncd_lock_door,
	select_speed: pncd_select_speed,
	get_last_session: pncd_get_last_session,
	get_mcn: pncd_get_mcn,
	reset: pncd_reset,
	audio_ioctl: pncd_audio_ioctl,
	dev_ioctl: pncd_dev_ioctl,
	capability: ( CDC_OPEN_TRAY
	            | CDC_CLOSE_TRAY
	            | CDC_LOCK
	            | CDC_SELECT_SPEED
	            | CDC_MULTI_SESSION
	            | CDC_MCN
	            | CDC_MEDIA_CHANGED
	            | CDC_PLAY_AUDIO
	            | CDC_RESET
	            | CDC_DRIVE_STATUS
	            | CDC_IOCTLS
	            ),
	n_minors: 1,
};

static struct block_device_operations pncd_bdops = {
	owner: THIS_MODULE,
	open: cdrom_open,
	release: cdrom_release,
	ioctl: cdrom_ioctl,
	check_media_change: cdrom_media_changed,
};

static struct cdrom_device_info pncd_info = {
	ops: &pncd_dops,
	speed: 2,
	capacity: 1,
	name: "pncd",
};

/*
 * Convert internal error code to system error code
 */
static int pncd_errconv(int e)
{
	switch(e) {
	case DST_OK:
		return 0;
	case DST_NODISK:
	case DST_TRAYOUT:
	case DST_DISKREMOVED:
	case DST_DISKCHANGED:
		return -ENOMEDIUM;
	default:
		return -EIO;
	}
}

/*
 * block <-> minute/second/frame conversion 
 */
static int msf2block(u_char m, u_char s, u_char f)
{
	return m * (CD_SECS * CD_FRAMES) + s * CD_FRAMES + f - DATA_OFFSET;
}

static void block2msf(u_char *m, u_char *s, u_char *f, int block)
{
	block += DATA_OFFSET;
	*f = block % CD_FRAMES;
	block = block / CD_FRAMES;
	*s = block % CD_SECS;
	*m = block / CD_SECS;
}

static inline int msf_less(u_char m1, u_char s1, u_char f1,
                           u_char m2, u_char s2, u_char f2)
{
	if (m1 == m2) {
		if (s1 == s2)
			return f1 < f2;
		else
			return s1 < s2;
	} else
		return m1 < m2;
}

static struct PNCD_CMD *mkc(struct PNCD_CMD *cs, int p1, int p2, int p3,
                                                 int p4, int p5, int p6)
{
	cs->b1 = p1;
	cs->b2 = p2;
	cs->b3 = p3;
	cs->b4 = p4;
	cs->b5 = p5;
	cs->b6 = p6;
	return cs;
}

static void clear_cmd(struct PNCD_CMD *cs)
{
	cs->b1 = 0;
	cs->b2 = 0;
	cs->b3 = 0;
	cs->b4 = 0;
	cs->b5 = 0;
	cs->b6 = 0;
}

static void pncd_cmd(int cmd, struct PNCD_CMD *cs)
{
	OUTB(cmd, pncd.base);
	OUTB(cs->b1, pncd.base);
	OUTB(cs->b2, pncd.base);
	OUTB(cs->b3, pncd.base);
	OUTB(cs->b4, pncd.base);
	OUTB(cs->b5, pncd.base);
	OUTB(cs->b6, pncd.base);
}

static void pncd_cmd0(int cmd)
{
	int i;

	OUTB(cmd, pncd.base);
	for (i = 0; i < 6; ++i)
		OUTB(0, pncd.base);
}

static void perr(int ec)
{
	printk(KERN_ERR "pncd: Error: %s.\n", ERRSTR(ec));
}

/*
 * read error code
 */
static inline int pncd_errcode(void)
{
	int i;
	char errbuf[ERRSIZE];

	pncd_cmd0(CMD_GETERRCODE);
	if (OK != wait_data(RDY_REPLY, DRIVE_SLEEP_TO))
		return DST_TIMEOUT;
	for (i = 0; i < ERRSIZE; ++i)
		errbuf[i] = INB(pncd.base);
	if (errbuf[ERRBYTE] == DST_DISKCHANGED) {
		pncd.changed = 1;
		pncd.change_flag = 1;
		pncd.update_toc = 1;
	}
	pncd.status = INB(pncd.base);
	return errbuf[ERRBYTE];
}

/*
 * read status u_char
 * 
 * returned error code depends on command class
 */
static int pncd_readstatus(int class)
{
	int errcode = DST_OK;

	if ((pncd.status = INB(pncd.base)) & STAT_ERROR)
		errcode = pncd_errcode();
	if (class != DRIVECMDCLASS && class != TRAYCMDCLASS) {
		if (!(pncd.status & STAT_TRAY))
			return DST_TRAYOUT;
		if (!(pncd.status & STAT_DISK))
			return DST_NODISK;
		if (!(pncd.status & STAT_READY))
			return DST_BUSY;
		if (class == DATACMDCLASS && pncd.status & STAT_PLAY)
			return DST_AUDIOPLAY;
		if (class == STATUSCMDCLASS && !(pncd.status & STAT_SPIN))
			return DST_NOSPIN;
	}
	return errcode;
}

static int pncd_resetcmd(void)
{
	OUTB(0, pncd.reset);
	pncd_delay(RESET_DELAY);
	pncd_cmd0(CMD_STATUS);
	if (OK != wait_data(RDY_REPLY, FAST_TO))
		return 0;
	if (DST_RESET != pncd_readstatus(DRIVECMDCLASS))
		return 0;
	return 1;
}

static inline int pncd_version(char *s)
{
	int i, e;
	char ver[VERSION_SIZE];

	e = pncd_docmd(DRIVECMDCLASS, CMD_VERSION, NULL, ver, VERSION_SIZE);
	if (e)
		return e;
	for (i = 0; i < VERS_MODEL_LEN; ++i)
		*s++ = ver[i];
	*s++ = ',';
	*s++ = ' ';
	for (; i < VERSION_SIZE; ++i)
		*s ++ = ver[i];
	*s = 0;
	return 0;
}

/*
 * Determine interface type, and return 0 on success
 */
static inline int pncd_if_type(void)
{
	u_char c;

	pncd_cmd0(CMD_STATUS);
	if (OK != wait_data(RDY_REPLY, DRIVE_SLEEP_TO))
		return 1;
	OUTB(PNCD_SEL_DATA, pncd.select);
	(void)INB(pncd.base);
	c = INB(pncd.ready) & RDY_REPLY;
	OUTB(PNCD_SEL_STATUS, pncd.select);
	(void)INB(pncd.base);
	if (c) {
		pncd.if_type = PNCD_NORMAL_IF;
		pncd.data = pncd.base + 2;
	} else {
		pncd.if_type = PNCD_SB_IF;
		pncd.data = pncd.base;
	}
#ifdef PNCD_DEBUG
	printk(KERN_DEBUG "pncd DEBUG: if_type: %d\n", pncd.if_type);
#endif
	return 0;
}

/*
 * Select the drive
 */
inline void pncd_select_drive(int id)
{
	OUTB(id, pncd.enable);
}

/*
 * check the drive
 */
int __init pncd_probe(int port, int id, char *version)
{
	int e;

	if (check_region(port, 4)) {
		printk(KERN_INFO "pncd: Ports 0x%x-0x%x already in use.\n",
		       port, port + 3);
		return 0;
	}
	pncd.base = port;
	pncd.ready = pncd.base + 1;
	pncd.select = pncd.base + 1;
	pncd.reset = pncd.base + 2;
	pncd.enable = pncd.base + 3;
	pncd.id = id;
	SEMA_INIT(cmd_s);
	/* let's assume we have only one drive */
	pncd_select_drive(pncd.id);
	/* ignore the first status... */
	(void)pncd_readstatus(DRIVECMDCLASS);
	e = pncd_readstatus(DRIVECMDCLASS);
	if (e && e != DST_RESET && e != DST_DISKCHANGED)
		return 0;
	if(pncd_if_type())
		return 0;
	if(pncd_version(version))
		return 0;
	pncd.prev_status = pncd.status;
	return 1;
}

/*
 * if there's a response from the drive read it
 */
static inline void pncd_readresp(u_char *buf, int n)
{
	int i;

	for (i = 0; i < n; ++i)
		buf[i] = INB(pncd.base);
}

/*
 * determine time out value
 */
static inline int timeout_value(int class)
{
	switch (class) {
	case DRIVECMDCLASS:
		return DRIVE_SLEEP_TO;
	case TRAYCMDCLASS:
		return TRAY_SLEEP_TO;
	default:
		return DISK_SLEEP_TO;
	}
}

/* 
 * retry command?
 */
static int retry_cmd(int e, int class)
{
	/* retryable errors: */
	switch (e) {
	case DST_SR_RETRY:
	case DST_SR_EC:
	case DST_SEEKNCOMPL:
		return 1;
	case DST_BUSY:
	case DST_NOTREADY:
	case DST_DISKCHANGED:
	case DST_TIMEOUT:
	case DST_ILLCMD:
	case DST_ILLREQ:
		switch (class) {
		case AUDIOCMDCLASS:
		case STATUSCMDCLASS:
		case DRIVECMDCLASS:
		case TRAYCMDCLASS:
			return 1;
		default:
			return 0;
		}
	default:
		return 0;
	}
}

static inline int retry_delay(int class)
{
	return class == TRAYCMDCLASS ? TRAY_RETRY_DELAY : RETRY_DELAY;
}

static inline int retry_n(int class)
{
	return class == TRAYCMDCLASS ? TRAY_RETRY_N : RETRY_N;
}

/*
 * issue a command, read response, retry
 */
static int pncd_docmd(int class, int cmd, struct PNCD_CMD *cs,
                     u_char *rbuf, int rn)
{
	int i, e;
	int delay, n;

#ifdef PNCD_DEBUG
	if (cs)
		printk(KERN_DEBUG "pncd DEBUG: docmd cmd = %02x, "
		       "%x %x %x %x %x %x\n", cmd,
		       cs->b1, cs->b2, cs->b3, cs->b4, cs->b5, cs->b6);
	else
		printk(KERN_DEBUG "pncd DEBUG: docmd cmd = %02x, 0 0 0 0 0 0\n",
		       cmd);
#endif
	e = 0;	/* make gcc happy */
	n = retry_n(class);
	delay = retry_delay(class);
	for (i = 1; i <= n; ++i) {
		/* issue command */
		if (cmd == CMD_STOP)
			OUTB(cmd, pncd.base);
		else {
			if (cs)
				pncd_cmd(cmd, cs);
			else
				pncd_cmd0(cmd);
		}
		/* wait for status reply */
		if (wait_data(RDY_REPLY, timeout_value(class)) == TIMEOUT) {
			e = DST_TIMEOUT;
		} else {
			/* read command response if it is required */
			if (rbuf)
				pncd_readresp(rbuf, rn);
			/* read status (it's the last u_char in the response) */
			e = pncd_readstatus(class);
		}
		if (e == 0) {
#ifdef PNCD_STAT
			if (i > 1)
				++stat.cmdretry[i - 1];
#endif
#if 0
			printk(KERN_DEBUG "pncd DEBUG: docmd retries = %d.\n",
			       i - 1);
#endif
			return 0;
		}
		if (retry_cmd(e, class)) {
			pncd_delay(delay);
#ifdef PNCD_DEBUG
			printk(KERN_DEBUG "pncd DEBUG: Error: %s (0x%02x), "
			       "retrying.\n", ERRSTR(e), e);
#endif
		} else
			break;
	}
#ifdef PNCD_DEBUG
	printk(KERN_DEBUG "pncd DEBUG: Error: %s (0x%02x), retries = %d.\n",
	       ERRSTR(e), e, i - 1);
#endif
#ifdef PNCD_STAT
	if (i > 1)
		++stat.cmdretry[i - 1];
#endif
	return e;
}

/*
 * read data -- n * blksize u_chars 
 */
static void read_data(int n, char *buf, int blksize)
{
	int j;
	char *p;

	for (j = 0, p = buf; j < n; ++j, p += blksize) {
		if (wait_data(RDY_DATA | RDY_REPLY, DISK_SLEEP_TO) == TIMEOUT)
			return;
		/* there is an error if reply came first */
		if ((pncd.ready_value & RDY_REPLY) == 0)
			return;
		if (pncd.if_type == PNCD_SB_IF)
			OUTB(PNCD_SEL_DATA, pncd.select);
		insb(pncd.data, p, blksize);
		if (pncd.if_type == PNCD_SB_IF)
			OUTB(PNCD_SEL_STATUS, pncd.select);
	}
}

/*
 * read n blocks from the drive
 */
static int pncd_readcmd(u_char m, u_char s, u_char f,
                       char *buf, int n, int blksize)
{
	struct PNCD_CMD cs;
	int i, e;
#ifdef PNCD_DEBUG
	printk(KERN_DEBUG "pncd DEBUG: read %d blocks m/s/f = %d/%d/%d\n",
               n, m, s, f);
#endif
	for (i = 1; i <= RETRY_N; ++i) {
		/* issue read command */
		pncd_cmd(CMD_READ, mkc(&cs, m, s, f, 0, 0, n));
		/* read data -- n * blksize u_chars */
		read_data(n, buf, blksize);
		/* wait for status reply */
		if (wait_data(RDY_REPLY, READ_SLEEP_TO) == TIMEOUT) {
			e = DST_TIMEOUT;
		} else {
			e = pncd_readstatus(DATACMDCLASS);
		}
		if (e == 0) {
#ifdef PNCD_STAT
			if (i > 1)
				++stat.cmdretry[i - 1];
#endif
			return 0;
		}
		if (retry_cmd(e, DATACMDCLASS))
			pncd_delay(RETRY_DELAY);
		else
			break;
	}
#ifdef PNCD_DEBUG
	printk(KERN_DEBUG "pncd DEBUG: (readcmd) Error: %s (0x%02x), "
	       "retries = %d.\n", ERRSTR(e), e, i - 1);
	printk(KERN_DEBUG "pncd DEBUG: cmd = %02x, %d %d %d %d %d %d\n",
	       CMD_READ, cs.b1, cs.b2, cs.b3, cs.b4, cs.b5, cs.b6);
#endif
#ifdef PNCD_STAT
	if (i > 1)
		++stat.cmdretry[i - 1];
#endif
	return e;
}

static inline int pncd_getstatd(void)
{
	return pncd_docmd(DISKCMDCLASS, CMD_STATUS, NULL, NULL, 0);
}

static inline int pncd_getstatu(void)
{
	return pncd_docmd(STATUSCMDCLASS, CMD_STATUS, NULL, NULL, 0);
}

static inline int pncd_getstat(void)
{
	return pncd_docmd(DRIVECMDCLASS, CMD_STATUS, NULL, NULL, 0);
}

static inline int pncd_spin(void)
{
	return pncd_docmd(DISKCMDCLASS, CMD_SPIN, NULL, NULL, 0);
}

static inline int pncd_stop(void)
{
	return pncd_docmd(DRIVECMDCLASS, CMD_STOP, NULL, NULL, 0);
}

static inline int pncd_pause(void)
{
	int e;

	if ((pncd.status & STAT_PLAY) && !pncd.paused) {
		e = pncd_docmd(AUDIOCMDCLASS, CMD_PAUSE_RES, NULL, NULL, 0);
		if (e)
			return e;
		pncd.paused = 1;
	}
	return 0;
}

static inline int pncd_resume(void)
{
	int e;
	struct PNCD_CMD cmd;

	if ((pncd.status & STAT_PLAY) && pncd.paused) {
		clear_cmd(&cmd);
		cmd.b1 = P_RESUME;
		e = pncd_docmd(AUDIOCMDCLASS, CMD_PAUSE_RES, &cmd, NULL, 0);
		pncd.paused = 0;
		if (e)
			return e;
	}
	return 0;
}

static inline int pncd_playti(u_char from_t, u_char from_i,
                             u_char to_t, u_char to_i)
{
	struct PNCD_CMD cmd;

	pncd.paused = 0;
	return pncd_docmd(AUDIOCMDCLASS, CMD_PLAYTI,
	                 mkc(&cmd, from_t, from_i, to_t, to_i, 0, 0), NULL, 0);
}

static inline int pncd_playmsf(u_char from_m, u_char from_s, u_char from_f,
                              u_char to_m, u_char to_s, u_char to_f)
{
	struct PNCD_CMD cmd;
	
	pncd.paused = 0;
	return pncd_docmd(AUDIOCMDCLASS, CMD_PLAYMSF,
	                 mkc(&cmd, from_m, from_s, from_f, to_m, to_s, to_f),
	                 NULL, 0);
}

static int pncd_lock(void)
{
	struct PNCD_CMD cmd;

	clear_cmd(&cmd);
	cmd.b1 = 1;
	return pncd_docmd(DRIVECMDCLASS, CMD_LOCK, &cmd, NULL, 0);
}

static int pncd_unlock(void)
{
	return pncd_docmd(DRIVECMDCLASS, CMD_LOCK, NULL, NULL, 0);
}

static int pncd_tray(int command, int wait)
{
	int e, i, cmd;

	cmd = command == TRAY_OPEN ? CMD_EJECT : CMD_CLOSE;
	e = pncd_docmd(TRAYCMDCLASS, cmd, NULL, NULL, 0);
	if (e)
		return e;
	/*
	 * We have to wait until the drive becomes ready, or,
	 * until it becomes certain that no disk is in
         */
	if (command == TRAY_CLOSE && wait == TRAY_WAIT) {
		/* wait for no DST_NODISK */
		for (i = 0; i < TRAY_NODISK_RETRY; ++i) {
			e = pncd_getstatd();
			if (e != DST_NODISK) {
#ifdef PNCD_DEBUG
				printk(KERN_DEBUG "pncd DEBUG: tray-x: %d\n",i);
#endif
				break;
			}
			pncd_delay(TRAY_NODISK_DELAY);
		}
		if (e == DST_NODISK) {
#ifdef PNCD_DEBUG
			printk(KERN_DEBUG "pncd DEBUG: tray move ended, "
			       "no disk.\n");
#endif
			return 0;
		}
		/* wait for no DST_BUSY */
		for (i = 0; i < TRAY_BUSY_RETRY; ++i) {
			e = pncd_getstatd();
			if (e != DST_BUSY)
				break;
			pncd_delay(TRAY_BUSY_DELAY);
		}
		/* should we report error here, on timeout? */
	}
#ifdef PNCD_DEBUG
	printk(KERN_DEBUG "pncd DEBUG: tray move ended.\n");
#endif
	return 0;
}

static inline int pncd_volume(u_char vol)
{
	struct PNCD_CMD cmd;

	return pncd_docmd(DRIVECMDCLASS, CMD_SETMODE,
	                  mkc(&cmd, MODE_VOL, 0, 1, vol, 2, vol), NULL, 0);
}

static inline int pncd_readvolume(u_char *volp)
{
	int e;
	struct PNCD_CMD cmd;
	u_char buf[VOL_SIZE];

	clear_cmd(&cmd);
	cmd.b1 = MODE_VOL;
	e = pncd_docmd(DRIVECMDCLASS, CMD_READVOLUME, &cmd, buf, VOL_SIZE);
	if (e)
		return e;
	*volp = buf[VOL_VALUE];
	return 0;
}

static inline int pncd_readsubq(void)
{
	int e;
	u_char buf[SUBQ_SIZE];

	e = pncd_docmd(DISKCMDCLASS, CMD_READSUBQ, NULL, buf, SUBQ_SIZE);
	if (e)
		return e;
	pncd.subq.adr = (buf[SUBQ_TYPE] & 0xf0) >> 4;
	pncd.subq.ctrl = buf[SUBQ_TYPE] & 0x0f;
	pncd.subq.track = buf[SUBQ_TRACK];
	pncd.subq.index = buf[SUBQ_INDEX];
	pncd.subq.absaddr.m = buf[SUBQ_ABS_M];
	pncd.subq.absaddr.s = buf[SUBQ_ABS_S];
	pncd.subq.absaddr.f = buf[SUBQ_ABS_F];
	pncd.subq.reladdr.m = buf[SUBQ_REL_M];
	pncd.subq.reladdr.s = buf[SUBQ_REL_S];
	pncd.subq.reladdr.f = buf[SUBQ_REL_F];
	return 0;
}

/*
 * read multisession info
 */
static inline int pncd_multi(void)
{
	int e;
	u_char buf[MULTI_SIZE];

	e = pncd_docmd(DISKCMDCLASS, CMD_MULTI, NULL, buf, MULTI_SIZE);
	if (e)
		return e;
	if (buf[MULTI_B] & MULTI_FLAG) {
		pncd.multi.flag = 1;
		pncd.multi.base.m = buf[MULTI_M];
		pncd.multi.base.s = buf[MULTI_S];
		pncd.multi.base.f = buf[MULTI_F];
	} else
		pncd.multi.flag = 0;
	return 0;
}

static inline int pncd_set_audio_frame_size(void)
{
	struct PNCD_CMD cmd;

	cmd.b1 = MODE_SETFRAMESIZE;
	cmd.b2 = AUDIO_SIZE_CONST;
	cmd.b3 = AUDIO_SIZE_MSB;
	cmd.b4 = AUDIO_SIZE_LSB;
	cmd.b5 = 0;
	cmd.b6 = 0;
	return pncd_docmd(DRIVECMDCLASS, CMD_SETMODE, &cmd, NULL, 0);
}

static int pncd_set_data_frame_size(void)
{
	struct PNCD_CMD cmd;

	cmd.b1 = MODE_SETFRAMESIZE;
	cmd.b2 = DATA_SIZE_CONST;
	cmd.b3 = DATA_SIZE_MSB;
	cmd.b4 = DATA_SIZE_LSB;
	cmd.b5 = 0;
	cmd.b6 = 0;
	return pncd_docmd(DRIVECMDCLASS, CMD_SETMODE, &cmd, NULL, 0);
}

/*
 * read MCN (UPC)
 */
static inline int pncd_mcn(u_char *mcnstr)
{
	return pncd_docmd(DISKCMDCLASS, CMD_GETMCN, NULL, mcnstr, MCNSTR_SIZE);
}

/*
 * read toc header
 */
static inline int pncd_diskinfo(void)
{
	int e;
	u_char buf[DINFOBUF_SIZE];

	if ((e = pncd_docmd(DISKCMDCLASS, CMD_DISKINFO, NULL,
	                   buf, DINFOBUF_SIZE)))
		return e;
	pncd.toc.type = buf[DINFO_TYPE];
	pncd.toc.start_track = buf[DINFO_STARTTRK];
	pncd.toc.end_track = buf[DINFO_ENDTRK];
	pncd.toc.size.m = buf[DINFO_M];
	pncd.toc.size.s = buf[DINFO_S];
	pncd.toc.size.f = buf[DINFO_F];
#ifdef PNCD_DEBUG
	printk(KERN_DEBUG "pncd DEBUG: Diskinfo: type:%x, start:%d, end:%d, "
	       "m/s/f:%d/%d/%d\n",
		pncd.toc.type,
		pncd.toc.start_track,
		pncd.toc.end_track,
		pncd.toc.size.m,
		pncd.toc.size.s,
		pncd.toc.size.f);
#endif
	return 0;
}

/*
 * This routine tries to search for the end of the data track.
 * Returns the real size for the data track.
 */
static inline int pncd_search_end(int *newsize, int size)
{
	u_char m, s, f;
	int block, e, j;

	*newsize = 0;
	/* step back SEARCH_BACK blocks until a good block is reached */
	for (block = size - 1, j = 0, e = -1; block >= 0 && j < SEARCH_N;
	     block -= SEARCH_BACK, ++j) {
		block2msf(&m, &s, &f, block);
		e = pncd_readcmd(m, s, f, pncd.buffer, 1, CDBLOCK_SIZE);
#ifdef PNCD_DEBUG
		printk(KERN_DEBUG "pncd DEBUG: search: m/s/f=%d/%d/%d, %s\n",
		       m, s, f, e ? "bad" : "good");
#endif
		if (SEARCH_ERROR(e))
			return e;
		if (!e)
			break;
	}
	/* give up if good block not found */
	if (e)
		return 0;
	/* now search forward for the first readable block */
	for (++block; !e && block < size; ++block) {
		block2msf(&m, &s, &f, block);
		e = pncd_readcmd(m, s, f, pncd.buffer, 1, CDBLOCK_SIZE);
#ifdef PNCD_DEBUG
		printk(KERN_DEBUG "pncd DEBUG: search: m/s/f=%d/%d/%d, %s\n",
		       m, s, f, e ? "bad" : "good");
#endif
		if (SEARCH_ERROR(e))
			return e;
	}
	/* return the size (not the last good block) */
	*newsize = block - 1;
	return 0;
}

/*
 * read table of contents
 */
static int pncd_readtoc(void)
{
	int e, i, newsize;
	struct PNCD_CMD cmd;
	struct MSF msf;
	u_char buf[TOCBUF_SIZE];

	/* read toc header */
	if ((e = pncd_diskinfo()))
		return e;
	/* read multisession info */
	if ((e = pncd_multi()))
		return e;
	clear_cmd(&cmd);
	for (i = pncd.toc.start_track; i <= pncd.toc.end_track; ++i) {
		cmd.b2 = i;
		e = pncd_docmd(DISKCMDCLASS, CMD_READTOC, &cmd,
		               buf, TOCBUF_SIZE);
		if (e)
			return e;
		pncd.toc.entry[i].num = buf[TOC_NUM];
		pncd.toc.entry[i].adr = (buf[TOC_TYPE] & 0xf0) >> 4;
		pncd.toc.entry[i].ctrl = buf[TOC_TYPE] & 0x0f;
		pncd.toc.entry[i].datamode = buf[TOC_DATAMODE];
		pncd.toc.entry[i].start.m = buf[TOC_M];
		pncd.toc.entry[i].start.s = buf[TOC_S];
		pncd.toc.entry[i].start.f = buf[TOC_F];
	}
	/* now fill the "lead-out" record */
	pncd.toc.entry[i].num = CDROM_LEADOUT;
	pncd.toc.entry[i].datamode = 0;
	pncd.toc.entry[i].adr = 0;
	pncd.toc.entry[i].ctrl = 0;
	pncd.toc.entry[i].start = pncd.toc.size;
	/* determine size of the data track */
	if (pncd.toc.entry[pncd.toc.start_track].ctrl & DATA_CTRL) {
		msf = pncd.toc.entry[pncd.toc.start_track + 1].start;
		pncd.toc.datasize = msf2block(msf.m, msf.s, msf.f);
	} else
		pncd.toc.datasize = 0;
	pncd.update_toc = 0;
	if ((e = pncd_search_end(&newsize, pncd.toc.datasize)))
		return e;
	pncd.toc.datasize = newsize;
	/* device size in 1K blocks */
	blk_size[MAJOR_NR][0] = pncd.toc.datasize * 2;
#ifdef PNCD_DEBUG
	for (i = pncd.toc.start_track; i <= pncd.toc.end_track + 1; ++i) {
		printk(KERN_DEBUG "pncd DEBUG:   %d: num:%d, adr:%x, ctrl:%x, "
		       "form:%x, m/s/f:%d/%d/%d\n",
			i,
			pncd.toc.entry[i].num,
			pncd.toc.entry[i].adr,
			pncd.toc.entry[i].ctrl,
			pncd.toc.entry[i].datamode,
			pncd.toc.entry[i].start.m,
			pncd.toc.entry[i].start.s,
			pncd.toc.entry[i].start.f);
	}
	printk(KERN_DEBUG "pncd DEBUG: datasize = %d\n", pncd.toc.datasize);
	printk(KERN_DEBUG "pncd DEBUG: multi = %d", pncd.multi.flag);
	if (pncd.multi.flag)
		printk(", base = %d/%d/%d\n", 
		       pncd.multi.base.m,
		       pncd.multi.base.s,
		       pncd.multi.base.f);
	else
		printk("\n");
#endif
	return 0;
}

int __init pncd_hunt(char *version)
{
	int i, j;

	for (i = 0; pncd_ports[i]; ++i) {
		for (j = 0; j <= PNCD_MAX_ID; ++j) {
			if (pncd_probe(pncd_ports[i], j, version))
				return 1;
			else
				printk(KERN_INFO "pncd: no Panasonic CD-ROM "
				       "found on address 0x%x, id %d\n",
				        pncd_ports[i], j);
		}
	}
	return 0;
}

#ifdef MODULE
int __pncd_init(void)
#else
int __init pncd_init(void)
#endif
{
	int i;
	char version[VERS_DISP_SIZE];

#ifdef PNCD_DEBUG
	printk(KERN_DEBUG "pncd DEBUG: io[0] = 0x%x\n", io[0]);
#endif
	init_waitqueue_head(&pncd_waitqueue);
#ifdef MODULE
	if (io[0])
		i = pncd_probe(io[0], id, version);
	else
#endif
#ifdef AUTOHUNT
		i = pncd_hunt(version);
#else
		i = pncd_probe(PNCD_BASEPORT, PNCD_DRIVE_ID, version);
#endif
	if (!i) {
		printk(KERN_ERR "pncd: No Panasonic CR56x CD-ROM device "
		       "found.\n");
#ifdef MODULE
		return -EIO;
#else
		return 0;
#endif
	}
	if(register_blkdev(MAJOR_NR, "pncd", &pncd_bdops)) {
		printk(KERN_ERR "pncd: Unable to get major %d for "
		       "CD-ROM driver.\n", MAJOR_NR);
#ifdef MODULE
		return -EIO;
#else
		return 0;
#endif
	}
	pncd_info.dev = MKDEV(MAJOR_NR, 0);
	if (register_cdrom(&pncd_info) != 0) {
		printk(KERN_ERR 
		       "pncd: Unable to register for cdrom major %d.\n",
		       MAJOR_NR);
#ifdef MODULE
		return -EIO;
#else
		return 0;
#endif
	}
#ifdef MODULE
	printk(KERN_INFO "pncd: %s, addr = %03x, id = %d, %s interface; "
	       "module version " PNCD_VERSION " inserted.\n", 
	       version, pncd.base, pncd.id, 
	       pncd.if_type == PNCD_SB_IF ? "SB" : "normal");
#else
	printk(KERN_INFO "pncd: %s, addr = %03x, id = %d, %s interface; "
	       "driver version " PNCD_VERSION " installed.\n", 
	       version, pncd.base, pncd.id, 
	       pncd.if_type == PNCD_SB_IF ? "SB" : "normal");
#endif
	devfs_register(NULL, "pncd", DEVFS_FL_DEFAULT, MAJOR_NR, 0,
	               S_IFBLK | S_IRUGO | S_IWUGO, &pncd_bdops, NULL);
	blk_init_queue(BLK_DEFAULT_QUEUE(MAJOR_NR), DO_PNCD_REQUEST);
	read_ahead[MAJOR_NR] = PNCD_READ_AHEAD;
	request_region(pncd.base, 4, "pncd");
	pncd.present = 1;
	pncd.change_flag = 0;
	pncd.open_count = 0;
	pncd.paused = 0;
	pncd.eject_sw = 0;
	pncd.update_toc = 1;
	blk_size[MAJOR_NR] = pncd_blk_size;
	blksize_size[MAJOR_NR] = pncd_blocksizes;
	return 0;
}	

#ifdef MODULE
void pncd_exit(void)
{
	if (MOD_IN_USE) {
		printk(KERN_NOTICE "pncd: Module in use, cannot remove.\n");
		return;
	}
	release_region(pncd.base, 4);
	if (unregister_cdrom(&pncd_info) != 0) {
		printk(KERN_ERR "pncd: Unable to unregister for "
		       "cdrom major %d.\n", MAJOR_NR);
		return;
	}
	if (unregister_blkdev(MAJOR_NR, "pncd") == -EINVAL) {
		printk(KERN_ERR "pncd: Cannot unregister block device.\n");
		return;
	}
	blk_size[MAJOR_NR] = NULL;
	printk(KERN_INFO "pncd: Module removed.\n");
}
#endif

#ifdef MODULE
module_init(__pncd_init);
module_exit(pncd_exit)
#endif

/*
 * read in data block into the buffer if not present
 * and set pncd.bufaddr
 */
static inline int pncd_readblock(char **bufp, int block)
{
	u_char m, s, f;
	int e, n;

	/* if block is not in the buffer...  */
	if (block < pncd.bufaddr || block >= pncd.bufaddr + PNCD_BUFFER_SIZE) {
#ifdef PNCD_STAT
		++stat.readcachemisses;
#endif
		block2msf(&m, &s, &f, block);
		/* if read ahead runs off trim n */
		if (block + PNCD_BUFFER_SIZE >= pncd.toc.datasize)
			n = pncd.toc.datasize - block;
		else
			n = PNCD_BUFFER_SIZE;
		e = pncd_readcmd(m, s, f, pncd.buffer, n, CDBLOCK_SIZE);
		pncd.bufaddr = e ? -PNCD_BUFFER_SIZE : block;
		if (e)
			return e;
	}
#ifdef PNCD_STAT
	else
		++stat.readcachehits;
#endif
	*bufp = pncd.buffer + (block - pncd.bufaddr) * CDBLOCK_SIZE;
	return 0;
}

/*
 * read nsect sectors into the buf
 */
static inline int pncd_read(unsigned nsect, unsigned sectaddr, char *buf)
{
	int block, startblock, endblock;
	int startoffset, endoffset, transfersize;
	int e;
	char *buffer;

	/*
	 * note that we have to cope with the
	 * different block sizes between the kernel and the device
	 */
#ifdef PNCD_STAT
	++stat.totalreads;
#endif
	DOWN(cmd_s);
	startblock = sectaddr >> 2;
	endblock = (sectaddr + nsect - 1) >> 2;
	if (endblock >= pncd.toc.datasize) {
		UP(cmd_s);
		return 0;
	}
	for (block = startblock; block <= endblock; ++block) {
		e = pncd_readblock(&buffer, block);
		if (e) {
			perr(e);
			UP(cmd_s);
			return 0;
		}
		/* start and end offset within the block */
		startoffset = block == startblock ?
			      sectaddr & 3 :
			      0;
		endoffset = block == endblock ?
		            ((sectaddr + nsect - 1) & 3) + 1:
			    4;
		transfersize = (endoffset - startoffset) * SECTOR_SIZE;
		memcpy(buf, buffer + startoffset * SECTOR_SIZE, transfersize);
		buf += transfersize;
	}
	UP(cmd_s);
	return 1;
}

/*
 * strategy routine called by the kernel
 */
static void DO_PNCD_REQUEST(request_queue_t *q)
{
	int r;

	for (;;) {
		INIT_REQUEST;
		switch(CURRENT->cmd) {
		case READ:
			if (pncd.changed) {
				printk(KERN_CRIT "pncd: Media changed, I/O "
				       "disabled until device is closed.\n");
				end_request(0);
				continue;
			}
			r = pncd_read(CURRENT->nr_sectors,
			             CURRENT->sector,
			             CURRENT->buffer);
			end_request(r);
			break;
		case WRITE:
			end_request(0);
			break;
		default:
			panic("pncd: Unknown command in do_pncd_request");
			break;
		}
	}
}

/* delay t jiffies */
static void pncd_delay(unsigned int t)
{
	sleep_on_timeout(&pncd_waitqueue, t);
}

/* wait for given flags in the pncd.ready port */
static int wait_data(u_char mask, int sleep_to)
{
	int i;
#ifdef PNCD_STAT
	unsigned start;
#endif

	/* first fast busy loop... */
	for (i = 0; i < BUSY_TO; ++i) {
		if (((pncd.ready_value = INB(pncd.ready)) & mask) != mask) {
#ifdef PNCD_STAT
			++stat.busyloop[i];
#endif
			return OK;
		}
	}
	/* ... then slower with delays */
#ifdef PNCD_STAT
	start = jiffies;
#endif
	for (i = 0; i < sleep_to; ++i) {
		if (((pncd.ready_value = INB(pncd.ready)) & mask) != mask) {
#ifdef PNCD_STAT
			stat.sleeptime[i] = jiffies - start;
			++stat.sleeploop[i];
#endif
			return OK;
		}
		pncd_delay(1);
	}
#ifdef PNCD_STAT
	if (stat.timeout_index < MAXTIMEOUT)
		stat.timeout[stat.timeout_index++] = jiffies - start;
#endif
	return TIMEOUT;
}

static int pncd_init_ioctl(unsigned int cmd)
{
	int e, strict;

	switch (cmd) {
	case CDROMSUBCHNL:
	case CDROMVOLCTRL:
	case CDROMVOLREAD:
		strict = 0;
		break;
	default:
		strict = 1;
	}
	/*
	 * read status and update toc if necessary
	 */
	e = pncd_getstatd();
	if (e && (strict || !IOCTL_OK(e)))
		return e;
	if ((e == DST_OK || e == DST_DISKCHANGED) && pncd.update_toc) {
		if ((e = pncd_readtoc()))
			return e;
	}
	return 0;
}

static int pncd_dev_ioctl(struct cdrom_device_info *cdi,
                         unsigned int cmd, unsigned long arg)
{
	int e;

#ifdef PNCD_DEBUG
	printk(KERN_DEBUG "pncd DEBUG: entering dev_ioctl\n");
#endif
	DOWN(cmd_s);
	e = pncd_init_ioctl(cmd);
	if (e) {
		perr(e);
		UP(cmd_s);
		return pncd_errconv(e);
	}

	switch (cmd) {
	case CDROMAUDIOBUFSIZ:
#ifdef PNCD_DEBUG
		printk(KERN_DEBUG "pncd DEBUG: dev_ioctl CDROMAUDIOBUFSIZ %d\n",
                                  (int)arg);
#endif
		if ((int)arg < 0) {
			UP(cmd_s);
			return -EINVAL;
		}
		if (pncd.abufsize != arg) {
			if (pncd.abuf) {
				vfree(pncd.abuf);
				pncd.abuf = NULL;
				pncd.abufsize = 0;
			}
			if (arg != 0) {
				pncd.abuf = vmalloc(arg * CD_FRAMESIZE_RAW);
				pncd.abufsize = pncd.abuf ? arg : 0;
			}
		}
		UP(cmd_s);
		return pncd.abufsize;
	case CDROMREADAUDIO: {
		u_char m, s, f;
		int i, lba, size;
		u_char *p;
		struct cdrom_read_audio ra;

#ifdef PNCD_DEBUG
		printk(KERN_DEBUG "pncd DEBUG: dev_ioctl CDROMREADAUDIO\n");
#endif
		e = verify_area(VERIFY_READ, (void *)arg, sizeof(ra));
		if (e) {
			UP(cmd_s);
			return e;
		}
		if (
		    copy_from_user(&ra, (void *)arg, sizeof(ra))
		                                                ) {
			UP(cmd_s);
			return -EFAULT;
		}
		e = verify_area(VERIFY_WRITE, (void *)ra.buf,
		                ra.nframes * CD_FRAMESIZE_RAW);
		if (e) {
			UP(cmd_s);
			return e;
		}
		switch (ra.addr_format) {
		case CDROM_MSF:
			m = ra.addr.msf.minute;
			s = ra.addr.msf.second;
			f = ra.addr.msf.frame;
			lba = msf2block(m, s, f);
			break;
		case CDROM_LBA:
			lba = ra.addr.lba;
			block2msf(&m, &s, &f, lba);
			break;
		default:
			UP(cmd_s);
			return -EINVAL;
		}
		size = msf2block(pncd.toc.size.m,
		                 pncd.toc.size.s,
		                 pncd.toc.size.f);
#ifdef PNCD_DEBUG
		printk(KERN_DEBUG "pncd DEBUG: disk size %d, msf = %d/%d/%d, "
		                  "lba %d, n %d\n", size, (int)m, (int)s,
		                  (int)f, lba, ra.nframes);
#endif
		/* lba + ra.nframes > size would be more correct, but 
		   sometimes cdda2wav issues reads past end of track... 
		   (and tries to trash cache with lba = -1) */
		if (lba < 0 || lba > size) {
			UP(cmd_s);
			return -EINVAL;
		}
		if (pncd.abuf == NULL) {
			pncd.abuf = vmalloc(CD_FRAMESIZE_RAW);
			pncd.abufsize = pncd.abuf ? 1 : 0;
			if (pncd.abuf == NULL) {
				UP(cmd_s);
				return -ENOMEM;
			}
		}
		e = pncd_set_audio_frame_size();
		if (e) {
			perr(e);
			UP(cmd_s);
			return pncd_errconv(e);
		}
		p = ra.buf;
		size = pncd.abufsize;
		for (i = 0; i < ra.nframes; i += pncd.abufsize) {
			if(size > ra.nframes - i)
				size = ra.nframes - i;
			e = pncd_readcmd(m, s, f,
			                pncd.abuf, size, CD_FRAMESIZE_RAW);
			if (e) {
				(void)pncd_set_data_frame_size();
				perr(e);
				UP(cmd_s);
				return pncd_errconv(e);
			}
			if (
			    copy_to_user(p, pncd.abuf, size*CD_FRAMESIZE_RAW)
			                                                    ) {
				(void)pncd_set_data_frame_size();
				UP(cmd_s);
				return -EFAULT;
			}
			p += size * CD_FRAMESIZE_RAW;
			lba += size;
			block2msf(&m, &s, &f, lba);
		}
		e = pncd_set_data_frame_size();
		if (e) {
			perr(e);
			UP(cmd_s);
			return pncd_errconv(e);
		}
		break;
	}
	default:
#ifdef PNCD_DEBUG
		printk(KERN_DEBUG "pncd DEBUG: Got unknown ioctl call: 0x%x\n",
		       cmd);
#endif
		UP(cmd_s);
		return -ENOSYS;
	}
	UP(cmd_s);
	return 0;
}

#ifdef PNCD_STAT
void printstats(void)
{
	int i, j, s, inc;

	printk(KERN_DEBUG "pncd STATS: Statistics:\n");
	printk(KERN_DEBUG "pncd STATS:   totalreads = %u, readcachehits = %u, "
	       "readcachemisses = %u\n",
	       stat.totalreads, stat.readcachehits, stat.readcachemisses);
	for (i = 0, inc = BUSY_TO / 16 > 0 ? BUSY_TO / 16 : 1;
	     i < BUSY_TO; i += inc) {
		for (j = i, s = 0; j < i + inc; ++j)
			s += stat.busyloop[j];
		if (s)
			printk(KERN_DEBUG "pncd STATS:   busyloop[%d - %d] = "
			       "%d\n", i, i + inc - 1, s);
	}
	for (i = 0; i < DISK_SLEEP_TO; i += 1) {
		s = stat.sleeploop[i];
		if (s) {
			printk(KERN_DEBUG "pncd STATS:   sleeploop[%d] = %d, ",
			       i, s);
			printk("sleeptime[%d] = %d\n", i, stat.sleeptime[i]);
		}
	}
	for (i = 0; i < RETRY_N; i += 1) {
		s = stat.cmdretry[i];
		if (s)
			printk(KERN_DEBUG "pncd STATS:   cmdretry[%d] = %d\n",
			       i, s);
	}
	for (i = 0; i < stat.timeout_index; i += 1) {
		s = stat.timeout[i];
		printk(KERN_DEBUG "pncd STATS:   timeout[%d] = %d\n", i, s);
	}
	printk(KERN_DEBUG "pncd STATS:   delayloop = %d\n", stat.delayloop);
}
#endif /* PNCD_STAT */

static int pncd_open(struct cdrom_device_info *cdi, int open_for_ioctls)
{
	int i, e;

	e = 0; /* keep GHC happy */
#ifdef PNCD_DEBUG
	printk(KERN_DEBUG "pncd DEBUG: pncd_open, open_for_ioctls = %d\n",
	       open_for_ioctls);
#endif
	DOWN(cmd_s);
	MOD_INC_USE_COUNT;
	if (!pncd.present) {
		MOD_DEC_USE_COUNT;
		UP(cmd_s);
		return -ENXIO;
	}
	if (!open_for_ioctls) {
		for (i = 0; i < OPEN_RETRY_N; ++i) {
			e = pncd_getstatd();
			if (e == DST_DISKCHANGED)
				e = 0;
			if (e && OPEN_RETRYABLE(e))
				pncd_delay(OPEN_DELAY);
			else
				break;
		}
		if (e) {
			perr(e);
			MOD_DEC_USE_COUNT;
			UP(cmd_s);
			return pncd_errconv(e);
		}
		if (pncd.update_toc) {
			if ((e = pncd_readtoc())) {
				perr(e);
				MOD_DEC_USE_COUNT;
				UP(cmd_s);
				return pncd_errconv(e);
			}
		}
		/* initialise/invalidate buffer */
		pncd.bufaddr = -PNCD_BUFFER_SIZE;
	}
#ifdef PNCD_STAT
	memset(&stat, 0, sizeof stat);
#endif
	pncd.changed = 0;
	++pncd.open_count;
	UP(cmd_s);
	return 0;
}

static void pncd_release(struct cdrom_device_info * pncdi)
{
#ifdef PNCD_DEBUG
	printk(KERN_DEBUG "pncd DEBUG: pncd_release\n");
#endif
	--pncd.open_count;
	MOD_DEC_USE_COUNT;
#ifdef PNCD_STAT
	printstats();
#endif
}

static int pncd_drive_status(struct cdrom_device_info *cdi, int slot)
{
	int s, r, i;

	DOWN(cmd_s);
	/* Maybe the tray is just closed. */
	/* wait for no DST_NODISK */
	for (i = 0; i < TRAY_NODISK_RETRY; ++i) {
		s = pncd_getstatu();
		if (s != DST_NODISK) {
#ifdef PNCD_DEBUG
			printk(KERN_DEBUG "pncd DEBUG: drive-x: %d\n", i);
#endif
			if (s == DST_NOSPIN) {
				(void)pncd_spin();
				pncd_delay(SPINUP_DELAY);
				s = pncd_getstatu();
			}
			break;
		}
		pncd_delay(TRAY_NODISK_DELAY);
	}
#ifdef PNCD_DEBUG
	printk(KERN_DEBUG "pncd DEBUG: pncd_drive_status, getstats = %d\n", s);
#endif
	switch(s) {
	case DST_TRAYOUT:
		r = CDS_TRAY_OPEN;
		break;
	case DST_NODISK:
		r = CDS_NO_DISC;
		break;
	case DST_BUSY:
	case DST_NOSPIN:
		r = CDS_DRIVE_NOT_READY;
		break;
	case DST_OK:
		r = CDS_DISC_OK;
		break;
	default:
		r = pncd_errconv(s);
		break;
	}
	UP(cmd_s);
	return r;
}

static int pncd_tray_move(struct cdrom_device_info *cdi, int position)
{
	int e, r;
#ifdef PNCD_DEBUG
	printk(KERN_DEBUG "pncd DEBUG: pncd_tray_move, position = %d\n",
	       position);
#endif
	DOWN(cmd_s);
	e = pncd_tray(position, TRAY_WAIT);
	if (e) {
		perr(e);
		r = pncd_errconv(e);
	} else
		r = 0;
	UP(cmd_s);
	return r;
}

static int pncd_lock_door(struct cdrom_device_info *cdi, int lock)
{
	int e, r;

#ifdef PNCD_DEBUG
	printk(KERN_DEBUG "pncd DEBUG: pncd_lock_door, lock = %d\n", lock);
#endif
	DOWN(cmd_s);
	r = 0;
	if(lock) {
		e = pncd_lock();
		if (e) {
			perr(e);
			r = pncd_errconv(e);
		} else
			pncd.locked = 1;
	} else {
		e = pncd_unlock();
		if (e) {
			perr(e);
			r = pncd_errconv(e);
		} else
			pncd.locked = 0;
	}
	UP(cmd_s);
	return r;
}

static inline int pncd_speed(int s)
{
	struct PNCD_CMD cmd;

	return pncd_docmd(DRIVECMDCLASS, CMD_SETMODE,
	                  mkc(&cmd, MODE_SPEED, s, 0, 0, 0, 0), NULL, 0);
}

static int pncd_select_speed(struct cdrom_device_info *cdi, int speed)
{
	int e, r, s;

#ifdef PNCD_DEBUG
	printk(KERN_DEBUG "pncd DEBUG: pncd_select_speed: %d\n", speed);
#endif
	DOWN(cmd_s);
	switch(speed) {
	case 0:
		s = PNCD_SPEED_AUTO;
		break;
	case 1:
		s = PNCD_SPEED_1x;
		break;
	case 2:
		s = PNCD_SPEED_2x;
		break;
	default:
		UP(cmd_s);
		return -EINVAL;
	}
	if ((e = pncd_speed(s))) {
		perr(e);
		r = pncd_errconv(e);
	} else
		r = 0;
	UP(cmd_s);
	return r;
}

static int pncd_get_last_session(struct cdrom_device_info *cdi,
                                struct cdrom_multisession *cdmp)
{
	int e;

#ifdef PNCD_DEBUG
	printk(KERN_DEBUG "pncd DEBUG: pncd_get_last_session\n");
#endif
	DOWN(cmd_s);
	e = pncd_getstatd();
	if (e) {
		UP(cmd_s);
		return pncd_errconv(e);
	}
	cdmp->xa_flag = pncd.multi.flag;
	if (pncd.multi.flag) {
		cdmp->addr_format = CDROM_MSF;
		cdmp->addr.msf.minute = pncd.multi.base.m;
		cdmp->addr.msf.second = pncd.multi.base.s;
		cdmp->addr.msf.frame = pncd.multi.base.f;
		UP(cmd_s);
		return 0;
	} else {
		UP(cmd_s);
		return 1;
	}
}

static int pncd_get_mcn(struct cdrom_device_info *cdi, struct cdrom_mcn *mcn)
{
	int e, i, j;
	u_char mcnstr[MCNSTR_SIZE];
	char *m;

#ifdef PNCD_DEBUG
	printk(KERN_DEBUG "pncd DEBUG: pncd_get_mcn\n");
#endif
	DOWN(cmd_s);
	if ((e = pncd_mcn(mcnstr))) {
		UP(cmd_s);
		return pncd_errconv(e);
	}
	m = mcn->medium_catalog_number;
	if (mcnstr[0] != MCN_OK) {
		*m = '\0';
	} else {
		for (i = 0, j = 0; i < MCN_SIZE; ++i) {
			if (i % 2 == 0) {
				++j;
				m[i] = (mcnstr[j] >> 4) | (u_char)'0';
			} else {
				m[i] = (mcnstr[j] & 0xf) | (u_char)'0';
			}
#ifdef PNCD_DEBUG
			printk(KERN_DEBUG "pncd DEBUG: m[%d]: %x, [%d]: %x\n",
			       i, m[i], j, mcnstr[j]);
#endif
		}
		m[i] = '\0';
	}
#ifdef PNCD_DEBUG
	printk(KERN_DEBUG "pncd DEBUG: mcn: %s\n", m);
#endif
	UP(cmd_s);
	return 0;
}

static int pncd_reset(struct cdrom_device_info *cdi)
{
	int e;

#ifdef PNCD_DEBUG
	printk(KERN_DEBUG "pncd DEBUG: pncd_reset\n");
#endif
	DOWN(cmd_s);
	e = pncd_resetcmd();
	if (e == 0) {
		UP(cmd_s);
		return -EIO;
	}
	e = pncd_getstat();
	/* media changed msg is OK */
	if (e && e != DST_DISKCHANGED) {
		perr(e);
		UP(cmd_s);
		return pncd_errconv(e);
	}
	if (pncd.update_toc) {
		e = pncd_readtoc();
		if (e) {
			perr(e);
			UP(cmd_s);
			return pncd_errconv(e);
		}
	}
	UP(cmd_s);
	return 0;
}

static int pncd_media_changed(struct cdrom_device_info *cdi, int disc_nr)
{
	int e;

#ifdef PNCD_DEBUG
	printk(KERN_DEBUG "pncd DEBUG: pncd_media_changed: pncd.change_flag = "
	       "%d\n", pncd.change_flag);
#endif
	DOWN(cmd_s);
	if (pncd.change_flag) {
		pncd.change_flag = 0;
		UP(cmd_s);
		return 1;
	}
	e = pncd_getstat();
	if (e) {
		perr(e);
		UP(cmd_s);
		return pncd_errconv(e);
	}
	if (pncd.change_flag) {
		pncd.change_flag = 0;
		UP(cmd_s);
		return 1;
	}
	UP(cmd_s);
	return 0;
}

#define TH		((struct cdrom_tochdr *)(arg))
#define TE		((struct cdrom_tocentry *)(arg))
#define MSF		((struct cdrom_msf *)(arg))
#define TI		((struct cdrom_ti *)(arg))
#define VOLCTRL		((struct cdrom_volctrl *)(arg))
#define SUBCHNL		((struct cdrom_subchnl *)(arg))

static int pncd_audio_ioctl(struct cdrom_device_info *cdi,
                           unsigned int cmd, void *arg)
{
	int e, track;
	u_char vol;

#ifdef PNCD_DEBUG
	printk(KERN_DEBUG "pncd DEBUG: entering audio_ioctl\n");
#endif
	DOWN(cmd_s);
	e = pncd_init_ioctl(cmd);
	if (e) {
		perr(e);
		UP(cmd_s);
		return pncd_errconv(e);
	}

	switch (cmd) {
	case CDROMREADTOCHDR:
#ifdef PNCD_DEBUG
		printk(KERN_DEBUG "pncd DEBUG: audio_ioctl CDROMREADTOCHDR\n");
#endif
		TH->cdth_trk0 = pncd.toc.start_track;
		TH->cdth_trk1 = pncd.toc.end_track;
		break;
	case CDROMREADTOCENTRY:
#ifdef PNCD_DEBUG
		printk(KERN_DEBUG "pncd DEBUG: audio_ioctl CDROMREADTOCENTRY\n");
#endif
		track = TE->cdte_track == CDROM_LEADOUT ?
		          pncd.toc.end_track + 1 :
		          TE->cdte_track;
		if (track < pncd.toc.start_track ||
		    track > pncd.toc.end_track + 1) {
			UP(cmd_s);
			return -EINVAL;
		}
		TE->cdte_adr = pncd.toc.entry[track].adr;
		TE->cdte_ctrl = pncd.toc.entry[track].ctrl;
		TE->cdte_datamode = pncd.toc.entry[track].datamode;
		TE->cdte_addr.msf.minute = pncd.toc.entry[track].start.m;
		TE->cdte_addr.msf.second = pncd.toc.entry[track].start.s;
		TE->cdte_addr.msf.frame = pncd.toc.entry[track].start.f;
		break;
	case CDROMPLAYMSF:
#ifdef PNCD_DEBUG
		printk(KERN_DEBUG "pncd DEBUG: audio_ioctl CDROMPLAYMSF\n");
#endif
		if (msf_less(pncd.toc.size.m,
		             pncd.toc.size.s,
		             pncd.toc.size.f,
		             MSF->cdmsf_min1,
		             MSF->cdmsf_sec1,
		             MSF->cdmsf_frame1)) {
			UP(cmd_s);
			return -EINVAL;
		}
		e = pncd_playmsf(MSF->cdmsf_min0,
		                MSF->cdmsf_sec0,
		                MSF->cdmsf_frame0,
		                MSF->cdmsf_min1,
		                MSF->cdmsf_sec1,
		                MSF->cdmsf_frame1);
		if (e) {
			perr(e);
			UP(cmd_s);
			return pncd_errconv(e);
		}
		break;
	case CDROMPLAYTRKIND:
#ifdef PNCD_DEBUG
		printk(KERN_DEBUG "pncd DEBUG: audio_ioctl CDROMPLAYTRKIND\n");
#endif
		if (TI->cdti_trk0 < pncd.toc.start_track ||
		    TI->cdti_trk1 > pncd.toc.end_track ||
		    TI->cdti_trk0 > TI->cdti_trk1) {
			UP(cmd_s);
			return -EINVAL;
		}
		/* index is at least 1... */
		/* maybe the unified driver should check */
		/* CR56x (and pncd) _does_ care about index values */
		if (TI->cdti_ind0 <= 0)
			TI->cdti_ind0 = 1;
		if (TI->cdti_ind1 <= 0)
			TI->cdti_ind1 = 1;
		e = pncd_playti(TI->cdti_trk0, TI->cdti_ind0,
		               TI->cdti_trk1, TI->cdti_ind1);
		if (e) {
			perr(e);
			UP(cmd_s);
			return pncd_errconv(e);
		}
		break;
	case CDROMSTOP:
#ifdef PNCD_DEBUG
		printk(KERN_DEBUG "pncd DEBUG: audio_ioctl CDROMSTOP\n");
#endif
		if ((e = pncd_stop())) {
			perr(e);
			UP(cmd_s);
			return pncd_errconv(e);
		}
		break;
	case CDROMPAUSE:
#ifdef PNCD_DEBUG
		printk(KERN_DEBUG "pncd DEBUG: audio_ioctl CDROMPAUSE\n");
#endif
		if ((e = pncd_pause())) {
			perr(e);
			UP(cmd_s);
			return pncd_errconv(e);
		}
		break;
	case CDROMRESUME:
#ifdef PNCD_DEBUG
		printk(KERN_DEBUG "pncd DEBUG: audio_ioctl CDROMRESUME\n");
#endif
		if ((e = pncd_resume())) {
			perr(e);
			UP(cmd_s);
			return pncd_errconv(e);
		}
		break;
	case CDROMSTART:
#ifdef PNCD_DEBUG
		printk(KERN_DEBUG "pncd DEBUG: audio_ioctl CDROMSTART\n");
#endif
		if ((e = pncd_spin())) {
			perr(e);
			UP(cmd_s);
			return pncd_errconv(e);
		}
		break;
	case CDROMVOLCTRL:
#ifdef PNCD_DEBUG
		printk(KERN_DEBUG "pncd DEBUG: audio_ioctl CDROMVOLCTRL\n");
#endif
		if (VOLCTRL->channel0 > VOLCTRL->channel1)
			vol = VOLCTRL->channel0;
		else
			vol = VOLCTRL->channel1;
		if ((e = pncd_volume(vol))) {
			perr(e);
			UP(cmd_s);
			return pncd_errconv(e);
		}
		break;
	case CDROMVOLREAD: {
#ifdef PNCD_DEBUG
		printk(KERN_DEBUG "pncd DEBUG: audio_ioctl CDROMVOLREAD\n");
#endif
		if((e = pncd_readvolume(&vol))) {
			perr(e);
			UP(cmd_s);
			return pncd_errconv(e);
		}
		VOLCTRL->channel0 = VOLCTRL->channel1 = vol;
		VOLCTRL->channel2 = VOLCTRL->channel3 = 0;
		break;
	}
	case CDROMSUBCHNL:
#ifdef PNCD_DEBUG
		/*printk(KERN_DEBUG "pncd DEBUG: audio_ioctl CDROMSUBCHNL\n");*/
#endif
		SUBCHNL->cdsc_audiostatus =
		   pncd.status & STAT_PLAY ?
		      pncd.paused ?
		         CDROM_AUDIO_PAUSED :
		         CDROM_AUDIO_PLAY :
		      pncd.prev_status & STAT_PLAY ?
		         CDROM_AUDIO_COMPLETED :
		         CDROM_AUDIO_NO_STATUS;
#ifdef PNCD_DEBUG
		printk(KERN_DEBUG "pncd DEBUG: audio_status; %x\n",
		       SUBCHNL->cdsc_audiostatus);
#endif
		pncd.prev_status = pncd.status;
		e = pncd_readsubq();
		/* 
		 * don't write error messages: this ioctl is used to be
		 * called to poll CDROM status in CD players
		 */
		if (e) {
#ifdef PNCD_DEBUG
			printk(KERN_DEBUG "pncd DEBUG: Error: %s.\n", ERRSTR(e));
#endif
			UP(cmd_s);
			return pncd_errconv(e);
		}
		SUBCHNL->cdsc_adr = pncd.subq.adr;
		SUBCHNL->cdsc_ctrl = pncd.subq.ctrl;
		SUBCHNL->cdsc_trk = pncd.subq.track;
		SUBCHNL->cdsc_ind = pncd.subq.index;
		SUBCHNL->cdsc_absaddr.msf.minute = pncd.subq.absaddr.m;
		SUBCHNL->cdsc_absaddr.msf.second = pncd.subq.absaddr.s;
		SUBCHNL->cdsc_absaddr.msf.frame = pncd.subq.absaddr.f;
		SUBCHNL->cdsc_reladdr.msf.minute = pncd.subq.reladdr.m;
		SUBCHNL->cdsc_reladdr.msf.second = pncd.subq.reladdr.s;
		SUBCHNL->cdsc_reladdr.msf.frame = pncd.subq.reladdr.f;
		break;
	default:
#ifdef PNCD_DEBUG
		printk(KERN_DEBUG "pncd DEBUG: unknown audio_ioctl\n");
#endif
		UP(cmd_s);
		return -ENOSYS;
	}
	UP(cmd_s);
	return 0;
}


