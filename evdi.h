#pragma once

#ifndef EVDI_LIB_H
struct evdi_rect {
	int x1, y1, x2, y2;
};
#endif

struct evdi_update {
	char *fb;
	struct evdi_rect *rects;
	int num_rects;
};

/*
 * setup_evdi creates a virtual display and framebuffers. Returns 0 on success
 * and an exit code >0 on failure. If return value is 0, cleanup_evdi MUST be
 * called before program exit. If return value is nonzero, cleanup_evdi doesn't
 * need to be called, but it shouldn't cause nasty behavior.
 */
int evdi_setup(void);

/*
 * evdi_cleanup deregisters EVDI objects and deallocates their memory.
 * Redundant calls are safe.
 */
void evdi_cleanup(void);

/*
 * evdi_get gets the next available frame update from EVDI. Returns 0 on
 * success and an exit code >0 on failure. Tries to get an immediately ready
 * framebuffer, otherwise waits until one is ready. evdi_setup() must already
 * have been called.
 */
int evdi_get(struct evdi_update *update);

/* vi: set ts=8 sts=8 sw=8 noet: */
