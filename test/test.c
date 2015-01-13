/* test.c */

/* Internal test program.  Not for distribution. */
/* Copyright (c) 1996, 1999 Zoltan Vorosbaranyi  */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

#include "/usr/src/linux/include/asm/io.h"

typedef unsigned char byte;

#define DATA_OFFSET	150

#define INBUF_SIZE	256
char inbuf[INBUF_SIZE];

#define DATABUF_SIZE	0x100
byte databuf[DATABUF_SIZE];

int cmd_n;		/* Number of words in the command line */

#define CMD_SIZE	64
char *cmd[CMD_SIZE];	/* command line */
int arg[CMD_SIZE];	/* numeric arguments */

#define BASE_ADDR	0x340

typedef struct CMD_S
{
	char *name;
	char *desc;
	void (*fun)();
} cmd_s;

struct timeval tv1, tv2;
struct timezone tz;

int blksize = 2048;

byte in;
	
#define WAIT_TIME	3000000
#define SHORT_WAIT	10000

char *errstr[] = {
	"No error",
	"Soft read error after retry",
	"Soft read error after error correction",
	"Not ready",
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
	"Disc removed during operation",
	"Drive hardware error",
	"Illegal request from host"
};

int errstrsize = sizeof(errstr) / sizeof(char *);

void geterr(void);

void quit_c(void);
void help_c(void);
void in_c(void);
void out_c(void);
void info_c(void);
void stat_c(void);
void open_c(void);
void close_c(void);
void geterr_c(void);
void lock_c(void);
void unlock_c(void);
void ti_c(void);
void msf_c(void);
void stop_c(void);
void getmode_c(void);
void getam_c(void);
void getsm_c(void);
void vol_c(void);
void pitch_c(void);
void pause_c(void);
void res_c(void);
void reset_c(void);
void toc_c(void);
void dinfo_c(void);
void subq_c(void);
void read_c(void);
void setblksize_c(void);
void speed_c(void);
void mcn_c(void);

cmd_s commands[] =
{
	{"in", "input byte(s) from port: in #port [number]", in_c},
	{"out", "output byte(s) to port: out #port val1 val2 ...", out_c},
	{"help", "give help", help_c},
	{"quit", "quit", quit_c},
	{"info", "get info string", info_c},
	{"stat", "get CD status", stat_c},
	{"open", "open tray", open_c},
	{"close", "close tray", close_c},
	{"err", "get error", geterr_c},
	{"lock", "lock tray", lock_c},
	{"unlock", "unlock tray", unlock_c},
	{"ti", "play track & index: ti from-track index to-track index", ti_c},
	{"msf", "play msf: msf from-m s f to-m s f", msf_c},
	{"stop", "stop audio playing", stop_c},
	{"getmode", "report drive settings", getmode_c},
	{"getam", "report audio settings", getam_c},
	{"getsm", "report speed settings", getsm_c},
	{"vol", "set volume: vol hexnum", vol_c},
	{"pitch", "set speed: speed num (87 <= num <= 113)", pitch_c},
	{"pause", "pause audio playing", pause_c},
	{"res", "resume audio playing", res_c},
	{"reset", "reset drive", reset_c},
	{"toc", "read toc (table of contents)", toc_c},
	{"dinfo", "read disk info", dinfo_c},
	{"subq", "subchannel info", subq_c},
	{"read", "read data: read m s f", read_c},
	{"setblksize", "set block size to be read", setblksize_c},
	{"speed", "set speed: speed 1 | speed 2", speed_c},
	{"mcn", "get Media Catalog Number (Universal Product Code)", mcn_c},
	{NULL, NULL}
};

void tokenize_cmd(void)
{
	int i;
	char *s;

	cmd_n = 0;
	if((cmd[0] = strtok(inbuf, " \t")) != NULL) {
		for(i = 1; (s = strtok(NULL, " \t")) && i < CMD_SIZE; ++i) {
			cmd[i] = s;
		}
		cmd_n = i;
	}
}

