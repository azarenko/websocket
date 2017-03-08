/*
 * FIFO buffer
 */
#include <pthread.h>
#include <libwebsockets.h>

#define BUFSIZE 1024

struct wsinf {
    struct lws *wsi_in;
    char *content;
    int str_size_in;
};

typedef struct wsinf wsinf;

wsinf *fifobuf[BUFSIZE];
wsinf **pIn, **pOut, **pEnd;
int full;
pthread_mutex_t lock;

int isfifofull();

// init
void fifo_init();

// add char 'c' to buffer
int fifo_put(wsinf *val);

// get a char from circular buffer
int fifo_get(wsinf **val);

void fifo_free();
