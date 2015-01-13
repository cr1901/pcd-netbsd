/*	$NetBSD: pcd.c,v 0.1 2015/01/12 21:31:28 wjones Exp $	*/

/*
 * Copyright (c) 2014-2015 William D. Jones.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by Charles M. Hannum.
 * 4. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * Copyright 2014-2015 by William D. Jones (data part)
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This software was developed by Holger Veit and Brian Moore
 *      for use with "386BSD" and similar operating systems.
 *    "Similar operating systems" includes mainly non-profit oriented
 *    systems for research and education, including but not restricted to
 *    "NetBSD", "FreeBSD", "Mach" (by CMU).
 * 4. Neither the name of the developer(s) nor the name "386BSD"
 *    may be used to endorse or promote products derived from this
 *    software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE DEVELOPER(S) ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE DEVELOPER(S) BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY,
 * OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT
 * OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
 
#include <sys/cdefs.h>
__KERNEL_RCSID(0, "$NetBSD: pcd.c,v 0.1 2015/01/12 21:31:28 wjones wjones Exp $");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/errno.h>
#include <sys/device.h>
#include <sys/malloc.h>
#include <sys/uio.h>
#include <sys/proc.h>
#include <sys/ioctl.h>
#include <sys/conf.h>
#include <sys/disk.h>
#include <sys/mutex.h>

#include <sys/bus.h>

#include <dev/isa/isavar.h>

/* Stolen from mcd.c */
#ifndef PCDDEBUG
#define PCD_TRACE(fmt,...)
#else
#define PCD_TRACE(fmt,...)	{if (sc->debug) {printf("%s: st=%02x: ", device_xname(sc->sc_dev), sc->status); printf(fmt,__VA_ARGS__);}}
#endif


#define PCD_NPORTS 4

#define ERRSIZE		8
#define ERRBYTE		2


/* IO ports */
#define	PORT_BASE  0x340 
#define PCD_CMD 0    /* R/W- Returns data on Sound Blaster; Write Command and Read Command Response for all models. */             
#define PCD_CMD_RESPONSE 0         
#define PCD_SB_DATA 0             
#define PCD_SB_SELECT 1   /* W- On Sound Blaster, this swaps PORT_BASE between receiving data (1) and command response (0).
			This does not appear to be documented in: http://pdos.csail.mit.edu/6.828/2008/readings/hardware/SoundBlaster.pdf
			Does nothing for other models? */     
#define PCD_RESPONSE_TYPE 1  /* R- tells whether Data or Command Response is available. */
#define PCD_RESET 2 	/* W- Write to this to reset the drive. Or use CMD_RESET. Either works. */
#define PCD_DATA 2      /* R- Depending on interface, data may come here (normal) or via PORT_BASE (Sound Blaster). */
#define PCD_ENABLE 3         /* W- Which drive do we want? PCD supports up to 4 drives. */

/* Status register */
#define	STAT_TRAY_CLOSED  0x80 /* Set if tray closed. */
#define	STAT_DISK_IN_DRIVE  0x40
#define	STAT_UNKNOWN_0x20  0x20 /* It's possible this is a "drive idle/not up to speed" bit. */
#define	STAT_ERROR_CMD  0x10 /* If the just-issued command errored, this bit is set. */
#define	STAT_PLAY  0x08
#define	STAT_UNKNOWN_0x04  0x04 /* Unused? */
#define	STAT_UNKNOWN_0x02  0x02 /* Always set for my drive. */
#define	STAT_DRIVE_READY  0x01

