#ifndef _RINGBUFFER_H
#define _RINGBUFFER_H

#include <sys/types.h>

/** @file ringbuffer.h
 *
 * A set of library functions to make lock-free ringbuffers available
 * to JACK clients.  The `capture_client.c' (in the example_clients
 * directory) is a fully functioning user of this API.
 *
 * The key attribute of a ringbuffer is that it can be safely accessed
 * by two threads simultaneously -- one reading from the buffer and
 * the other writing to it -- without using any synchronization or
 * mutual exclusion primitives.  For this to work correctly, there can
 * only be a single reader and a single writer thread.  Their
 * identities cannot be interchanged.
 * 
 */

typedef struct  
{
  char *buf;
  size_t len;
} 
jack_ringbuffer_data_t ;

typedef struct
{
  char *buf;
  volatile size_t write_ptr;
  volatile size_t read_ptr;
  size_t size;
  size_t size_mask;
  int mlocked;
} 
jack_ringbuffer_t ;

/**
 * jack_ringbuffer_create
 *
 * Allocates a ringbuffer data structure of a specified size. The
 * caller must arrange for a call to jack_ringbuffer_free to release
 * the memory associated with the ringbuffer.
 *
 * @param sz the ringbuffer size in bytes.
 *
 * @return a pointer to a new jack_ringbuffer_t, if successful; NULL
 * otherwise.
 */

jack_ringbuffer_t *jack_ringbuffer_create(size_t sz);
void jack_ringbuffer_free(jack_ringbuffer_t *rb);

size_t jack_ringbuffer_write_space(jack_ringbuffer_t *rb);
size_t jack_ringbuffer_read_space(jack_ringbuffer_t *rb);

/**
 * jack_ringbuffer_read
 *
 * read a specified number of bytes from the ringbuffer.
 *
 * @param rb a pointer to the ringbuffer structure
 * @param dest a pointer to a buffer where the data read from the ringbuffer
 *               will be placed
 * @param cnt the number of bytes to be read
 *
 * @return the number of bytes read, which may range from 0 to cnt
 */
size_t jack_ringbuffer_read(jack_ringbuffer_t *rb, char *dest, size_t cnt);

/**
 * jack_ringbuffer_write
 *
 * write a specified number of bytes from the ringbuffer.
 *
 * @param rb a pointer to the ringbuffer structure
 * @param src a pointer to a buffer where the data written to the ringbuffer
 *               will be read from
 * @param cnt the number of bytes to be write
 *
 * @return the number of bytes write, which may range from 0 to cnt
 */
size_t jack_ringbuffer_write(jack_ringbuffer_t *rb, char *src, size_t cnt);

/**
 * jack_ringbuffer_get_read_vector
 *
 * fill a data structure with a description of the current readable data
 * held in the ringbuffer. the description is returned in a 2 element
 * array of jack_ringbuffer_data_t. two elements are necessary
 * because the data to be read may be split across the end of the ringbuffer.
 *
 * the first element will always contain
 * a valid len field, which may be zero or greater. if the len field
 * is non-zero, then data can be read in a contiguous fashion using the address given
 * in the corresponding buf field.
 *
 * if the second element has a non-zero len field, then a second contiguous 
 * stretch of data can be read from the address given in the corresponding buf
 * field.
 *
 * @param rb a pointer to the ringbuffer structure
 * @param vec a pointer to a 2 element array of jack_ringbuffer_data_t
 *
 */
void jack_ringbuffer_get_read_vector(jack_ringbuffer_t *rb, jack_ringbuffer_data_t *vec);

/**
 * jack_ringbuffer_get_write_vector
 *
 * fill a data structure with a description of the current writable space
 * in the ringbuffer. the description is returned in a 2 element
 * array of jack_ringbuffer_data_t. two elements are necessary
 * because the space available to write in may be split across the end 
 * of the ringbuffer.
 *
 * the first element will always contain
 * a valid len field, which may be zero or greater. if the len field
 * is non-zero, then data can be written in a contiguous fashion using the address given
 * in the corresponding buf field.
 *
 * if the second element has a non-zero len field, then a second contiguous 
 * stretch of data can be written to the address given in the corresponding buf
 * field.
 *
 * @param rb a pointer to the ringbuffer structure
 * @param vec a pointer to a 2 element array of jack_ringbuffer_data_t
 *
 */
void jack_ringbuffer_get_write_vector(jack_ringbuffer_t *rb, jack_ringbuffer_data_t *vec);

int jack_ringbuffer_mlock(jack_ringbuffer_t *rb);

void jack_ringbuffer_reset(jack_ringbuffer_t *rb);

void jack_ringbuffer_write_advance(jack_ringbuffer_t *rb, size_t cnt);
void jack_ringbuffer_read_advance(jack_ringbuffer_t *rb, size_t cnt);


#endif
