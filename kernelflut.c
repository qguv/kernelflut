#include <dirent.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/types.h>
#include <unistd.h>

#include "evdi_lib.h"
#include "pixelflut.h"
#include "kernelflut.h"

#define BYTES_PER_PIXEL 4
#define RECTS 16
#define PF_CONNS 16
#define ROUNDS 5
#define STAGGER 2
#define EPOLL_TIMEOUT 100 /* ms */
#define EPOLL_NUM_EVENTS 1
#define FRAMEBUFFERS 16

extern const unsigned char _binary_thinkpad_edid_start[];
static volatile sig_atomic_t doomed = 0;
static volatile sig_atomic_t update_ready = 0;

void interrupt(int _, siginfo_t *__, void *___)
{
	(void) _;
	(void) __;
	(void) ___;

	doomed = 1;
}

void update_ready_handler(int _, void *__)
{
	(void) _;
	(void) __;

	update_ready = 1;
}

/*
 * get_first_device looks through registered cardX entries in /dev/dri to try
 * to find one managed by EVDI; returns -1 if it can't find one
 */
int get_first_device()
{
	DIR *dp;
	dp = opendir("/dev/dri");
	if (dp == NULL) {
		perror("couldn't get evdi cards");
		return -1;
	}

	int card;
	struct dirent *ep;
	while ((ep = readdir(dp)))
		if (sscanf(ep->d_name, "card%d", &card) == 1 && evdi_check_device(card) == AVAILABLE)
			return card;

	return -1;
}