/* Commands */
#define	CMD_SEEK  0x00
#define	CMD_SPINUP  0x01
#define	CMD_STATUS  0x05
#define	CMD_TRAY_OUT  0x06
#define	CMD_TRAY_IN  0x07
#define	CMD_ABORT  0x08
#define	CMD_SET  0x09
#define	CMD_RESET  0x0A
#define	CMD_LOCK  0x0C
#define	CMD_PAUSE  0x0D
#define	CMD_PLAY_MSF  0x0E
#define	CMD_PLAY_TRACK  0x0F
#define	CMD_READ  0x10
#define	CMD_SUBCHANNEL  0x11
#define	CMD_ERROR  0x82
#define	CMD_VERSION  0x83
#define	CMD_MODE  0x84
#define	CMD_CAPACITY  0x85
#define	CMD_SUBQ  0x87
#define	CMD_UPC  0x88
#define	CMD_DISKINFO  0x8B
#define	CMD_TOC  0x8C
#define	CMD_MULTISESSION  0x8D
#define	CMD_PACKET  0x83
	

#define WAIT_CMD_REPLY 0x04
#define WAIT_DATA 0x02


/* Possible error codes. Based on Zoltan's Linux Driver, since the drive never
returns negative numbers, repurpose them for the driver. */

#define ERR_TIMEOUT -1
#define ERR_OK 0
#define ERR_RESET 0x12

/* static unsigned char version_string[12] = {'\0'};
static unsigned char error_str[ERRSIZE] = {'\0'}; */


struct pcd_bus_handles
{
	bus_space_tag_t iot;
	bus_space_handle_t ioh;
};

struct pcd_softc {
             device_t sc_dev;                /* generic device info */
             struct disk sc_dk;
             kmutex_t sc_lock; /* Make sure shared state isn't trashed. */
             
             /* We need to keep the allocated handles around so bus ops don't fail. */
             struct pcd_bus_handles bh;
             
             uint8_t last_error; /* Byte 2 of the error buffer. */
             
             /* device-specific state */
};


int pcd_probe(device_t parent, cfdata_t match, void *aux);
void pcd_attach(device_t parent, device_t self, void *aux);
int pcd_activate(device_t self, enum devact act);

int pcd_find(struct pcd_bus_handles * bh);
int pcd_reset(struct pcd_bus_handles * bh);
int pcd_wait(struct pcd_bus_handles * bh, uint8_t response, unsigned long timeout);
int pcd_get_error(struct pcd_bus_handles * bh);
void pcd_send_simple_command(struct pcd_bus_handles *, unsigned char cmd);
//int pcd_find(bus_space_tag_t, bus_space_handle_t, struct pcd_softc *);


//CFATTACH_DECL does not work, per ftp://ftp.netbsd.org/pub/NetBSD/misc/ddwg/NetBSD-driver_writing-1.0.1e.pdf
CFATTACH_DECL_NEW(pcd, sizeof(struct pcd_softc), pcd_probe, pcd_attach, NULL, pcd_activate); 

dev_type_open(pcdopen);
dev_type_close(pcdclose);
dev_type_read(pcdread);
dev_type_write(pcdwrite);
dev_type_ioctl(pcdioctl);
dev_type_strategy(pcdstrategy);
dev_type_dump(pcddump);
dev_type_size(pcdsize);      

//Need to include more than just the 3 header files that driver(9) specifies.
const struct bdevsw pcd_bdevsw = {
	.d_open = pcdopen,
	.d_close = pcdclose,
	.d_strategy = pcdstrategy,
	.d_ioctl = pcdioctl,
	.d_dump = pcddump,
	.d_psize = pcdsize,
	.d_flag = D_DISK
};

const struct cdevsw pcd_cdevsw = {
	.d_open = pcdopen,
	.d_close = pcdclose,
	.d_read = pcdread,
	.d_write = pcdwrite,
	.d_ioctl = pcdioctl,
	.d_stop = nostop,
	.d_tty = notty,
	.d_poll = nopoll,
	.d_mmap = nommap,
	.d_kqfilter = nokqfilter,
	.d_flag = D_DISK
};



/* driver(9) says return EOPNOTSUPP if activation is not supported. */
int
pcd_activate(device_t self, enum devact act)
{
	return EOPNOTSUPP;
}

