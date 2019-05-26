#pragma once

#include <stdbool.h>	/* bool */

struct pf_size {
	int w;
	int h;
};

int pf_connect(char *host, int port);
bool pf_size(int fd, struct pf_size *ret);
bool pf_set(int fd, int x, int y, unsigned char r, unsigned char g, unsigned char b);
