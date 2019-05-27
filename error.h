#pragma once

/* general errors */
#define ERR_BADARG	1
#define ERR_ALLOC	2
#define ERR_IRRECOVERABLE 3

/* evdi errors */
#define ERR_EVDI_ADD	10
#define ERR_EVDI_FIND	11
#define ERR_EVDI_OPEN	12
#define ERR_EVDI_EPOLL	13

/* pixelflut errrors */
#define ERR_PF_SOCKET	20
#define ERR_PF_GETHOST	21
#define ERR_PF_CONNECT	22
#define ERR_PF_SIZE	23
#define ERR_PF_SEND	24
#define ERR_PF_RECV	25
#define ERR_PF_ACCEL	26

/* non-errors (should never exit with these) */
#define EXCEPTION_PT_FINISHED	30
#define EXCEPTION_INT		31

/* vi: set ts=8 sts=8 sw=8 noet: */
