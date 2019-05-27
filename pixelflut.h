#pragma once

#include <stdint.h>	/* uint16_t */

struct pf_size {
	int w;
	int h;
};

/*
 * pf_connect opens a pool of tcp sockets to a running pixelflut server.
 * Returns 0 on success.
 */
int pf_connect(int pool_size, char *host, int port);

/* pf_size asks pixelflut for its current dimensions. Returns 0 on success. */
int pf_size(struct pf_size *ret);

/*
 * pf_set tells pixelflut at fd to set pixel (x, y) to color rrggbb. Returns 0
 * on success.
 */
int pf_set(int x, int y, unsigned char r, unsigned char g, unsigned char b);

/*
 * pf_set_buf tells pixelflut at fd to set a bunch of RGB32 pixels from fb.
 * Returns 0 on success.
 */
int pf_set_buf(uint32_t *fb, int width, int x1, int x2, int y1, int y2);

/*
 * pf_close closes the connection pool opened by pf_connect and deallocates its
 * memory. Redundant calls are safe.
 */
void pf_close(void);

/*
 * pf_increase_sndbuf reconfigures open pixelflut sockets to increase their
 * send buffers SO_SNDBUF. The value is doubled `factor` times. Returns 0 on
 * success.
 */
int pf_increase_sndbuf(int factor);

/*
 * pf_increase_sndbuf reconfigures open pixelflut sockets to use asynchronous
 * I/O. Returns 0 on success.
 */
int pf_asyncio(void);

/* vi: set ts=8 sts=8 sw=8 noet: */
