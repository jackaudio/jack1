
#include "jack/jack.h" 

typedef struct time_smoother {
	int history_size;
	jack_nframes_t *x;
	jack_nframes_t *y;
	int num_valid;
} time_smoother;


time_smoother *time_smoother_new( int history_size );
void time_smoother_free( time_smoother *ts );
void time_smoother_put ( time_smoother *ts, jack_nframes_t x, jack_nframes_t y );
void time_smoother_get_linear_params( time_smoother *ts, jack_nframes_t now_x, jack_nframes_t now_y, 
	jack_nframes_t history, double *a, double *b );
