#include <dirent.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

#include "evdi_lib.h"
#include "kernelflut.h"

#define BYTES_PER_PIXEL 4

extern const unsigned char _binary_thinkpad_edid_start[];
static bool volatile doomed = false;

void interrupt(int _)
{
	(void) _;
	doomed = true;
}

/*
 * get_pixelflut_size asks pixelflut for its height and width; returns 1 on
 * success, 0 on failure
 *
 * FIXME: NOT ACTUALLY IMPLEMENTED
 */
int get_pixelflut_size(struct pixelflut_size *ps)
{
	ps->w = 1920;
	ps->h = 1080;
	return 1;
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

int main(void)
{
	signal(SIGINT, interrupt);

	int card = get_first_device();
	if (card < 0) {
		if (!evdi_add_device()) {
			perror("couldn't create new evdi device");
			return 1;
		}
		card = get_first_device();
		if (card < 0) {
			printf("couldn't find newly created evdi device\n");
			return 2;
		}
	}
	printf("card is %d\n", card);

	evdi_handle ehandle = evdi_open(card);
	if (ehandle == EVDI_INVALID_HANDLE) {
		perror("couldn't open evdi device");
		return 3;
	}

	struct pixelflut_size ps;
	if (!get_pixelflut_size(&ps)) {
		printf("couldn't get size of pixelflut screen\n");
		return 4;
	}

	const uint32_t sku_area_limit = ps.w * ps.h;
	evdi_connect(ehandle, _binary_thinkpad_edid_start, 128, sku_area_limit);

	void *framebuffer = calloc(ps.w * ps.h * BYTES_PER_PIXEL, 1);
	if (framebuffer == NULL) {
		perror("couldn't allocate framebuffer");
		return 5;
	}

	struct evdi_buffer ebuf;
	memset(&ebuf, 0, sizeof(ebuf));
	ebuf.buffer = framebuffer;
	ebuf.width = ps.w;
	ebuf.height = ps.h;
	ebuf.stride = ps.w * 4; /* RGB32, so four bytes per pixel */
	evdi_register_buffer(ehandle, ebuf);

	while (!doomed);

	evdi_unregister_buffer(ehandle, 0);
	evdi_disconnect(ehandle);
	evdi_close(ehandle);
	return 0;
}
