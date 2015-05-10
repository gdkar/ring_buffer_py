#ifndef _BSD_SOURCE
# define _BSD_SOURCE 1
#endif

#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <stddef.h>
#include <stdint.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>

#include "buffer.h"
#define MIN(a,b) ((a)<(b)?(a):(b))
#define MAX(a,b) ((a)>(b)?(a):(b))
#define ABS(x)   ((x)<0?-(x):(x))

/* Construct a ring_buffer by passing a reference to the zero-filled initialized *buffer pointer
 * @buffer: the zero-filled ring_buffer pointer
 * @order: size of the buffer in log2, which has to be at least 12 on linux
 */
void
ring_buffer_create(struct ring_buffer *buffer, llong order){
    int fd;
    void *address;
    int status;
    fd = open("/dev/zero", O_RDWR);
    if (fd < 0) {
      fprintf(stderr,"failed to open /dev/zero (returned fd %d)\n",fd);
      return;
    }
    buffer->page_size = sysconf(_SC_PAGESIZE);
    buffer->count_bytes = ((1LL << order) + (buffer->page_size-1))&(~(buffer->page_size-1));
    buffer->count_mask  = buffer->count_bytes-1;
    buffer->write_offset_bytes = 0;
    buffer->cached_write_offset= 0;
    buffer->read_offset_bytes = 0;
    buffer->cached_read_offset= 0;
    buffer->end_offset_bytes = 0;
    buffer->address = mmap (NULL, buffer->count_bytes << 1, PROT_NONE,
                            MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
    if (buffer->address == MAP_FAILED){
      fprintf(stderr,"failed to map /dev/zero for ringbuffer\n");
      return;
    } 
    /* Notice how this mmap call and the next mmap call map two physical memory pieces 
     * to the same file descriptor @fd with the same offset 0. This is why when the
     * write pointer advances beyond the buffer->count_bytes, the next write call will
     * start automatically from the beginning of the @fd.
     */
    address = mmap (buffer->address, buffer->count_bytes, PROT_EXEC | PROT_READ | PROT_WRITE,
                    MAP_FIXED | MAP_SHARED, fd, 0);

    if (address != buffer->address){
      fprintf(stderr,"failed to map first block for ringbuffer\n");
      munmap(buffer->address,buffer->count_bytes<<1);
      buffer->address=NULL;
      buffer->count_bytes = 0;
      return;
    }
    address = mmap (buffer->address + buffer->count_bytes,
                    buffer->count_bytes, PROT_EXEC | PROT_READ | PROT_WRITE,
                    MAP_FIXED | MAP_SHARED, fd, 0);
    if (address != buffer->address+buffer->count_bytes){
      fprintf(stderr,"failed to map second block for ringbuffer\n");
      munmap(buffer->address,buffer->count_bytes<<1);
      buffer->address=NULL;
      buffer->count_bytes = 0;
      return;
    }
    status = close (fd); // already has a mmap-ed ring_buffer pointer *address, do not need fd anymore
    if (status){
      fprintf(stderr,"failed to close copy of /dev/zero\n");
      munmap(buffer->address,buffer->count_bytes<<1);
      buffer->address=NULL;
      buffer->count_bytes = 0;
      return;
    }
}

void
ring_buffer_free (struct ring_buffer *buffer){
    int status;
    if (buffer == NULL || buffer->address == NULL)return;
    status = munmap (buffer->address, buffer->count_bytes << 1);
    if (status){
      fprintf(stderr,"failed to unmap ringbuffer\n");
    }
    buffer->address     = 0;
    buffer->count_bytes = 0;
}

void *
ring_buffer_write_address (struct ring_buffer *buffer){
  return buffer->address + ((buffer->count_mask)&(buffer->write_offset_bytes));
}

void
ring_buffer_write_advance (struct ring_buffer *buffer, llong count_bytes){
  count_bytes = MAX(count_bytes,0);
  llong post = buffer->write_offset_bytes+count_bytes;
  post = MIN(post,buffer->read_offset_bytes+buffer->count_bytes);
  buffer->write_offset_bytes = post;
}
void *
ring_buffer_read_address (struct ring_buffer *buffer)
{return buffer->address + ((buffer->count_mask)&(buffer->read_offset_bytes));}
void
ring_buffer_read_advance (struct ring_buffer *buffer, llong count_bytes){
    count_bytes = MAX(count_bytes,0);
    llong post = buffer->read_offset_bytes+count_bytes;
    post = MIN(post,buffer->write_offset_bytes);
    buffer->read_offset_bytes = post;
}

/* Return how many bytes available for read
 */
llong
ring_buffer_count_bytes (struct ring_buffer *buffer){
  return (buffer->cached_write_offset=atomic_load(&buffer->write_offset_bytes))-
      (buffer->cached_read_offset=atomic_load(&buffer->read_offset_bytes));
}
/* Return how many bytes available for write
 */
llong
ring_buffer_count_free_bytes (struct ring_buffer *buffer){
  return buffer->count_bytes - ring_buffer_count_bytes(buffer);  
}

void
ring_buffer_clear (struct ring_buffer *buffer){
    buffer->cached_write_offset=0;
    atomic_store(&buffer->write_offset_bytes,0);
    buffer->cached_read_offset=0;
    atomic_store(&buffer->read_offset_bytes,0);
}

/* Write @data of size @count_bytes into the buffer if there is enough space. Otherwise,
 * terminate_and_generate_core_dump()
 */
llong
ring_buffer_write (struct ring_buffer *buffer, char *data, llong count_bytes){
    llong write_offset = atomic_load(&buffer->write_offset_bytes);
    llong read_offset  = buffer->cached_read_offset;
    if(write_offset + count_bytes > buffer->count_bytes + read_offset
    && write_offset + count_bytes > buffer->count_bytes + (read_offset = buffer->cached_read_offset
                                                                       = atomic_load(&buffer->read_offset_bytes)))
      count_bytes = read_offset + buffer->count_bytes - (write_offset);
    memmove ((void *)(buffer->address + (buffer->count_mask&write_offset)), (void *)data, count_bytes);
    atomic_fetch_add(&buffer->write_offset_bytes,count_bytes);
    return count_bytes;
}
/* Read @count_bytes into the @data.
 */
llong
ring_buffer_read (struct ring_buffer *buffer, char *data, llong count_bytes){
    llong write_offset = buffer->cached_write_offset;
    llong read_offset  = atomic_load(&buffer->read_offset_bytes);
    if(read_offset + count_bytes > write_offset 
    && read_offset + count_bytes > (write_offset = buffer->cached_write_offset
                                   = atomic_load(&buffer->write_offset_bytes)))
      count_bytes = write_offset - read_offset;
    memmove(data, buffer->address + (buffer->count_mask&read_offset), count_bytes);
    atomic_fetch_add(&buffer->read_offset_bytes,count_bytes);
    return count_bytes;
}

/* Assign the value of current write_offset_bytes to end_offset_bytes to mark an eof
 */
void
ring_buffer_write_close (struct ring_buffer *buffer)
{buffer->end_offset_bytes = buffer->write_offset_bytes;}

bool ring_buffer_eof (struct ring_buffer *buffer)
{return buffer->write_offset_bytes == buffer->read_offset_bytes;}
/* Copy the buffer data into @data without advancing the read pointer
 */
llong
ring_buffer_peek (struct ring_buffer *buffer, char *data, llong count_bytes){

llong write_offset = buffer->cached_write_offset;
    llong read_offset  = atomic_load(&buffer->read_offset_bytes);
    if(count_bytes > write_offset - read_offset 
    && count_bytes > (write_offset = buffer->cached_write_offset
                                   = atomic_load(&buffer->write_offset_bytes)) - read_offset)
      count_bytes = write_offset - read_offset;
    memmove(data, buffer->address + (buffer->count_mask&read_offset), count_bytes);
    return count_bytes;
}
