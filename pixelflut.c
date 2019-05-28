#include <fcntl.h>	/* fcntl */
#include <netdb.h>	/* gethostbyname */
#include <stdbool.h>	/* bool, true, false */
#include <stdio.h>	/* perror, dprintf */
#include <stdlib.h>	/* calloc */
#include <string.h>	/* memset */
#include <sys/socket.h> /* socket, getsockopt, setsockopt */
#include <time.h>	/* clock_gettime */
#include <unistd.h>	/* read */

#include "error.h"	/* ERR_* */

#include "pixelflut.h"

#define ROUNDS 5
#define STAGGER 2
#define REBLIT_FREQUENCY 23	/* should be prime and far from 2^n */

/* performance test */
#define PT_TRIALS 5
extern bool pt_active;
struct timespec pt_running, pt_trials[PT_TRIALS];
int pt_trial;

static int *conns;
static int active_conn_i;
static int num_conns;

int pf_increase_sndbuf(int factor)
{
	for (int i = 0; i < num_conns; i++) {
		int sndbuf;
		socklen_t optlen = sizeof(sndbuf);
		int err = getsockopt(conns[i], SOL_SOCKET, SO_SNDBUF, &sndbuf, &optlen);
		if (err)
			return err;

		fprintf(stderr, "DEBUG: socket buffer size was %d, now it's %d\n", sndbuf, sndbuf << factor);

		/* value is already doubled once on write */
		sndbuf <<= factor - 1;

		err = setsockopt(conns[i], SOL_SOCKET, SO_SNDBUFFORCE, &sndbuf, optlen);
		if (err)
			return err;
	}
	return 0;
}

int pf_asyncio(void)
{
	for (int i = 0; i < num_conns; i++) {
		int flags = fcntl(conns[i], F_GETFL);
		int err = fcntl(conns[i], F_SETFL, flags | O_ASYNC);
		if (err == -1) {
			perror("fcntl couldn't set O_ASYNC");
			return ERR_IRRECOVERABLE;
		}
	}
	return 0;
}

/*
 * pf_connect1 opens a tcp socket to a running pixelflut server and connects.
 * Returns the fd if successful, otherwise returns a negative error; please
 * multiply this error by -1 before using it.
 */