int get_cmd(void)
{

	printf("test> ");
	if(fgets(inbuf, INBUF_SIZE, stdin) == NULL) {
		printf("\n");
		return 0;		/* quit */
	}
	inbuf[strlen(inbuf) - 1] = '\0';
	tokenize_cmd();
	return 1;
}

void undef_cmd_c(void)
{
	printf("Undefined command.  Try ``help''.\n");
}

void help_c(void)
{
	int i;

	printf("Available commands:\n");
	for(i = 0; commands[i].name; ++i) {
		printf("%s -- %s\n", commands[i].name, commands[i].desc);
	}
}	

void quit_c(void)
{
	exit(0);
}

void do_cmd(void)
{
	int i;

	if(cmd_n == 0)
		return;

	for(i = 0; commands[i].name != NULL; ++i) {
		if(strcmp(commands[i].name, cmd[0]) == 0) {
			commands[i].fun();
			return;
		}
	}
	undef_cmd_c();
}

#define ISSEP(c) ((c) == ' ' || (c) == '\0' || (c) == '\t')

int number_p(char *p)
{
	for(; !ISSEP(*p); ++p) {
		if(*p > '9' || *p < '0')
			return 0;
	}
	return 1;
}

#define ISHEX(c) (((c) >= '0' && (c) <= '9') || \
                  ((c) >= 'a' && (c) <= 'f') || \
                  ((c) >= 'A' && (c) <= 'F'))

int hex_p(char *p)
{
	for(; !ISSEP(*p); ++p) {
		if(!ISHEX(*p))
			return 0;
	}
	return 1;
}

int base_args(int from, int to, int (*base_p)(char *), int base)
{
	char *ep;
	int j;

	for(j = from; j <= to; ++j) {
		if(!(*base_p)(cmd[j]))
			return j;
		arg[j] = strtol(cmd[j], &ep, base);
		if(index(" \t", *ep) == NULL) {
			printf("Shouldn't happen: *ep == %d in base_args()!\n",
			       *ep);
			return j;
		}
	}
	return 0;
}

int hexargs(int from, int to)
{
	return base_args(from, to, &hex_p, 16);
}

int numargs(int from, int to)
{
	return base_args(from, to, &number_p, 10);
}

int msf2block(byte m, byte s, byte f)
{
	return m * 60 * 75 + s * 75 + f - DATA_OFFSET;
}

void block2msf(byte *m, byte *s, byte *f, int block)
{
	block += DATA_OFFSET;
	*f = block % 75;
	block = block / 75;
	*s = block % 60;
	*m = block / 60;
}

void in_c(void)
{
	int ix, c;

	if(cmd_n < 2) {
		printf("Too few args.\n");
		return;
	}
	if(cmd_n > 3) {
		printf("Too much args.\n");
		return;
	}
	if((ix = numargs(1, 1)) != 0) {
		printf("arg %d: number expected.\n", ix);
		return;
	}
	if(cmd_n == 3 && (ix = numargs(2, 2)) != 0) {
		printf("arg %d: number expected.\n", ix);
		return;
	}
	if(arg[1] > 3 || arg[1] < 0) {
		printf("arg 1: relative port number 0 .. 3 should be used.\n");
		return;
	}
	if(cmd_n == 2) {
		databuf[0] = c = inb(BASE_ADDR + arg[1]);
		printf("%x:%c ", c, c < 0x20 ? '.' : c);
	} else
		for(ix = 0; ix < arg[2]; ++ix) {
			databuf[ix] = c = inb(BASE_ADDR + arg[1]);
			printf("%x:%c ", c, c < 0x20 ? '.' : c);
	}
	printf("\n");
}

void out_c(void)
{
	int ix;

	if(cmd_n < 3) {
		printf("Too few args.\n");
		return;
	}
	if((ix = numargs(1, 1)) != 0) {
		printf("arg %d: number expected.\n", ix);
		return;
	}
	if((ix = hexargs(1, cmd_n - 1)) != 0) {
		printf("arg %d: hex number expected.\n", ix);
		return;
	}
	if(arg[1] > 3 || arg[1] < 0) {
		printf("arg 1: relative port number 0 .. 3 should be used.\n");
		return;
	}
	for(ix = 2; ix < cmd_n; ++ix)
		outb(arg[ix], BASE_ADDR + arg[1]);
}