int
pcd_probe(device_t parent, cfdata_t match, void *aux)
{
	struct isa_attach_args *ia = aux;
	struct pcd_bus_handles bh; /* We need a copy of this because of the signatures
	of the send/rcv functions. We only use it primarily for iot/ioh tho. */
	bus_space_tag_t iot;
	int io_base;
	int rv;
	bus_space_handle_t ioh; 
	
	//printf("Hi! Looking for PCD...\n");
	
	/* This is strictly a port I/O device. */
	if (ia->ia_nio < 1)
	{
		//printf("Ooh, sorry! ia->ia_nio was %d, which is < 1...\n", ia->ia_nio);
		return (0);
	}
	
	//This device does not use IRQs. Is < 1 or >= 1 required then?
	/* if (ia->ia_nirq < 1)
	{
		return (0);
	} */
	
	/* This is not a PnP device. Joy is similar (only port I/O), but doesn't 
	require this part. Hmmm... */
	if (ISA_DIRECT_CONFIG(ia))
	{
		//printf("ISA bus thinks this is a Plug-n-Pray device...\n");
		return (0);
	}

	/* Disallow wildcarded i/o address. */
	if (ia->ia_io[0].ir_addr == ISA_UNKNOWN_PORT)
	{
		//printf("Wildcard I/O address...\n");
		return (0);
	}
	
	//Device does not use IRQs
	/*if (ia->ia_irq[0].ir_irq == ISA_UNKNOWN_IRQ)
	{
		printf("Wildcard IRQ...\n");
		return (0);
	} */
	
	iot = ia->ia_iot;
	io_base = ia->ia_io[0].ir_addr;
	
	/* Reserve the 4 I/O ports using the machine independent bus mapping
	function (of course, it'll resolve to an ISA bus mapping, but 
	it's meant to be abstract. */
	if (bus_space_map(iot, io_base, PCD_NPORTS, 0, &ioh)) {
		return 0;
	}
	
	/* Use our shiny new handle to do some I/O reads/and writes and see if the
	darn thing exists. This function determines whether this driver can 
	confidently support what the ISA bus autoconfig thinks is a device. */
	bh.iot = iot;
	bh.ioh = ioh;
	rv = pcd_find(&bh);
	
	/* Okay, we don't need the ports anymore. */
	bus_space_unmap(iot, ioh, PCD_NPORTS);
	
	if (rv)	{
		ia->ia_nio = 1;
		ia->ia_io[0].ir_size = PCD_NPORTS;

		ia->ia_nirq = 0;

		ia->ia_niomem = 0;
		ia->ia_ndrq = 0;
		//printf("Drive was found! Return 0 for now\n");
	}
	
	//printf("Well, we got to the end of pcd_probe in one piece, at least...\n");
	//return rv;
	return rv;
}

void
pcd_attach(device_t parent, device_t self, void *aux)
{
	struct isa_attach_args *ia = aux;
	struct pcd_softc *sc = device_private(self);
	int io_base;
	bus_space_tag_t iot;
	bus_space_handle_t ioh; 
	
	iot = ia->ia_iot; /* Get handle to appropriate bus */
	io_base = ia->ia_io[0].ir_addr;
	
	if (bus_space_map(iot, io_base, PCD_NPORTS, 0, &ioh)) {
		return;
	}
	
	/* Ensure two programs don't try to access driver state at the same
	time. */
	mutex_init(&sc->sc_lock, MUTEX_DEFAULT, IPL_NONE);
	sc->bh.iot = iot;
	sc->bh.ioh = ioh;
	
	aprint_normal("\n");
	aprint_naive("\n");
	
	return;
}



int 
pcd_find(struct pcd_bus_handles * bh) {
	/* Choose drive 0. Add multiple drive support later. */
	bus_space_write_1(bh->iot, bh->ioh, PCD_ENABLE, 0);
	return pcd_reset(bh); /* If reset happened successfully, we can be
	confident the drive exists. */
}




