#include <netdb.h>
#include <netinet/in.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "pixelflut.h"

/* pf_connect connects to a running pixelflut server with tcp over ipv4 */
int pf_connect(char *host, int port)
{
   int fd = socket(AF_INET, SOCK_STREAM, 0);
   if (fd < 0) {
      perror("couldn't open socket to connect to pixelflut");
      return -1;
   }

   // TODO: ipv6
   struct hostent *server = gethostbyname(host);
   if (server == NULL) {
      herror("pixelflut hostname not found");
      return -1;
   }

   struct sockaddr_in serv_addr;
   memset(&serv_addr, 0, sizeof(serv_addr));
   serv_addr.sin_family = AF_INET;

   memcpy((char *) &serv_addr.sin_addr.s_addr, (char *) server->h_addr, server->h_length);
   serv_addr.sin_port = htons(port);

   if (connect(fd, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
      perror("couldn't connect to pixelflut");
      return -1;
   }

   return fd;
}

/*
 * read_until reads from fd until sep is reached, then replaces sep with a null
 * terminator. Returns true if sep was reached.
 */
bool read_until(int fd, char sep, char *buf, int buf_len)
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
   printf("DEBUG wrote size request!\n");

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
   printf("DEBUG read size response!\n");

   ret->w = w;
   ret->h = h;
   return true;
}

bool pf_set(int fd, int x, int y, unsigned char r, unsigned char g, unsigned char b)
{
   return dprintf(fd, "PX %d %d %02x%02x%02x\n", x, y, r, g, b) > 0;
}
