#include "fifo.h"

int isfifofull()
{
    return (pIn == pOut  &&  full);
}

// init
void fifo_init()
{
    pIn = pOut = fifobuf;       // init to any slot in buffer
    pEnd = &fifobuf[BUFSIZE];   // past last valid slot in buffer
    full = 0;               // buffer is empty
}

void fifo_free()
{
    pthread_mutex_destroy(&lock);
}

// add char 'c' to buffer
int fifo_put(wsinf *val)
{
    pthread_mutex_lock(&lock);
    
    if (pIn == pOut  &&  full)
    {
        pthread_mutex_unlock(&lock);
        return 1;           // buffer overrun
    }

    *pIn++ = val;             // insert c into buffer
    if (pIn >= pEnd)        // end of circular buffer?
        pIn = fifobuf;          // wrap around

    if (pIn == pOut)        // did we run into the output ptr?
        full = 1;           // can't add any more data into buffer
    
    pthread_mutex_unlock(&lock);
    return 0;               // all OK
}

// get a char from circular buffer
int fifo_get(wsinf **val)
{
    pthread_mutex_lock(&lock);
    
    if (pIn == pOut  &&  !full)
    {
        pthread_mutex_unlock(&lock);
        return 1;           // buffer empty  FAIL
    }

    *val = *pOut++;              
    if (pOut >= pEnd)       // end of circular buffer?
        pOut = fifobuf;         // wrap around

    full = 0;               // there is at least 1 slot    
    pthread_mutex_unlock(&lock);
    
    return 0;               // *pc has the data to be returned
}
