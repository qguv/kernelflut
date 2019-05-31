#include <signal.h>	/* sigaction, sig_atomic_t */
#include <stdbool.h>	/* bool, true, false */
#include <stdio.h>	/* perror, printf */
#include <stdlib.h>	/* atoi, strtoul */
#include <string.h>	/* memset */
#include <unistd.h>	/* close, getopt */

#include "pixelflut.h"	/* pf_connect */
#include "error.h"	/* ERR_* */
#include "evdi.h"	/* evdi_setup, evdi_cleanup, evdi_get */

#define EPOLL_TIMEOUT 100 /* ms */
#define EPOLL_NUM_EVENTS 1

#define MIN(X, Y) ((Y) < (X) ? (Y) : (X))

#define DEFAULT_HOSTNAME	"localhost"
#define DEFAULT_PORT		1337
#define DEFAULT_CONNECTIONS	8

/* performance test */
bool pt_active;

volatile sig_atomic_t doomed;

static void interrupt(int _, siginfo_t *__, void *___)
{
	(void) _;
	(void) __;
	(void) ___;

	doomed = 1;
}

static int loop(int width, uint32_t bgcolor)
{
	struct evdi_update update;

	int DEBUG_alternate = 0;
	for (;;) {
		DEBUG_alternate ^= 1;
		if (doomed)
			return EXCEPTION_INT;

		int err = evdi_get(&update);
		if (err)
			return err;

		for (int rect = 0; rect < update.num_rects; rect++) {
#ifdef DEBUG
			printf(DEBUG_alternate ? ". " : " .");
			printf("DEBUG %dx%d (%d of %d)\n",
					update.rects[rect].x2 - update.rects[rect].x1, // DEBUG
					update.rects[rect].y2 - update.rects[rect].y1, // DEBUG
					rect + 1, update.num_rects // DEBUG
			); fflush(stdout); // DEBUG
#endif

			err = pf_set_buf((uint32_t *) update.fb, width,
					update.rects[rect].x1, update.rects[rect].x2,
					update.rects[rect].y1, update.rects[rect].y2,
					bgcolor);
			if (err)
				return err;
		}
	}

	return 0;
}

/* usage prints usage info to stderr. It returns ERR_BADARG for convenience. */
static int usage(char *progname)
{
	fprintf(stderr,
		"Usage:\n"
		"  sudo %s [options...] [HOST [PORT]] \n"
		"\n"
		"Arguments:\n"
		"  HOST			pixelflut hostname (default " DEFAULT_HOSTNAME ")\n"
		"  PORT			pixelflut port (default %d)\n"
		"\n"
		"Options:\n"
		"  -a			use async i/o\n"
		"  -b RRGGBB		occasionally blit every pixel except this one\n"
		"  -c CONNECTIONS	size of pixelflut connection pool (default %d)\n"
		"  -d WxH		scale down to width W and height H\n"
		"  -o X,Y		move the top-left corner down by Y pixels and right by X pixels\n"
		"  -s			increase SO_SNDBUF socket buffers by 2x (can pass multiple times)\n"
		"  -p			do a performance test (time five screen updates)\n"
		"",
		progname,
		DEFAULT_PORT,
		DEFAULT_CONNECTIONS
	);
	return ERR_BADARG;
}

int main(int argc, char *argv[])
{
	/* handle SIGINT gracefully */
	struct sigaction act;
	memset(&act, 0, sizeof(act));
	sigemptyset(&act.sa_mask);
	act.sa_flags = SA_SIGINFO;
	act.sa_sigaction = interrupt;
	if (sigaction(SIGINT, &act, NULL) == -1) {
		perror("DEBUG sigaction failed somehow");
		return ERR_IRRECOVERABLE;
	}

	/* parse flags and options */
	bool asyncio = false;
	int connections = DEFAULT_CONNECTIONS;
	int sndbuf_shift = 0;
	pt_active = false;

	/* TODO implement */
	int constrain_width = 0;
	int constrain_height = 0;

	/* TODO implement */
	int origin_x = 0;
	int origin_y = 0;

	uint32_t bgcolor = PF_NO_BGCOLOR;

	char c;
	int opt;
	while ((opt = getopt(argc, argv, "ab:c:d:o:sph?")) != -1) {
		switch (opt) {
		case 'a':
			asyncio = true;
			break;
		case 'b':
			bgcolor = strtoul(optarg, NULL, 16);
			if (bgcolor > 0x00ffffff)
				return usage(argv[0]);
			break;
		case 'c':
			connections = atoi(optarg);
			if (connections <= 0)
				return usage(argv[0]);
			break;
		case 'd':
			constrain_width = atoi(optarg);
			if (constrain_width <= 0)
				return usage(argv[0]);

			/* read until we encounter an x */
			while ((c = *optarg++) && c != 'x');
			if (!c)
				return usage(argv[0]);

			constrain_height = atoi(optarg);
			if (constrain_height <= 0)
				return usage(argv[0]);
			break;
		case 'o':
			origin_x = atoi(optarg);
			if (origin_x <= 0)
				return usage(argv[0]);

			/* read until we encounter a comma */
			while ((c = *optarg++) && c != ',');
			if (!c)
				return usage(argv[0]);

			origin_y = atoi(optarg);
			if (origin_y <= 0)
				return usage(argv[0]);
			break;
		case 's':
			sndbuf_shift++;
			break;
		case 'p':
			pt_active = true;
			break;
		case 'h':
		case '?':
			usage(argv[0]);
			return 0;
		default:
			return usage(argv[0]);
		}
	}

	/* parse positional arguments */
	char *hostname = DEFAULT_HOSTNAME;
	if (optind < argc)
		hostname = argv[optind++];

	int port = DEFAULT_PORT;
	if (optind < argc) {
		port = atoi(argv[optind++]);
		if (port <= 0)
			return usage(argv[0]);
	}

	/* too many positional arguments */
	if (optind < argc)
		return usage(argv[0]);

	int err = pf_connect(connections, hostname, port);
	if (err)
		return err;

	if (asyncio) {
		err = pf_asyncio();
		if (err)
			return err;
	}

	if (sndbuf_shift) {
		err = pf_increase_sndbuf(sndbuf_shift);
		if (err)
			return err;
	}

	err = evdi_setup();
	if (err)
		return err;

	const int width = 800; // DEBUG
	err = loop(width, bgcolor);
	if (err == EXCEPTION_PT_FINISHED || err == EXCEPTION_INT)
		err = 0;

	pf_close();

	evdi_cleanup();

	return err;
}

/* vi: set ts=8 sts=8 sw=8 noet: */
