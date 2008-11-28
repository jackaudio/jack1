
// After looking at how pulseaudio is doing things,
// i have came to the conclusion, that forgetting
// about statistics is a bad idea ;)
//
// i am loosely basing this on pulsecore/time-smoother.c

#include "time_smoother.h"
#include <stdlib.h>
#include <assert.h>

time_smoother *time_smoother_new( int history_size )
{
	time_smoother *retval = malloc( sizeof( time_smoother ) );
	if( !retval )
		return NULL;

	retval->x = malloc( sizeof(jack_nframes_t) * history_size );

	if( !retval->x ) { 
		free( retval );
		return NULL;
	}

	retval->y = malloc( sizeof(jack_nframes_t) * history_size );
	if( !retval->y ) {
		free( retval->x );
		free( retval );
		return NULL;
	}

	retval->history_size = history_size;
	retval->num_valid = 0;
	return retval;
}

void time_smoother_free( time_smoother *ts )
{
	free( ts->x );
	free( ts->y );
	free( ts );
}

// put a time measurement into the smoother.
// assume monotonically increasing x.

void time_smoother_put ( time_smoother *ts, jack_nframes_t x, jack_nframes_t y )
{
	int i;
	int oldest_index;
	jack_nframes_t oldest_diff;

	if( ts->num_valid < ts->history_size ) {
		ts->x[ts->num_valid] = x;
		ts->y[ts->num_valid] = y;
		ts->num_valid += 1;
		return;
	}

	// Its full. find oldest value and replace.
	oldest_index = -1;
	oldest_diff  = 0;
	for( i=0; i<ts->history_size; i++ ) {
		if( (x - ts->x[i]) > oldest_diff ) {
			oldest_diff  = (x - ts->x[i]);
			oldest_index = i;
		}
	}
	assert( oldest_index != -1 );
	
	ts->x[oldest_index] = x;
	ts->y[oldest_index] = y;
}

// return a and b for the linear regression line,
// such that y=a+bx;
void time_smoother_get_linear_params( time_smoother *ts,
				      jack_nframes_t now_x,
				      jack_nframes_t now_y,
				      jack_nframes_t history,
				      double *a, double *b    )
{
	int i;
	jack_nframes_t sum_x = 0;
	jack_nframes_t sum_y = 0;
	int num_values = 0;
	double mean_x, mean_y;
	double sxx = 0.0;
	double sxy = 0.0;
	double val_a, val_b;

	for( i=0; i<ts->num_valid; i++ ) {
		if( (now_x - ts->x[i]) < history ) {
			sum_x += (now_x - ts->x[i]);
			sum_y += (now_y - ts->y[i]);
			num_values += 1;
		}
	}

	// Give some valid values if we dont have
	// enough data;
	if( num_values < 10 ) {
		if( a ) *a = 0.0;
		if( b ) *b = 1.0;

		return;
	}

	mean_x = (double) sum_x / (double) num_values;
	mean_y = (double) sum_y / (double) num_values;
	//printf( "mean: %f %f\n", (float) mean_x, (float) mean_y );

	for( i=0; i<ts->num_valid; i++ ) {
		if( (now_x - ts->x[i]) < history ) {
			sxx += ((double)(now_x-ts->x[i])-mean_x) * ((double)(now_x-ts->x[i])-mean_x);
			sxy += ((double)(now_x-ts->x[i])-mean_x) * ((double)(now_y-ts->y[i])-mean_y);
		}
	}
	
	val_b = sxy/sxx;
	val_a = mean_y - val_b*mean_x;
	
	if( a )
		*a = val_a;

	if( b )
		*b = val_b;
}
