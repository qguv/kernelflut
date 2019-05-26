#include <netdb.h>	/* gethostbyname */
#include <stdio.h>	/* perror, dprintf */
#include <string.h>	/* memset */
#include <sys/socket.h> /* socket, getsockopt, setsockopt */
#include <unistd.h>	/* read */
#include <fcntl.h>	/* fcntl */

#include "error.h"	/* ERR_* */

#include "pixelflut.h"

static int accelerate(int fd)
{
	int sndbuf;
	socklen_t optlen = sizeof(sndbuf);
	int err = getsockopt(fd, SOL_SOCKET, SO_SNDBUF, &sndbuf, &optlen);
	if (err)
		return err;

	printf("socket buffer size was %d, now it's %d\n", sndbuf, sndbuf << 3);

	sndbuf <<= 2;

	/* value is doubled once again on write */
	err = setsockopt(fd, SOL_SOCKET, SO_SNDBUFFORCE, &sndbuf, optlen);
	if (err)
		return err;

	int flags = fcntl(fd, F_GETFL);
	err = fcntl(fd, F_SETFL, flags | O_ASYNC);
	if (err == -1) {
		perror("fcntl couldn't set O_ASYNC");
		return ERR_IRRECOVERABLE;
	}

	return 0;
}

int pf_connect(char *host, int port)
{
	int fd = socket(AF_INET, SOCK_STREAM, 0);
	if (fd < 0) {
		perror("couldn't open socket to connect to pixelflut");
		return -ERR_PF_SOCKET;
	}

	int err = accelerate(fd);
	if (err) {
		perror("socket acceleration failed");
		return -ERR_PF_ACCEL;
	}

	// TODO: ipv6
	struct hostent *server = gethostbyname(host);
	if (server == NULL) {
		herror("pixelflut hostname not found");
		return -ERR_PF_GETHOST;
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

bool pf_size(int fd, struct pf_size *ret)
{
	if (write(fd, "SIZE\n", 5) != 5) {
		perror("couldn't write size request");
		return false;
	}

	char buf[32];
	if (!read_until(fd, '\n', buf, sizeof(buf))) {
		perror("couldn't read size response");
		return false;
	}

	int w, h;
	if (sscanf(buf, "SIZE %12d %12d\n", &w, &h) != 2) {
		perror("couldn't read size response");
		return false;
	}

	ret->w = w;
	ret->h = h;
	return true;
}

bool pf_set(int fd, int x, int y, unsigned char r, unsigned char g, unsigned char b)
{
	return dprintf(fd, "PX %d %d %02x%02x%02x\n", x, y, r, g, b) > 0;
}

/* vi: set ts=8 sts=8 sw=8 noet: */
