#include <stdio.h>
#include <stdint.h>
#include "evdi_lib.h"
#include "kernelflut.h"

#define NUM_POSSIBLE_CARDS 64

extern const char _binary_thinkpad_edid_start[];
extern const size_t _binary_thinkpad_edid_size;

/*
 * FIXME: NOT IMPLEMENTED
 * asks pixelflut for its size
 * returns 1 on success, 0 on failure
 */
int get_pixelflut_size(struct pixelflut_size *ps)
{
	ps->w = 1920;
	ps->h = 1080;
	return 1;
}

int main(void)
{
	int ok = evdi_add_device();
	if (!ok) {
		perror("couldn't create new evdi device");
		return 1;
	}

	int cardid;
	evdi_device_status devstatus;
	evdi_handle devhandle;
	int found = 0;
	for (cardid = 0; cardid < NUM_POSSIBLE_CARDS; cardid++) {

		devstatus = evdi_check_device(cardid);
		if (devstatus != AVAILABLE)
			continue;

		evdi_handle devhandle = evdi_open(cardid);
		if (devhandle == EVDI_INVALID_HANDLE)
			continue;

		found = 1;
		break;
	}

	if (!found) {
		printf("couldn't find an available evdi device, even after making one\n");
		return 2;
	}

	struct pixelflut_size ps;
	ok = get_pixelflut_size(&ps);
	if (!ok) {
		printf("couldn't get size of pixelflut screen\n");
		return 3;
	}

	const uint32_t sku_area_limit = ps.w * ps.h;
	evdi_connect(devhandle, _binary_thinkpad_edid_start, _binary_thinkpad_edid_size, sku_area_limit);

	//struct evdi_buffer ebuf;
	//evdi_register_buffer(...);
}
