/*
 * cdctrl.c
 *
 * utility to set CDROM driver parameters
 */

#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>
#include <limits.h>
#include <string.h>
#include "cdlib.h"

#define PROGNAME	"cdctrl"

char *cd_device = NULL;
int desc = 0;

struct option opt[] = {
	{"speed", required_argument, NULL, 'S'},
	{"device", required_argument, NULL, 'd'},
	{"open", no_argument, NULL, 'e'},
	{"eject", no_argument, NULL, 'e'},
	{"close", no_argument, NULL, 'c'},
	{"status", no_argument, NULL, 's'},
	{"option", required_argument, NULL, 'o'},
	{"help", no_argument, NULL, 'h'},
	{NULL, 0, NULL, 0}
};

char *optstr = "S:d:ecso:h";

#define HAS_ARG		1
#define NO_ARG		0

union argu {
	int iarg;
	char *sarg;
};

struct cmdnode {
	void (*cmd)();
	int hasarg;
	union argu arg;
	struct cmdnode *next;
};

struct cmdnode *cmdchain = NULL;
struct cmdnode *cmdchainend = NULL;

void leave(int retcode)
{
	exit(retcode);
}

void *cmalloc(size_t size)
{
	void *p;

	p = malloc(size);
	if (p == NULL) {
		fprintf(stderr, PROGNAME ": out of memory\n");
		leave(3);
	}
	return p;
}

int intarg(char *arg, int base, int min, int max)
{
	long l;
	char *end;

	end = NULL;
	l = strtol(arg, &end, base);
	if (*end != '\0') {
		fprintf(stderr, PROGNAME ": invalid number '%s'\n", arg);
		leave(1);
	}
	if (min <= max && (l > max || l < min)) {
		fprintf(stderr, PROGNAME ": number out of range '%s'\n", arg);
		leave(1);
	}
	return (int)l;
}

void store(void (*c)(), int hasarg, union argu arg)
{
	struct cmdnode *cp;

	cp = cmalloc(sizeof(struct cmdnode));

	cp->cmd = c;
	cp->hasarg = hasarg;
	cp->arg = arg;
	cp->next = NULL;

	if (cmdchain == NULL) {
		cmdchain = cp;
	} else {
		cmdchainend->next = cp;
	}

	cmdchainend = cp;
}

void speed(union argu arg)
{
	int r;

	/* if (cd_chk_speed(s)) .... */
	r = cd_set_speed(desc, arg.iarg);
	if (r == -1) {
		perror(PROGNAME ": cd_set_speed");
		leave(2);
	}
}

void speed_cmd(char *arg)
{
	int s;

	s = intarg(arg, 10, 0, INT_MAX);
	store(speed, HAS_ARG, s);
}

void eject(void)
{
	int r;

	/* XXX check capability ... */
	r = cd_eject_tray(desc);
	if (r == -1) {
		perror(PROGNAME ": cd_eject_tray");
		leave(2);
	}
}

void eject_cmd(void)
{
	store(eject, NO_ARG, 0);
}

void status(void)
{
	int r;

	/* XXX check capability ... */
	r = cd_get_drive_status(desc);
	if (r == -1) {
		perror(PROGNAME ": cd_status");
		leave(2);
	}
	printf("Drive status: ");
	switch(r) {
	case CDS_NO_INFO:
		printf("No information available\n");
		break;
	case CDS_NO_DISC:
		printf("No disk inserted\n");
		break;
	case CDS_TRAY_OPEN:
		printf("Tray is open\n");
		break;
	case CDS_DRIVE_NOT_READY:
		printf("Drive not ready\n");
		break;
	case CDS_DISC_OK:
		printf("Disk OK\n");
		break;
	default:
		printf("Unknown drive status\n");
		break;
	}
	r = cd_get_disk_status(desc);
	if (r == -1) {
		perror(PROGNAME ": cd_status");
		leave(2);
	}
	printf("Disk status: ");
	switch(r) {
	case CDS_NO_INFO:
		printf("No information available\n");
		break;
	case CDS_NO_DISC:
		printf("No disk inserted\n");
		break;
	case CDS_AUDIO:
		printf("Audio disk (2352 audio bytes/frame)\n");
		break;
	case CDS_DATA_1:
		printf("Data disk (2048 user bytes/frame)\n");
		break;
	case CDS_XA_2_1:
		printf("Mixed data (XA), mode 2 form 1 (2048 user bytes/frame)\n");
		break;
	case CDS_XA_2_2:
		printf("Mixed data (XA), mode 2 form 2 (2324 user bytes/frame)\n");
		break;
	case CDS_MIXED:
		printf("Mixed audio/data disk\n");
		break;
	default:
		printf("Unknown disk status\n");
		break;
	}
	r = cd_get_options(desc);
	if (r == -1) {
		perror(PROGNAME ": cd_status");
		leave(2);
	}
	printf("Options: ");
	printf("%sauto-close, ", r & CDO_AUTO_CLOSE ? "" : "no-");
	printf("%sauto-eject, ", r & CDO_AUTO_EJECT ? "" : "no-");
	printf("%suse-fflags, ", r & CDO_USE_FFLAGS ? "" : "no-");
	printf("%slock, ", r & CDO_LOCK ? "" : "no-");
	printf("%scheck-type\n", r & CDO_CHECK_TYPE ? "" : "no-");
}