int setup(char *hostname, int port)
{
	int ret = 0;

	int card = get_first_device();
	if (card < 0) {
		if (!evdi_add_device()) {
			printf("couldn't create EVDI device; did you forget to `insmod evdi.ko`?\n");
			ret = ERR_EVDI_ADD;
			goto cleanup;
		}

		card = get_first_device();
		if (card < 0) {
			printf("couldn't find newly created evdi device\n");
			ret = ERR_EVDI_FIND;
			goto cleanup;
		}
	}
	printf("card is %d\n", card);

	evdi_handle ehandle = evdi_open(card);
	if (ehandle == EVDI_INVALID_HANDLE) {
		perror("couldn't open evdi device");
		ret = ERR_EVDI_OPEN;
		goto cleanup;
	}

	printf("connecting to pixelflut");
	fflush(stdout);

	int pf_conns[PF_CONNS];
	memset(pf_conns, 0, sizeof(pf_conns));
	for (int i = 0; !doomed && i < PF_CONNS; i++) {
		printf("-");
		fflush(stdout);

		pf_conns[i] = pf_connect(hostname, port);
		if (pf_conns[i] < 0) {
			ret = ERR_PF_CONNECT;
			goto cleanup_pf_connect;
		}

		printf("\b.");
		fflush(stdout);
	}
	if (doomed)
		goto cleanup_pf_connect;
	printf(" ok\n");

	struct pf_size ps;
	/* DEBUG
	ps.w = 1366;
	ps.h = 768;
	DEBUG */ps.w = 800; ps.h = 600;

	const uint32_t sku_area_limit = ps.w * ps.h;
	evdi_connect(ehandle, _binary_thinkpad_edid_start, 128, sku_area_limit);

	struct evdi_rect rects[16][FRAMEBUFFERS];
	memset(&rects, 0, sizeof(rects));

	struct evdi_buffer ebufs[FRAMEBUFFERS];
	memset(&ebufs, 0, sizeof(ebufs));
	for (int fbid = 0; fbid < FRAMEBUFFERS; fbid++) {
		char *fbuf = calloc(ps.w * ps.h, BYTES_PER_PIXEL);
		if (fbuf == NULL) {
			perror("couldn't allocate framebuffer");
			ret = ERR_ALLOC;
			goto cleanup_evdi_connect;
		}

		ebufs[fbid].id = fbid;
		ebufs[fbid].buffer = fbuf;
		ebufs[fbid].width = ps.w;
		ebufs[fbid].height = ps.h;
		ebufs[fbid].stride = ps.w * BYTES_PER_PIXEL; /* RGB32, so four bytes per pixel */
		ebufs[fbid].rects = rects[fbid];
		ebufs[fbid].rect_count = RECTS;
		evdi_register_buffer(ehandle, ebufs[fbid]);
	};

	/* don't need to close this */
	evdi_selectable evdi_fd = evdi_get_event_ready(ehandle);

	int epfd = epoll_create1(0);
	if (epfd == -1) {
		perror("DEBUG epoll_create1 failed somehow");
		ret = ERR_IRRECOVERABLE;
		goto cleanup_evdi_register_buffer;
	}

	struct epoll_event event, events[EPOLL_NUM_EVENTS];
	event.events = EPOLLIN;
	event.data.fd = evdi_fd;
	if (epoll_ctl(epfd, EPOLL_CTL_ADD, evdi_fd, &event)) {
		perror("DEBUG epoll_ctl failed somehow");
		ret = ERR_IRRECOVERABLE;
		goto cleanup_epoll_create1;
	}

	struct evdi_event_context econtext;
	memset(&econtext, 0, sizeof(econtext));
	econtext.update_ready_handler = update_ready_handler;

	for (int fbid = 0; !doomed; fbid = (fbid + 1) % FRAMEBUFFERS) {
		struct evdi_buffer *ebuf = &ebufs[fbid];
		char *fbuf = ebuf->buffer;

		if (evdi_request_update(ehandle, fbid)) {
			printf("D"); fflush(stdout); // DEBUG a request directly responded to us
		} else {
			while (!doomed && !update_ready) {
				int w = epoll_wait(epfd, events, EPOLL_NUM_EVENTS, 100);
				if (!w)
					continue;
				if (w == -1) {
					perror("epoll");
					ret = ERR_IRRECOVERABLE;
					goto cleanup_epoll_create1;
				}
				evdi_handle_events(ehandle, &econtext);
			}
			if (doomed)
				goto cleanup_epoll_create1;
			update_ready = 0;
			printf("I"); fflush(stdout); // DEBUG a request caused an indirect reponse
		}

		int dirty_rects;
		evdi_grab_pixels(ehandle, rects[fbid], &dirty_rects);
		printf("%d\n", dirty_rects); fflush(stdout); // DEBUG number of dirty rectangles

		if (!dirty_rects)
			continue;

		int DEBUG_frame = !DEBUG_frame;
		int conn = 0;
		for (int i = 0; !doomed && i < dirty_rects; i++) {
			printf(" %d w=%4d h=%4d\n", i, rects[fbid][i].x2 - rects[fbid][i].x1, rects[fbid][i].y2 - rects[fbid][i].y1); fflush(stdout); // DEBUG
			for (int round = 0; round < ROUNDS; round++) {
				for (int y = rects[fbid][i].y1; y < rects[fbid][i].y2; y++) {

					int row_round = (round + y) % ROUNDS;
					int row_bias = (row_round * STAGGER) % ROUNDS;

					for (int x = rects[fbid][i].x1 + row_bias; x < rects[fbid][i].x2; x += ROUNDS) {
						int j = (y * ebuf->width + x) * BYTES_PER_PIXEL;

						char b = fbuf[j];
						char g = fbuf[j+1];
						char r = fbuf[j+2];

						if (!pf_set(pf_conns[conn++], x, y, r, g, b)) {
							ret = ERR_PF_SEND;
							goto cleanup_evdi_register_buffer;
						}
						conn %= PF_CONNS;
					}
				}
			}
		}
		printf(DEBUG_frame ? ". " : " ."); fflush(stdout);
	}

cleanup_epoll_create1:
	close(epfd);
cleanup_evdi_register_buffer:
	for (int fbid = 0; fbid < FRAMEBUFFERS; fbid++)
		evdi_unregister_buffer(ehandle, fbid);
cleanup_alloc_framebuffers:
	for (int fbid = 0; fbid < FRAMEBUFFERS; fbid++)
		if (ebufs[fbid].buffer != NULL)
			free(ebufs[fbid].buffer);
cleanup_evdi_connect:
	evdi_disconnect(ehandle);
cleanup_pf_connect:
	for (int i = 0; i < PF_CONNS; i++)
		if (pf_conns[i] > 0)
			close(pf_conns[i]);
cleanup_evdi_open:
	evdi_close(ehandle);
cleanup:
	return ret;
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

	return setup(hostname, port);
}