void do_int_cmd(char *cmdstr)
{
	strcpy(inbuf, cmdstr);
	printf("%s\n", inbuf);
	tokenize_cmd();
	do_cmd();
}

void diff_tv(struct timeval *dtvp, struct timeval *tvp1, struct timeval *tvp2)
{
	dtvp->tv_sec = tvp2->tv_sec - tvp1->tv_sec;
	dtvp->tv_usec = tvp2->tv_usec - tvp1->tv_usec;
	if(dtvp->tv_usec < 0) {
		dtvp->tv_usec += 1000000;
		dtvp->tv_sec -= 1;
	}
}
	
int wait_data(int silent, byte flag, int timeout)
{
	int i;
	struct timeval dt;

	in = 0;
	if (!silent)
		printf("Wait:%d ", flag);
	gettimeofday(&tv1, &tz);
	for (i = 0; i < timeout; ++i) {
		if(((in = inb(BASE_ADDR + 1)) & flag) != flag) {
			gettimeofday(&tv2, &tz);
			diff_tv(&dt, &tv1, &tv2);
			if (!silent) {
				printf("in = %x, ", in);
				printf("waited %d cycles, it took %ld.%06ld s\n", 
				       i, dt.tv_sec, dt.tv_usec);
			}
			return i;
		}
	}
	gettimeofday(&tv2, &tz);
	diff_tv(&dt, &tv1, &tv2);
	if (!silent) {
		printf("in = %x, ", in);
		printf("wait_data timed out after %d cycles, it took %ld.%06ld s\n", 
		       i, dt.tv_sec, dt.tv_usec);
	}
	return -1;		/* timed out */
}

void print_status(int stat)
{
 printf("                        STATUS BYTE = %x\n", stat);
 printf("+-------+-------+-------+-------+-------+-------+-------+-------+\n"); 
 printf("| tray  | disk  | spin  | error | audio | lock  | doubl | ready |\n");
 printf("+-------+-------+-------+-------+-------+-------+-------+-------+\n"); 
 printf("|  %s  |", stat & 0x80 ? "IN " : "OUT");
 printf("  %s   |", stat & 0x40 ? "IN" : "NO");
 printf("  %s  |", stat & 0x20 ? "YES" : "NO ");
 printf("  %s  |", stat & 0x10 ? "YES" : "NO ");
 printf(" %s  |", stat & 0x08 ? "PLAY" : " NO ");
 printf("  %s  |", stat & 0x04 ? "YES" : "NO ");
 printf("  %s  |", stat & 0x02 ? "YES" : "NO ");
 printf("  %s  |\n", stat & 0x01 ? "YES" : "NO ");
 printf("+-------+-------+-------+-------+-------+-------+-------+-------+\n"); 
}

void trailing(void)
{
	printf("Trailing: ");
	wait_data(0, 4, SHORT_WAIT);
}

void get_status(void)
{
	int stat;

	if (wait_data(0, 4, WAIT_TIME) != -1)
		print_status(stat = inb(BASE_ADDR));
	else
		return;
	
	if ((stat & 0x10) != 0)
		geterr();
}

void info_c(void)
{
	char *cmdstr;
	int i;
	char ib[12];

	cmdstr = "out 0 83 0 0 0 0 0 0";
	do_int_cmd(cmdstr);
	if (wait_data(0, 4, WAIT_TIME) != -1) {
		for (i = 0; i < 10; ++i)
			ib[i] = inb(BASE_ADDR);
		ib[i] = '\0';
		printf("%s\n", ib);
	} else
		return;
	get_status();
	trailing();
}

void stat_c(void)
{
	char *cmdstr;
	
	cmdstr = "out 0 5 0 0 0 0 0 0";
	do_int_cmd(cmdstr);
	get_status();
	trailing();
}