static int pf_connect1(struct addrinfo *server)
{
	struct addrinfo* p;
	int fd;
	for(p = server; p != NULL; p = p->ai_next) {
		// printf("{ai_family = %d, ai_socktype = %d, ai_protocol = %d}\n", p->ai_family, p->ai_socktype, p->ai_protocol);
		if((fd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1) {
			// perror("couldn't open socket to connect to pixelflut");
			continue;
		}
		if (connect(fd, p->ai_addr, p->ai_addrlen) < 0) {
			close(fd);
			// perror("couldn't connect to pixelflut");
			continue;
		}
		// connected succesfully
		break;
	}
	// didn't break loop, so didn't manage to make connection
	if(p == NULL) {
		perror("unable to open any socket");
		return -ERR_PF_CONNECT;
	}

	return fd;
}

/*
 * read_until reads from fd until sep is reached, then replaces sep with a null
 * terminator. Returns true if sep was reached.
 */
static bool read_until(int fd, char sep, char *buf, int buf_len)
{
	for (int i = 0; i < buf_len; i++, buf++) {
		int n = read(fd, buf, 1);
		if (n != 1)
			return false;

		if (*buf == sep) {
			*buf = '\0';
			return true;
		}
	}

	return false;
}

int pf_connect(int pool_size, char *host, int port)
{
	/* allocate memory for the file descriptor table */
	num_conns = pool_size;
	conns = calloc(num_conns, sizeof(*conns));

	/* resolve host */
	struct addrinfo *address;
	struct addrinfo hints = {
		.ai_family = AF_UNSPEC,
		.ai_socktype = SOCK_STREAM,
	};
	char service[15];
	snprintf(service, 15, "%d", port);
	int error = getaddrinfo(host, service, &hints, &address);
	if(error != 0) {
		if(error == EAI_SYSTEM) {
			perror("getaddrinfo");
		}
		else {
			herror("pixelflut: getaddrinfo hostname not found");
		}
		return ERR_PF_GETHOST;
	}

	/* connect to pixelflut */
	for (int i = 0; i < num_conns; i++) {
		int fp = pf_connect1(address);
		if (fp < 0) {
			pf_close();
			return -fp;
		}
		conns[i] = fp;
	}

	return 0;
}

/* FIXME currently broken with async I/O */
int pf_size(struct pf_size *ret)
{
	/* get next connection from pool */
	int fd = conns[active_conn_i];
	active_conn_i = (active_conn_i + 1) % num_conns;

	if (write(fd, "SIZE\n", 5) != 5) {
		perror("couldn't write size request");
		return ERR_PF_SEND;
	}

	char buf[32];
	if (!read_until(fd, '\n', buf, sizeof(buf))) {
		perror("couldn't read size response");
		return ERR_PF_RECV;
	}

	int w, h;
	if (sscanf(buf, "SIZE %12d %12d\n", &w, &h) != 2) {
		perror("couldn't parse size response");
		return ERR_PF_RECV;
	}

	ret->w = w;
	ret->h = h;
	return 0;
}

int pf_set(int x, int y, unsigned char r, unsigned char g, unsigned char b)
{
	/* get next connection from pool */
	int fd = conns[active_conn_i];
	active_conn_i = (active_conn_i + 1) % num_conns;

	int err = dprintf(fd, "PX %d %d %02x%02x%02x\n", x, y, r, g, b);
	if (err < 0)
		return ERR_PF_SEND;

	return 0;
}

int pf_set_buf(const uint32_t * const fb, const int width, const int x1, const int x2, const int y1, const int y2, const uint32_t bgcolor)
{
	static const uint32_t *last_buf = NULL;
	static int skip_reblit = 0;

	const bool ignore_bgcolor = (bgcolor == PF_NO_BGCOLOR);

	if (pt_active)
		clock_gettime(CLOCK_MONOTONIC_RAW, &pt_running);

	for (int round = 0; round < ROUNDS; round++) {

		for (int y = y1, row_start_i = y1 * width; y < y2; y++, row_start_i += width) {

			/* stagger updates in rounds so we blit pixels ASAP */
			int row_round = (round + y) % ROUNDS;
			int row_bias = (row_round * STAGGER) % ROUNDS;
			int xi = x1 + row_bias;

			for (int x = xi, i = row_start_i + xi; x < x2; x += ROUNDS, i += ROUNDS) {
				skip_reblit = (skip_reblit + 1) % REBLIT_FREQUENCY;

				/* get next connection from pool */
				int fd = conns[active_conn_i];
				active_conn_i = (active_conn_i + 1) % num_conns;

				uint32_t color = fb[i];

				/* skip redundant pixels. sometimes reblit anyway if skip_reblit reaches zero */
				if (last_buf && last_buf[i] == color && (ignore_bgcolor || (color & 0x00ffffff) == bgcolor || skip_reblit))
					continue;

				/* extract colors */
				unsigned char b = (color >>  0) & 0xff;
				unsigned char g = (color >>  8) & 0xff;
				unsigned char r = (color >> 16) & 0xff;

				/* assemble into string and print */
				dprintf(fd, "PX %d %d %02x%02x%02x\n", x, y, r, g, b);
			}
		}
	}

	if (pt_active) {
		clock_gettime(CLOCK_MONOTONIC_RAW, &pt_trials[pt_trial]);

		/* get the time difference */
		pt_trials[pt_trial].tv_sec -= pt_running.tv_sec;
		pt_trials[pt_trial].tv_nsec -= pt_running.tv_nsec;
		if (pt_trials[pt_trial].tv_nsec < 0) {
			pt_trials[pt_trial].tv_sec--;
			pt_trials[pt_trial].tv_nsec += 1000 * 1000 * 1000;
		}
		pt_trial++;

		if (pt_trial == PT_TRIALS) {
			for (pt_trial = 0; pt_trial < PT_TRIALS; pt_trial++)
				fprintf(stderr, "trial %d / %d: %lld.%09ld sec\n",
						pt_trial + 1,
						PT_TRIALS,
						(long long int) pt_trials[pt_trial].tv_sec,
						pt_trials[pt_trial].tv_nsec);
			return EXCEPTION_PT_FINISHED;
		}
	}

	last_buf = fb;
	return 0;
}

void pf_close(void)
{
	for (int i = 0; i < num_conns; i++)
		if (conns[i])
			close(conns[i]);
	num_conns = 0;

	if (conns) {
		free(conns);
		conns = NULL;
	}
}

/* vi: set ts=8 sts=8 sw=8 noet: */
