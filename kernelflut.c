#include <signal.h>	/* sigaction, sig_atomic_t */
#include <stdio.h>	/* perror, printf */
#include <stdlib.h>	/* atoi */
#include <string.h>	/* memset */
#include <unistd.h>	/* close */

#include "pixelflut.h"	/* pf_connect */
#include "error.h"	/* ERR_* */
#include "evdi.h"	/* evdi_setup, evdi_cleanup, evdi_get */

#define ROUNDS 5
#define STAGGER 2
#define EPOLL_TIMEOUT 100 /* ms */
#define EPOLL_NUM_EVENTS 1
#define PF_CONNS 16
#define BYTES_PER_PIXEL 4 /* RGB32 */

#define MIN(X, Y) ((Y) < (X) ? (Y) : (X))

volatile sig_atomic_t doomed;

int pf_conns[PF_CONNS];

static void interrupt(int _, siginfo_t *__, void *___)
{
	(void) _;
	(void) __;
	(void) ___;

	doomed = 1;
}

static int loop(int width)
{
	struct evdi_update update;

	int conn = 0;
	for (;;) {
		if (doomed)
			return ERR_INT;

		printf("."); fflush(stdout); // DEBUG

		int err = evdi_get(&update);
		if (err)
			return err;

		for (int rect = 0; rect < update.num_rects; rect++) {
			printf(" %d/%d w=%4d h=%4d\n", rect + 1, update.num_rects, update.rects[rect].x2 - update.rects[rect].x1, update.rects[rect].y2 - update.rects[rect].y1); fflush(stdout); // DEBUG
			for (int round = 0; round < ROUNDS; round++) {

				int x1 = update.rects[rect].x1;
				int y1 = update.rects[rect].y1;
				int x2 = update.rects[rect].x2;
				int y2 = update.rects[rect].y2;

				for (int y = y1; y < y2; y++) {

					int row_round = (round + y) % ROUNDS;
					int row_bias = (row_round * STAGGER) % ROUNDS;

					for (int x = x1 + row_bias; x < x2; x += ROUNDS) {
						int j = (y * width + x) * BYTES_PER_PIXEL;

						char b = update.fb[j];
						char g = update.fb[j+1];
						char r = update.fb[j+2];

						if (!pf_set(pf_conns[conn++], x, y, r, g, b))
							return ERR_PF_SEND;

						conn %= PF_CONNS;
					}
				}
			}
		}
	}

	return 0;
}

int main(int argc, char *argv[])
{
	struct sigaction act;
	memset(&act, 0, sizeof(act));
	sigemptyset(&act.sa_mask);
	act.sa_flags = SA_SIGINFO;
	act.sa_sigaction = interrupt;
	if (sigaction(SIGINT, &act, NULL) == -1) {
		perror("DEBUG sigaction failed somehow");
		return ERR_IRRECOVERABLE;
	}

	if (argc == 2 && argv[1][0] == '-') {
		printf("Usage:\n  sudo %s [host [port]]\n", argv[0]);
		return ERR_USAGE;
	}

	char *hostname = "localhost";
	int port = 1337;

	if (argc >= 2)
		hostname = argv[1];

	if (argc >= 3) {
		port = atoi(argv[2]);
		if (port <= 0) {
			return ERR_BADPORT;
		}
	}

	/* connect to pixelflut */
	for (int i = 0; i < PF_CONNS; i++) {
		pf_conns[i] = pf_connect(hostname, port);
		if (pf_conns[i] < 0) {
			for (int j = 0; j < i; j++)
				close(pf_conns[j]);
			return -pf_conns[i];
		}
	}

	int ret = evdi_setup();
	if (ret)
		return ret;

	const int width = 800; // DEBUG
	ret = loop(width);

	for (int i = 0; i < PF_CONNS; i++)
		close(pf_conns[i]);

	evdi_cleanup();

	return ret;
}

/* vi: set ts=8 sts=8 sw=8 noet: */
