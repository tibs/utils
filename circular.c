/*
 * Circular buffer support
 *
 * Author: Tony J. Ibbs <tibs@tonyibbs.co.uk>
 * Released to the public domain (please be nice to it)
 * $Revision: 1.10 $
 * $Date: 2004/03/10 16:33:20 $
 */

#include <stdlib.h>
#include <stdlib.h>
#include <stdio.h>

#include <errno.h>
#include <string.h>

#include <sys/mman.h>   // Memory mapping

#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <time.h>       // Sleeping

#define BUFFER_SIZE  11  // one more item than we actually need

// A circular buffer, usable as a queue
// We "waste" one buffer item so that we don't have to maintain a count
// of items in the buffer
struct circular_buffer
{
  int front;
  int back;
  int buffer[BUFFER_SIZE];
};
typedef struct circular_buffer *circular_buffer_p;
#define SIZEOF_CIRCULAR_BUFFER sizeof(struct circular_buffer)

/*
 * Initialise our buffer
 */
void init_buffer(circular_buffer_p  buf)
{
  buf->front = 1;
  buf->back = 0;
}

/*
 * Add a new item to the queue.
 *
 * Returns 0 if it successfully adds the item, 1 if the buffer was full.
 */
int add_to_buffer(circular_buffer_p  buf,
                  int                item)
{
  if (buffer_full(buf))
    return 1;

  buf->back = (buf->back + 1) % BUFFER_SIZE;
  buf->buffer[buf->back] = item;

  return 0;
}

/*
 * Remove (pop) the oldest item from the queue
 *
 * Returns 0 if it successfully retrieves the item, 1 if the buffer was empty.
 */
int pop_from_buffer(circular_buffer_p  buf,
                    int               *item)
{
  if (buffer_empty(buf))
    return 1;

  *item = buf->buffer[buf->front];

  // Not needed for any reason except my printing out
  buf->buffer[buf->front] = -1;

  buf->front = (buf->front + 1) % BUFFER_SIZE;

  return 0;
}

/*
 * Return true if the buffer is empty, false otherwise.
 */
int buffer_empty(circular_buffer_p  buf)
{
  return (buf->front == (buf->back + 1) % BUFFER_SIZE);
}

/*
 * Return true if the buffer is full, false otherwise
 */
int buffer_full(circular_buffer_p  buf)
{
  return ((buf->back + 2) % BUFFER_SIZE == buf->front);
}

/*
 * Print out the contents of our buffer
 */
void print_buffer(circular_buffer_p  buf)
{
  int ii;
  printf("Buffer [%02d..%02d] = ",buf->front,buf->back);
  for (ii = 0; ii < BUFFER_SIZE; ii++)
    printf("%2d ",buf->buffer[ii]);
  printf("\n");
}



/*
 * Test code...
 */

// Ten milliseconds, in nanoseconds
#define TEN_MS  10000000

void print_circular_buffer(circular_buffer_p  circular)
{
  int ii;
  printf("Buffer is ");
  for (ii = 0; ii < BUFFER_SIZE; ii++)
  {
    printf("%s",(circular->front == ii ? "[":" "));
    printf("%08x",circular->buffer[ii]);
    printf("%s ",(circular->back == ii ? "]":" "));
  }
  printf("\n");
}

void parent_add_to_buffer(circular_buffer_p  buf,
                          int                item)
{
  struct timespec   time = {0,5*TEN_MS};  // The parent can wait longer
  int  full;
  // Keep trying until we actually get to put the item into the buffer
  for (;;)
  {
    full = add_to_buffer(buf,item);
    if (full)
    {
      int err;
      printf("Parent: waiting\n");
      err = nanosleep(&time,NULL);
      if (err == -1 && errno == EINVAL)
        fprintf(stderr,"Parent: bad value for wait time\n");
    }
    else
      break;
  }
}

void child_pop_from_buffer(circular_buffer_p   buf,
                           int                *item)
{
  struct timespec   time = {0,TEN_MS};  // Aim for about 10ms
  int  empty;
  // Keep trying until we actually get to take an item from the buffer
  for (;;)
  {
    empty = pop_from_buffer(buf,item);
    if (empty)
    {
      int err;
      printf("Child: waiting\n");
      err = nanosleep(&time,NULL);
      if (err == -1 && errno == EINVAL)
        fprintf(stderr,"Child: bad value for wait time\n");
    }
    else
      break;
  }
}


int main(int argc, char **argv)
{
  int    err;
  int    ii;
  circular_buffer_p  bufptr;
  pid_t  pid, result;

  // Rather than map a file, we'll map anonymous memory
  // BSD supports the MAP_ANON flag as is,
  // Linux (bless it) deprecates MAP_ANON and would prefer us to use
  // the more verbose MAP_ANONYMOUS (but MAP_ANON is still around, so
  // we'll stick with that while we can)
  bufptr = mmap(NULL, SIZEOF_CIRCULAR_BUFFER,PROT_READ | PROT_WRITE,
                MAP_SHARED | MAP_ANON, -1, 0);

  if (bufptr == (void *)-1)
  {
    perror("Mapping shared memory");
    return 1;
  }

  init_buffer(bufptr);

  pid = fork();
  if (pid == -1)
  {
    perror("Error forking");
    return 1;
  }
  else if (pid == 0)
  {
    // Aha - we're the child
    for (;;)
    {
      int val;
      child_pop_from_buffer(bufptr,&val);
      printf("Child: Pop %2d\n",val);
      print_circular_buffer(bufptr);
      if (val == -1)
        break;
    }
    return 0;
  }

  // And otherwise we're the parent

  for (ii = 0; ii < 50; ii++)
  {
    printf("Parent: Add %2d\n",ii);
    parent_add_to_buffer(bufptr,ii);
    print_circular_buffer(bufptr);
  }
  // We have to tell the child to stop, somehow
  printf("Parent: Add stopvalue -1\n");
  parent_add_to_buffer(bufptr,-1);
  print_circular_buffer(bufptr);

  printf("Waiting for child to exit\n");
  result = wait(&err);
  if (result == -1)
  {
    perror("Waiting for child to exit");
    return 1;
  }
  if (WIFEXITED(err))
  {
    printf("Child exited normally\n");
  }

  return 0;
}