void open_c(void)
{
	char *cmdstr;
	
	cmdstr = "out 0 c 0 0 0 0 0 0";
	do_int_cmd(cmdstr);
	get_status();
	cmdstr = "out 0 6 0 0 0 0 0 0";
	do_int_cmd(cmdstr);
	get_status();
	trailing();
}

void lock_c(void)
{
	char *cmdstr;
	
	cmdstr = "out 0 c 1 0 0 0 0 0";
	do_int_cmd(cmdstr);
	get_status();
	trailing();
}

void unlock_c(void)
{
	char *cmdstr;
	
	cmdstr = "out 0 c 0 0 0 0 0 0";
	do_int_cmd(cmdstr);
	get_status();
	trailing();
}

void stop_c(void)
{
	char *cmdstr;
	
	cmdstr = "out 0 8";
	do_int_cmd(cmdstr);
	get_status();
	trailing();
}

void ti_c(void)
{
	char cmdstr[40];
	int ix;

	if (cmd_n != 5) {
		printf("4 parameters required.\n");
		return;
	}
	if ((ix = numargs(1, 4)) != 0) {
		printf("arg %d: number expected.\n", ix);
		return;
	}
	if (arg[1] > 99 || arg[2] > 99 || arg[3] > 99 || arg[4] > 99) {
		printf("Number greater then 99.\n");
		return;
	}
	sprintf(cmdstr, "out 0 f %x %x %x %x 0 0", 
	        arg[1], arg[2], arg[3], arg[4]);
	do_int_cmd(cmdstr);
	get_status();
	trailing();
}

void msf_c(void)
{
	char cmdstr[40];
	int ix;

	if (cmd_n != 7) {
		printf("6 parameters required.\n");
		return;
	}
	if ((ix = numargs(1, 6)) != 0) {
		printf("arg %d: number expected.\n", ix);
		return;
	}
	sprintf(cmdstr, "out 0 e %x %x %x %x %x %x", 
	        arg[1], arg[2], arg[3], arg[4], arg[5], arg[6]);
	do_int_cmd(cmdstr);
	get_status();
	trailing();
}

void close_c(void)
{
	char *cmdstr;
	
	cmdstr = "out 0 7 0 0 0 0 0 0";
	do_int_cmd(cmdstr);
	get_status();
	trailing();
}

void pause_c(void)
{
	char *cmdstr;
	
	cmdstr = "out 0 d 0 0 0 0 0 0";
	do_int_cmd(cmdstr);
	get_status();
	trailing();
}

void reset_c(void)
{
	char *cmdstr;
	
	cmdstr = "out 2 0";
	do_int_cmd(cmdstr);
	sleep(1);
	stat_c();
}

void res_c(void)
{
	char *cmdstr;
	
	cmdstr = "out 0 d 80 0 0 0 0 0";
	do_int_cmd(cmdstr);
	get_status();
	trailing();
}

void getmode_c(void)
{
	char *cmdstr;
	
	cmdstr = "out 0 84 0 0 0 0 0 0";
	do_int_cmd(cmdstr);
	wait_data(0, 4, WAIT_TIME);
	cmdstr = "in 0 5";
	do_int_cmd(cmdstr);
	get_status();
	trailing();
}

void getam_c(void)
{
	char *cmdstr;
	
	cmdstr = "out 0 84 5 0 0 0 0 0";
	do_int_cmd(cmdstr);
	wait_data(0, 4, WAIT_TIME);
	cmdstr = "in 0 5";
	do_int_cmd(cmdstr);
	get_status();
	trailing();
}

void subq_c(void)
{
	char *cmdstr;
	
	cmdstr = "out 0 87 0 0 0 0 0 0";
	do_int_cmd(cmdstr);
	wait_data(0, 4, WAIT_TIME);
	cmdstr = "in 0 11";
	do_int_cmd(cmdstr);
	printf("SUBQ: [0] = %02x, ctladr = %02x, track = %d, index = %d, abs = %d/%d/%d, rel = %d/%d/%d [10] = %02x\n",
	       databuf[0], databuf[1], databuf[2], databuf[3],
	       databuf[4], databuf[5], databuf[6], 
	       databuf[7], databuf[8], databuf[9], databuf[10]);
	get_status();
	trailing();
}

