#pragma once

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
 * evdi_get requests a new framebuffer update from EVDI. evdi_setup() must
 * already have been called.
 */
void evdi_get(void);
