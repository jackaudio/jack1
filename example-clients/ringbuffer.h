#ifndef _RINGBUFFER_H
#define _RINGBUFFER_H

#include <sys/types.h>

typedef struct  
{
  char *buf;
  size_t len;
} 
ringbuffer_data_t ;

typedef struct
{
  char *buf;
  volatile size_t write_ptr;
  volatile size_t read_ptr;
  size_t size;
  size_t size_mask;
  int mlocked;
} 
ringbuffer_t ;

ringbuffer_t *ringbuffer_create(int sz);
void ringbuffer_free(ringbuffer_t *rb);

int ringbuffer_mlock(ringbuffer_t *rb);
void ringbuffer_reset(ringbuffer_t *rb);

void ringbuffer_write_advance(ringbuffer_t *rb, size_t cnt);
void ringbuffer_read_advance(ringbuffer_t *rb, size_t cnt);

size_t ringbuffer_write_space(ringbuffer_t *rb);
size_t ringbuffer_read_space(ringbuffer_t *rb);

size_t ringbuffer_read(ringbuffer_t *rb, char *dest, size_t cnt);
size_t ringbuffer_write(ringbuffer_t *rb, char *src, size_t cnt);

void ringbuffer_get_read_vector(ringbuffer_t *rb, ringbuffer_data_t *vec);
void ringbuffer_get_write_vector(ringbuffer_t *rb, ringbuffer_data_t *vec);

#endif
