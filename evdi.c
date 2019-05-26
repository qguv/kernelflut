#include <dirent.h>		/* readdir */
#include <signal.h>		/* sig_atomic_t */
#include <stdio.h>		/* perror, printf */
#include <stdlib.h>		/* calloc */
#include <sys/epoll.h>	/* epoll */
#include <unistd.h>		/* close */

#include "error.h"		/* ERR_* */
#include "evdi_lib.h"	/* evdi_* */

#include "evdi.h"

#define BYTES_PER_PIXEL 4
#define RECTS 16
#define FRAMEBUFFERS 16

/* binary data included from *.edid file */
#define EDID_OBJNAME _binary_thinkpad_edid_start
#define EDID_SIZE 128
extern const unsigned char EDID_OBJNAME[];

static evdi_handle ehandle;
static bool evdi_connected;

static struct evdi_buffer ebufs[FRAMEBUFFERS];
static bool evdi_buffer_registered[FRAMEBUFFERS];

static struct evdi_rect rects[RECTS][FRAMEBUFFERS];

/* econtext holds EVDI event handler callback function pointers */
static struct evdi_event_context econtext;

static int epoll_fd;

static volatile sig_atomic_t update_ready = 0;

/*
 * update_ready_handler is called when EVDI asynchronous framebuffer updates
 * are ready. If this is getting called, it means we're requesting frames
 * faster than EVDI can givem them to us, which is great!
 */
static void update_ready_handler(int _, void *__)
{
	(void) _;
	(void) __;

	update_ready = 1;
}

/*
 * get_first_device looks through registered cardX entries in /dev/dri to try
 * to find one managed by EVDI; returns -1 if it can't find one
 */
static int get_first_device(void)
{
	DIR *dp;
	dp = opendir("/dev/dri");
	if (dp == NULL) {
		perror("couldn't get EVDI cards");
		return -1;
	}

	int card;
	struct dirent *ep;
	while ((ep = readdir(dp)))
		if (sscanf(ep->d_name, "card%d", &card) == 1 && evdi_check_device(card) == AVAILABLE)
			return card;

	return -1;
}

static int setup_epoll(void)
{
	/* don't need to close this */
	evdi_selectable evdi_fd = evdi_get_event_ready(ehandle);

	int fd = epoll_create1(0);
	if (fd == -1) {
		perror("epoll_create1");
		return ERR_IRRECOVERABLE;
	}
	epoll_fd = fd;

	struct epoll_event event;
	event.events = EPOLLIN;
	event.data.fd = evdi_fd;
	if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, evdi_fd, &event)) {
		perror("epoll_ctl");
		return ERR_IRRECOVERABLE;
	}

	return 0;
}

int evdi_setup(void)
{
	int card = get_first_device();
	if (card < 0) {
		if (!evdi_add_device()) {
			printf("couldn't create EVDI device; did you forget to `insmod evdi.ko`?\n");
			return ERR_EVDI_ADD;
		}

		card = get_first_device();
		if (card < 0) {
			printf("couldn't find newly created EVDI device\n");
			return ERR_EVDI_FIND;
		}
	}

	printf("DEBUG card%d is open\n", card);

	{
		evdi_handle eh = evdi_open(card);
		if (eh == EVDI_INVALID_HANDLE) {
			perror("couldn't open EVDI device");
			return ERR_EVDI_OPEN;
		}
		ehandle = eh;
	}
	const int width = 800; // DEBUG
	const int height = 600; // DEBUG

	const uint32_t sku_area_limit = width * height;
	evdi_connect(ehandle, EDID_OBJNAME, EDID_SIZE, sku_area_limit);
	evdi_connected = true;

	for (int fbid = 0; fbid < FRAMEBUFFERS; fbid++) {
		char *fbuf = calloc(width * height, BYTES_PER_PIXEL);
		if (fbuf == NULL) {
			perror("couldn't allocate framebuffer");
			evdi_cleanup();
			return ERR_ALLOC;
		}

		ebufs[fbid].id = fbid;
		ebufs[fbid].buffer = fbuf;
		ebufs[fbid].width = width;
		ebufs[fbid].height = height;
		ebufs[fbid].stride = width * BYTES_PER_PIXEL; /* RGB32, so four bytes per pixel */
		ebufs[fbid].rects = rects[fbid];
		ebufs[fbid].rect_count = RECTS;
		evdi_register_buffer(ehandle, ebufs[fbid]);
		evdi_buffer_registered[fbid] = true;
	};

	int ret = setup_epoll();
	if (ret) {
		evdi_cleanup();
		return ret;
	}

	/* register event handlers */
	econtext.update_ready_handler = update_ready_handler;

	return 0;
}

void evdi_cleanup(void)
{
	/* close epoll fd */
	if (epoll_fd) {
		close(epoll_fd);
		epoll_fd = 0;
	}

	if (ehandle != NULL) {

		/* unregister framebuffer containers */
		for (int fbid = 0; fbid < FRAMEBUFFERS; fbid++) {
			if (evdi_buffer_registered[fbid]) {
				evdi_unregister_buffer(ehandle, fbid);
				evdi_buffer_registered[fbid] = false;
			}

			/* free framebuffer memory */
			if (ebufs[fbid].buffer != NULL) {
				free(ebufs[fbid].buffer);
				ebufs[fbid].buffer = NULL;
			}
		}

		/* disconnect virtual display */
		if (evdi_connected) {
			evdi_disconnect(ehandle);
			evdi_connected = false;
		}

		/* disconnects from EVDI */
		evdi_close(ehandle);
		ehandle = NULL;
	}
}

/*
 * evdi_get requests a new framebuffer update from EVDI. evdi_setup() must
 * already have been called.
 */
void evdi_get(void)
{
	static int fbid = 0;
	evdi_request_update(ehandle, fbid);
	fbid = (fbid + 1) % FRAMEBUFFERS;
}