void getsm_c(void)
{
	char *cmdstr;
	
	cmdstr = "out 0 84 3 0 0 0 0 0";
	do_int_cmd(cmdstr);
	wait_data(0, 4, WAIT_TIME);
	cmdstr = "in 0 5";
	do_int_cmd(cmdstr);
	get_status();
	trailing();
}

void toc_c(void)
{
	char *cmdstr;
	int start, end, i;
	char cmd[80];

	/* first get diskinfo (as in dinfo_c) */
	cmdstr = "out 0 8b 0 0 0 0 0 0";
	do_int_cmd(cmdstr);
	wait_data(0, 4, WAIT_TIME);
	cmdstr = "in 0 6";
	do_int_cmd(cmdstr);
	get_status();
	start = databuf[1];
	end = databuf[2];
	for (i = start; i <= end; ++i) {
		sprintf(cmd, "out 0 8c 0 %2x 0 0 0 0", i);
		do_int_cmd(cmd);
		wait_data(0, 4, WAIT_TIME);
		cmdstr = "in 0 8";
		do_int_cmd(cmdstr);
		get_status();
		printf("ctl_adr: %d, track #: %d, format: %d, MSF: %d/%d/%d\n",
		       databuf[1], databuf[2], databuf[3], databuf[4], databuf[5], databuf[6]);
	}
	trailing();
}

void dinfo_c(void)
{
	char *cmdstr;

	cmdstr = "out 0 8b 0 0 0 0 0 0";
	do_int_cmd(cmdstr);
	wait_data(0, 4, WAIT_TIME);
	cmdstr = "in 0 6";
	do_int_cmd(cmdstr);
	printf("Type = 0x%02x\n", databuf[0]);
	printf("Start track = %d\n", databuf[1]);
	printf("End track = %d\n", databuf[2]);
	printf("Min = %d\n", databuf[3]);
	printf("Sec = %d\n", databuf[4]);
	printf("Frame = %d\n", databuf[5]);
	printf("Blocks = %d\n", msf2block(databuf[3], databuf[4], databuf[5]));
	get_status();
	trailing();
}

void vol_c(void)
{
	int ix;
	char cmdstr[40];

	if((ix = hexargs(1, 1)) != 0) {
		printf("arg %d: hex number expected.\n", ix);
		return;
	}
	sprintf(cmdstr, "out 0 9 5 0 1 %x 2 %x", arg[1], arg[1]);
	do_int_cmd(cmdstr);
	get_status();
	trailing();
}

void read_data(int blocks)
{
	int i, j, r, n;

	for (j = n = 0; j < blocks; ++j) {
#if 0
		r = wait_data(1, 4, 4);
		if (r != -1) {
			get_status();
			break;
		}
#endif
		r = wait_data(0, 2 | 4, WAIT_TIME);
		printf("in = %x\n", in & 0xff);
		if ((in & 4) == 0)
			break;
		if (r != -1) {
			for (i = 0; i < blksize; ++i)
				(void)inb(BASE_ADDR + 2);
			++n;
		}
	}
	printf("%d BLOCKS READ\n", n);
}

void read_c(void)
{
	int ix, blk;
	char cmdstr[40];

	if(cmd_n != 5) {
		printf("4 args required.\n");
		return;
	}
	if((ix = numargs(1, 4)) != 0) {
		printf("arg %d: number expected.\n", ix);
		return;
	}
	sprintf(cmdstr, "out 0 10 %x %x %x 0 0 %x", arg[1], arg[2], arg[3], arg[4]);
	blk = arg[4];
	do_int_cmd(cmdstr);
	read_data(blk);
	get_status();
	trailing();
}

