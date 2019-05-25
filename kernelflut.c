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
	(void) argc;
	(void) argv;

	int ret = 0;
	signal(SIGINT, interrupt);

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

	// FIXME dynamic
	int pf_fd = pf_connect("127.0.0.1", 1337);
	if (pf_fd < 0) {
		printf("couldn't connect to pixelflut!");
		ret = ERR_PF_CONNECT;
		goto cleanup_evdi_open;
	}
	printf("connected to pixelflut\n");

	struct pf_size ps;
	ps.w = 800; // DEBUG
	ps.h = 600; // DEBUG
	/* DEBUG
	if (!pf_size(pf_fd, &ps)) {
		printf("couldn't get size of pixelflut screen\n");
		ret = ERR_PF_SIZE;
		goto cleanup_pf_connect;
	}
	*/
	printf("DEBUG pixelflut screen is %d pixels wide by %d tall\n", ps.w, ps.h);

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

		printf("DEBUG: %d rects to update! ", dirty_rects);
		fflush(stdout); // DEBUG

		for (int i = 0; i < dirty_rects && !doomed; i++) {
			for (int y = rects[i].y1; y < rects[i].y2; y++) {
				for (int x = rects[i].x1; x < rects[i].x2; x++) {

					int j = (y * ebuf.width + x) * BYTES_PER_PIXEL;

					char b = framebuffer[j];
					char g = framebuffer[j+1];
					char r = framebuffer[j+2];

					if (!pf_set(pf_fd, x, y, r, g, b)) {
						ret = ERR_PF_SEND;
						goto cleanup_evdi_register_buffer;
					}
				}
			}
		}
	}

cleanup_evdi_register_buffer:
	evdi_unregister_buffer(ehandle, 0);
cleanup_alloc_rects:
	free(rects);
	free(framebuffer);
cleanup_evdi_connect:
	evdi_disconnect(ehandle);
//cleanup_pf_connect: // DEBUG
	close(pf_fd);
cleanup_evdi_open:
	evdi_close(ehandle);
cleanup:
	return ret;
}