/* unsigned char
pcd_get_response(bus_space_tag_t iot, bus_space_handle_t ioh) {
	
	
}


int
pcd_delay(bus_space_tag_t iot, bus_space_handle_t ioh) {
		
	
} */

/* Returns 0 on failure, 1 on success. */
int
pcd_reset(struct pcd_bus_handles * bh) {
	uint8_t cmd_rc;
	int rv = 0;
	
	//printf("Sending reset command...\n");
	bus_space_write_1(bh->iot, bh->ioh, PCD_RESET, 0);
	delay(3000000); /* From Linux driver, should wait 3 seconds. */
	//pcd_send_simple_command(iot, ioh, CMD_STATUS);
	cmd_rc = bus_space_read_1(bh->iot, bh->ioh, PCD_CMD_RESPONSE);
	
	/* We expect a reset to error out. */
	if(cmd_rc & STAT_ERROR_CMD)
	{		
		if(pcd_get_error(bh) == 0x12)
		{
			rv = 1;
		}
		/* If the error code matches the reset occurred value, all is good. */
	}
	
	return rv;
}


void 
pcd_send_simple_command(struct pcd_bus_handles * bh, unsigned char cmd)
{
	register int count;
	
	bus_space_write_1(bh->iot, bh->ioh, PCD_CMD, cmd);
	for(count = 1; count <= 6; count++)
	{
		bus_space_write_1(bh->iot, bh->ioh, PCD_CMD, 0);
	}
}


int
pcd_get_error(struct pcd_bus_handles * bh)
{

	uint8_t error_str[ERRSIZE];
        register int i;

        pcd_send_simple_command(bh, CMD_ERROR);
        
        if(pcd_wait(bh, WAIT_CMD_REPLY, 2000000)) /* If timeout, abort. */
        {
        	//printf("Timed out...\n");
        	return -1;
        }

        for (i = 0; i < ERRSIZE; ++i)
        {
        	error_str[i] = bus_space_read_1(bh->iot, bh->ioh, PCD_CMD_RESPONSE);
                //printf("Error string %d: %hhx\n", i, error_str[i]);
        }
        
        /* Read and discard error code status (for now) */
        (void) bus_space_read_1(bh->iot, bh->ioh, PCD_CMD_RESPONSE);
        return error_str[ERRBYTE];
}

int
pcd_wait(struct pcd_bus_handles * bh, uint8_t response, unsigned long timeout)
{
	unsigned long usecs_elapsed = 0;
        
        while(usecs_elapsed < timeout)
        {
        	if((bus_space_read_1(bh->iot, bh->ioh, PCD_RESPONSE_TYPE) & response) != response)
        	{
        		return 0;
        	}
        	delay(1000);
        	usecs_elapsed += 1000;
        /* printf("Return Code from PORT_STATUS (%X) was: %X.\n", PORT_STATUS, r
eturn_code); */
        }
        
        return -1;
}




/* The block device functions. */

/* I can't find the man page which discusses the paramater variable naming 
convention, so I'm just using what I see every other driver do! */
int
pcdopen(dev_t dev, int flags, int mode, struct lwp *l)
{
	return 0;
}

int
pcdclose(dev_t dev, int flags, int mode, struct lwp *l)
{
	return 0;
}

int
pcdread(dev_t dev, struct uio *uio, int flags)
{
	return 0;	
}

int
pcdwrite(dev_t dev, struct uio *uio, int flags)
{
	return 0;
}

int
pcdioctl(dev_t dev, u_long cmd, void *addr, int flag, struct lwp *l)
{
	return 0;
}

void
pcdstrategy(struct buf *bp)
{
	return;
}

int
pcddump(dev_t dev, daddr_t blkno, void *va, size_t size)
{
	return 0;
}

int
pcdsize(dev_t dev)
{
	return 0;
}
