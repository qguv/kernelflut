#include <fcntl.h>	/* fcntl */
#include <netdb.h>	/* gethostbyname */
#include <stdbool.h>	/* bool, true, false */
#include <stdio.h>	/* perror, dprintf */
#include <stdlib.h>	/* calloc */
#include <string.h>	/* memset */
#include <sys/socket.h> /* socket, getsockopt, setsockopt */
#include <unistd.h>	/* read */
#include <time.h>	/* clock_gettime */

#include "error.h"	/* ERR_* */

#include "pixelflut.h"

#define ROUNDS 5
#define STAGGER 2
#define BYTES_PER_PIXEL 4 /* RGB32 */ // DEBUG

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
static int pf_connect1(struct hostent *server, int port)
{
	int fd = socket(AF_INET, SOCK_STREAM, 0);
	if (fd < 0) {
		perror("couldn't open socket to connect to pixelflut");
		return -ERR_PF_SOCKET;
	}

	struct sockaddr_in serv_addr;
	memset(&serv_addr, 0, sizeof(serv_addr));
	serv_addr.sin_family = AF_INET;
	serv_addr.sin_port = htons(port);
	memcpy((char *) &serv_addr.sin_addr.s_addr, (char *) server->h_addr, server->h_length);

	if (connect(fd, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) < 0) {
		perror("couldn't connect to pixelflut");
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

	/* resolve host TODO: ipv6 */
	struct hostent *server = gethostbyname(host);
	if (server == NULL) {
		herror("pixelflut hostname not found");
		return ERR_PF_GETHOST;
	}

	/* connect to pixelflut */
	for (int i = 0; i < num_conns; i++) {
		int fp = pf_connect1(server, port);
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

int pf_set_buf(unsigned char *fb, int width, int x1, int x2, int y1, int y2)
{
	if (pt_active)
		clock_gettime(CLOCK_MONOTONIC_RAW, &pt_running);

	for (int round = 0; round < ROUNDS; round++) {
		for (int y = y1; y < y2; y++) {

			int row_round = (round + y) % ROUNDS;
			int row_bias = (row_round * STAGGER) % ROUNDS;

			for (int x = x1 + row_bias; x < x2; x += ROUNDS) {

				/* get next connection from pool */
				int fd = conns[active_conn_i];
				active_conn_i = (active_conn_i + 1) % num_conns;

				/* extract colors */
				unsigned char b = fb[(y * width + x) * BYTES_PER_PIXEL + 0];
				unsigned char g = fb[(y * width + x) * BYTES_PER_PIXEL + 1];
				unsigned char r = fb[(y * width + x) * BYTES_PER_PIXEL + 2];

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
