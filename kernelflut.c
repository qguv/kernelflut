#include <dirent.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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

extern const unsigned char _binary_thinkpad_edid_start[];
static bool volatile doomed = false;

void interrupt(int _)
{
	(void) _;
	doomed = true;
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

int main(int argc, char *argv[])
{
	int ret = 0;

	signal(SIGINT, interrupt);

	if (argc == 2 && argv[1][0] == '-') {
		printf("Usage:\n  sudo %s [host [port]]\n", argv[0]);
		ret = ERR_USAGE;
		goto cleanup;
	}

	char *hostname = "localhost";
	int port = 1337;

	if (argc >= 2)
		hostname = argv[1];

	if (argc >= 3) {
		port = atoi(argv[2]);
		if (port <= 0) {
			ret = ERR_BADPORT;
			goto cleanup;
		}
	}

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

	char *framebuffer = calloc(ps.w * ps.h * BYTES_PER_PIXEL, 1);
	if (framebuffer == NULL) {
		perror("couldn't allocate framebuffer");
		ret = ERR_ALLOC;
		goto cleanup_evdi_connect;
	}

	struct evdi_rect *rects = calloc(RECTS, sizeof(struct evdi_rect));
	if (rects == NULL) {
		perror("couldn't allocate rect array");
		ret = ERR_ALLOC;
		goto cleanup_alloc_rects;
	}

	struct evdi_buffer ebuf;
	memset(&ebuf, 0, sizeof(ebuf));
	ebuf.buffer = framebuffer;
	ebuf.rects = rects;
	ebuf.rect_count = RECTS;
	ebuf.width = ps.w;
	ebuf.height = ps.h;
	ebuf.stride = ps.w * BYTES_PER_PIXEL; /* RGB32, so four bytes per pixel */
	evdi_register_buffer(ehandle, ebuf);

	while (!doomed) {
		int dirty_rects;
		if (!evdi_request_update(ehandle, 0))
			continue;

		evdi_grab_pixels(ehandle, rects, &dirty_rects);
		if (!dirty_rects)
			continue;

		printf("%d", dirty_rects); fflush(stdout); // DEBUG

		int conn = 0;
		for (int i = 0; !doomed && i < dirty_rects; i++) {
			for (int round = 0; round < ROUNDS; round++) {
				for (int y = rects[i].y1; y < rects[i].y2; y++) {

					int row_round = (round + y) % ROUNDS;
					int row_bias = (row_round * STAGGER) % ROUNDS;

					for (int x = rects[i].x1 + row_bias; x < rects[i].x2; x += ROUNDS) {
						int j = (y * ebuf.width + x) * BYTES_PER_PIXEL;

						char b = framebuffer[j];
						char g = framebuffer[j+1];
						char r = framebuffer[j+2];

						if (!pf_set(pf_conns[conn++], x, y, r, g, b)) {
							ret = ERR_PF_SEND;
							goto cleanup_evdi_register_buffer;
						}
						conn %= PF_CONNS;
					}
				}
			}
		}
		printf(". "); fflush(stdout); // DEBUG
	}

cleanup_evdi_register_buffer:
	evdi_unregister_buffer(ehandle, 0);
cleanup_alloc_rects:
	free(rects);
	free(framebuffer);
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
