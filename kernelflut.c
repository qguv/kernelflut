#include <signal.h>		/* sigaction, sig_atomic_t */
#include <stdio.h>		/* perror, printf */
#include <stdlib.h>		/* atoi */
#include <string.h>		/* memset */

#include "error.h"		/* ERR_* */
#include "evdi.h"		/* evdi_setup, evdi_cleanup, evdi_get */

#define ROUNDS 5
#define STAGGER 2
#define EPOLL_TIMEOUT 100 /* ms */
#define EPOLL_NUM_EVENTS 1
#define PF_CONNS 16

#define MIN(X, Y) ((Y) < (X) ? (Y) : (X))

static volatile sig_atomic_t doomed;

static void interrupt(int _, siginfo_t *__, void *___)
{
	(void) _;
	(void) __;
	(void) ___;

	doomed = 1;
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

	(void) hostname; (void) port; // DEBUG

	int ret = evdi_setup();
	if (ret) {
		return ret;
	}

	while (!doomed) {
		printf("."); fflush(stdout); // DEBUG
		evdi_get();
	}

	evdi_cleanup();
}