void status_cmd(void)
{
	store(status, NO_ARG, 0);
}

void close(void)
{
	int r;

	/* XXX check capability ... */
	r = cd_close_tray(desc);
	if (r == -1) {
		perror(PROGNAME ": cd_close_tray");
		leave(2);
	}
}

void close_cmd(void)
{
	store(close, NO_ARG, 0);
}

void device_cmd(char *arg)
{
	cd_device = arg;
}

struct opt_s {
	char *ostr;
	int bit;
};

struct opt_s set_opts[] = {
	{"auto-close",  1},
	{"auto-eject",  2},
	{"use-fflags",  4},
	{"lock",        8},
	{"check-type", 16}
};

struct opt_s clear_opts[] = {
	{"no-auto-close",  1},
	{"no-auto-eject",  2},
	{"no-use-fflags",  4},
	{"no-lock",        8},
	{"no-check-type", 16}
};

#define N_OPTIONS	5

void option(union argu arg)
{
	char *str, *tok;
	int r, i, set, clr, found;

	str = strdup(arg.sarg);
	if (str == NULL) {
		perror(PROGNAME ": option");
		leave(6);
	}
	tok = strtok(str, ",");
	set = 0;
	clr = 0;
	while (tok) {
		found = 0;
		for (i = 0; i < N_OPTIONS; ++i) {
			if (strcmp(tok, set_opts[i].ostr) == 0) {
				set |= set_opts[i].bit;
				found = 1;
			}
		}
		for (i = 0; i < N_OPTIONS; ++i) {
			if (strcmp(tok, clear_opts[i].ostr) == 0) {
				clr |= clear_opts[i].bit;
				found = 1;
			}
		}
		if (!found) {
			printf(PROGNAME ": unknown option: %s\n", tok);
		}
		tok = strtok(NULL, ",");
	}
	if (set != 0) {
		r = cd_set_options(desc, set);
		if (r == -1) {
			perror(PROGNAME ": cd_set_options");
			free(str);
			leave(2);
		}
	}
	if (clr != 0) {
		r = cd_clear_options(desc, clr);
		if (r == -1) {
			perror(PROGNAME ": cd_clear_options");
			free(str);
			leave(2);
		}
	}
	free(str);
}

void option_cmd(char *arg)
{
	store(option, HAS_ARG, arg);
}

void help_cmd(void)
{
	printf(PROGNAME ": control various settings of a CDROM kernel driver\n\n");
	printf("Parameters:\n\n");
	printf("   --close      -c      Close tray\n");
	printf("   --device     -d      CDROM device (default is /dev/cdrom)\n");
	printf("   --eject      -e      Eject tray or caddy\n");
	printf("   --help       -h      Help\n");
	printf("   --open               Same as --eject\n");
	printf("   --option <o> -o <o>  Set or clear options\n");
	printf("                        <o> can be:\n");
	printf("                          auto-close  Auto close tray on open\n");
	printf("                          auto-eject  Auto eject tray on device close\n");
	printf("                          use-fflags  Use O_NONBLOCK to indicate that device\n");
	printf("                                      is opened only for series to ioctl's\n");
	printf("                          lock        Lock the door if device is opened\n");
	printf("                          check-type  Check disk type on opening for data\n");
	printf("                        Options can be separated by comma\n");
	printf("                        (there should be no space between)\n");
	printf("                        To clear an option simply put ``no-''\n");
	printf("                        in front of the option string\n");
	printf("   --speed <s>  -S <s>  Set drive speed\n");
	printf("                        <s> is in units of standard CDROM speed\n");
	printf("                        0 means auto select.\n");
	printf("   --status     -s      Get drive and disk status\n");
}

void get_options(int argc, char *argv[])
{
	int c, ix;

	do {
		c = getopt_long(argc, argv, optstr, opt, &ix);
		switch (c) {
		case 'S':
			speed_cmd(optarg);
			break;
		case 'd':
			device_cmd(optarg);
			break;
		case 'e':
			eject_cmd();
			break;
		case 'c':
			close_cmd();
			break;
		case 's':
			status_cmd();
			break;
		case 'o':
			option_cmd(optarg);
			break;
		case 'h':
			help_cmd();
			break;
		case '?':
			printf("Illegal option.  Use --help or -h to get help.\n");
			break;
		case (-1):
			break;
		default:
			printf("He?\n");
		}
	} while (c != -1);
}

void do_commands(void)
{
	struct cmdnode *cp;

	for (cp = cmdchain; cp != NULL; cp = cp->next) {
		if (cp->hasarg)
			cp->cmd(cp->arg);
		else
			cp->cmd();
	}
}

void init_device(void)
{
	desc = cd_open_device(cd_device, CD_OPENMODE_IOCTL);
	if (desc == -1) {
		perror(PROGNAME ": init_device");
		leave(5);
	}
}

int main(int argc, char *argv[])
{
	get_options(argc, argv);
	init_device();
	do_commands();
	exit(0);
}