void setblksize_c(void)
{
	int ix;

	if(cmd_n != 2) {
		printf("1 arg required.\n");
		return;
	}
	if ((ix = numargs(1, 1)) != 0) {
		printf("arg %d: number expected.\n", ix);
		return;
	}
	blksize = arg[1];
	printf("Block size = %d (0x%x)\n", blksize, blksize);
}

void pitch_c(void)
{
	int ix, j;
	char cmdstr[40];

	if ((ix = numargs(1, 1)) != 0) {
		printf("arg %d: number expected.\n", ix);
		return;
	}
	if (arg[1] < 87) {
		printf("arg 1 < 87.\n");
		return;
	}
	if (arg[1] > 113) {
		printf("arg 1 > 113.\n");
		return;
	}
	j = (arg[1] - 100) * 10;
	sprintf(cmdstr, "out 0 9 3 0 %x %x 0 0", ((j >> 8) & 3) | 4, j & 0xff);
	do_int_cmd(cmdstr);
	get_status();
	trailing();
}

void speed_c(void)
{
	int ix;
	char cmdstr[40];

	if((ix = numargs(1, 1)) != 0) {
		printf("arg %d: number expected.\n", ix);
		return;
	}
	if (arg[1] != 1 && arg[1] != 2) {
		printf("arg 1: 1 or 2 expected.\n");
		return;
	}
	sprintf(cmdstr, "out 0 9 3 %s 4 0 0 0", arg[1] == 1 ? "40" : "80");
	do_int_cmd(cmdstr);
	get_status();
	trailing();
}

void mcn_c(void)
{
	char *cmdstr;
	
	cmdstr = "out 0 88 0 0 0 0 0 0";
	do_int_cmd(cmdstr);
	wait_data(0, 4, WAIT_TIME);
	cmdstr = "in 0 8";
	do_int_cmd(cmdstr);
	get_status();
	trailing();
}

void geterr(void)
{
	char *cmdstr;
	u_char inbuf[8];
	int i;
	
	cmdstr = "out 0 82 0 0 0 0 0 0";
	do_int_cmd(cmdstr);
	if (wait_data(0, 4, WAIT_TIME) != -1) {
		for (i = 0; i < 8; ++i)
			inbuf[i] = inb(BASE_ADDR);
		printf("+---------------+\n");
		if (inbuf[2] < errstrsize)
			printf("| Err code = %02x +--> %s\n", 
			       inbuf[2], errstr[inbuf[2]]);
		else
			printf("| Err code = %02x +--> Unknown error\n", 
			       inbuf[2]);
		printf("+---------------+\n");
		for (i = 0; i < 8; ++i)
			printf("%x ", inbuf[i]);
		printf("\n");
		print_status(inb(BASE_ADDR));
	}
}

void geterr_c(void)
{
	geterr();
	trailing();
}

void init(void)
{
	int i;
	struct timeval dt;

	if (ioperm(BASE_ADDR, 4, 1) != 0)
		printf("Cannot get IO permission for %x, %d address(es).\n", 
		    BASE_ADDR, 4);

	return;

	/* measuring inb time */
	gettimeofday(&tv1, &tz);
	for (i = 0; i < 100000; ++i) {
		(void)inb(BASE_ADDR);  /*  1 */
		(void)inb(BASE_ADDR);  /*  2 */
		(void)inb(BASE_ADDR);  /*  3 */
		(void)inb(BASE_ADDR);  /*  4 */
		(void)inb(BASE_ADDR);  /*  5 */
		(void)inb(BASE_ADDR);  /*  6 */
		(void)inb(BASE_ADDR);  /*  7 */
		(void)inb(BASE_ADDR);  /*  8 */
		(void)inb(BASE_ADDR);  /*  9 */
		(void)inb(BASE_ADDR);  /* 10 */
	}
	gettimeofday(&tv2, &tz);
	diff_tv(&dt, &tv1, &tv2);
	printf("inb takes %ld.%06ld us\n", 
	       dt.tv_sec,
	       dt.tv_usec);
}

int main()
{
	init();

	for (;;) {
		if(get_cmd() == 0)
			break;
		do_cmd();
	}
	
	exit(0);
}

