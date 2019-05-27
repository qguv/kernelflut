#pragma once

#include <stdint.h>	/* uint16_t */

#define PF_NO_BGCOLOR 0x80000000

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
 * pf_set_buf tells pixelflut at fd to set a bunch of RGB32 pixels from fb. If
 * bgcolor isn't PF_NO_BGCOLOR, then occasionally repaint every pixel except
 * this color. Returns 0 on success.
 */
int pf_set_buf(const uint32_t * const fb, const int width, const int x1, const int x2, const int y1, const int y2, const uint32_t bgcolor);

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
