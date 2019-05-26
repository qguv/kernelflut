#pragma once

#include <stdbool.h>	/* bool */

struct pf_size {
	int w;
	int h;
};

/*
 * pf_connect opens a tcp socket and connects to a running pixelflut server.
 * Returns the fd if successful, otherwise returns a negative error.
 */
int pf_connect(char *host, int port);

/* pf_size asks pixelflut for its current dimensions */
bool pf_size(int fd, struct pf_size *ret);

/* pf_set tells pixelflut to set pixel (x, y) to color rrggbb */
bool pf_set(int fd, int x, int y, unsigned char r, unsigned char g, unsigned char b);

/* vi: set ts=8 sts=8 sw=8 noet: */
