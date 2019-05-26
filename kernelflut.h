#pragma once

#define ERR_USAGE 2
#define ERR_BADPORT 3
#define ERR_ALLOC 4
#define ERR_IRRECOVERABLE 5

#define ERR_EVDI_ADD 10
#define ERR_EVDI_FIND 11
#define ERR_EVDI_OPEN 12

#define ERR_PF_CONNECT 20
#define ERR_PF_SIZE 21
#define ERR_PF_SEND 22

void interrupt(int _, siginfo_t *__, void *___);
void update_ready_handler(int _, void *__);
int get_first_device();
int setup(char *hostname, int port);
int main(int argc, char *argv[]);
