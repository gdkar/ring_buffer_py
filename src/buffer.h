#ifndef BUFFER_H
#define BUFFER_H

#include <stdlib.h>
#include <stddef.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <stdatomic.h>
#include <unistd.h>

#define terminate_and_generate_core_dump() abort ()
#ifndef llong
typedef long long llong;
#endif
struct ring_buffer{
    void *address;
    llong         count_bytes; // buffer size in bytes
    llong         count_mask;
    atomic_llong  write_offset_bytes __attribute__((aligned(64)));
    llong         cached_read_offset;
    atomic_llong  read_offset_bytes  __attribute__((aligned(64)));
    llong         cached_write_offset; 
    off_t         end_offset_bytes   __attribute__((aligned(64))); 
    // when an IWriteEndpoint calls close(), the end_offset_bytes is assigned
    // the value of write_offset_bytes
    long page_size; // unit of memory in bytes which is used by mmap to allocate memory
};
void ring_buffer_create (struct ring_buffer *buffer, llong order);
void ring_buffer_free (struct ring_buffer *buffer);
void * ring_buffer_write_address (struct ring_buffer *buffer);
void ring_buffer_write_advance (struct ring_buffer *buffer, llong count_bytes);
void * ring_buffer_read_address (struct ring_buffer *buffer);
void ring_buffer_read_advance (struct ring_buffer *buffer, llong count_bytes);
llong ring_buffer_count_bytes (struct ring_buffer *buffer);
llong ring_buffer_count_free_bytes (struct ring_buffer *buffer);
void ring_buffer_clear (struct ring_buffer *buffer);

/* For libbrowzoo.python.interface.stream.IReadEndpoint and IWriteEndpoint */
llong ring_buffer_write (struct ring_buffer *buffer, char *data, llong count_bytes);
llong ring_buffer_read (struct ring_buffer *buffer, char *data, llong  count_bytes);
void ring_buffer_write_close (struct ring_buffer *buffer);
bool ring_buffer_eof (struct ring_buffer *buffer);
llong ring_buffer_peek (struct ring_buffer *buffer, char *data, llong count_bytes);
#endif
