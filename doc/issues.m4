<html>

m4_include(`header.html')

<h2>Issues to consider when porting programs to JACK</h2>

<h4>Sample bit width assumptions</h4>

A lot existing Linux audio software tends to assume that audio samples
are 8 or 16 bits wide, and uses <code>short</code> to store them. This
does not work with JACK, where all sample data, regardless of the
original data format in which it was obtained (e.g. from disk), is
stored as a floating point value normalized to the range -1.0 to +1.0.

<h4>Channel interleaving assumptions</h4>

Almost all existing Linux audio software assumes that when delivering
a sample stream with more than one channel, the samples should be
interleaved. This does not work with JACK, where all sample streams
are mono.

<h4>Block-on-write or block-on-read assumptions</h4>

Quite a lot of existing Linux audio software tends to be structured
around the blocking behaviour of a call to write(2) or read(2) when
the file descriptor concerned refers to the audio interface. They
often have this structure:

<verbatim>

	// Playback

	while (1) {
	     get_sample_date_from_somewhere (buf);
	     write (audiofd, buf, bufsize);
        }

	// Capture

	while (1) {
	    read (audiofd, buf, bufsize);
	    put_sample_data_somewhere (buf);
        }

</verbatim>

These structures don't work with JACK, which is entirely callback
driven and moves audio data by copying it to and from memory
locations, not files. Instead, its necessary to define a
<code>process()</code> callback which does this:

<verbatim>

	 // playback

	 int 
	 process (nframes_t nframes)
	 {	
		get_nframes_of_data_from_somewhere_without_blocking (buf);
		sample_t *addr = jack_port_get_buffer (playback_port);
		memcpy (addr, buf, nframes * sizeof (sample_t));
         }

	 // capture

	 int 
	 process (nframes_t nframes)
	 {	
		sample_t *addr = jack_port_get_buffer (capture_port);
		memcpy (buf, addr, nframes * sizeof (sample_t));
		put_nframes_of_data_somewhere_without_blocking (buf);
         }

</verbatim>

The code in the <code>process()</code> function should not under
(almost) any circumstances block: that is, it may not read/write data
from/to a file, it may not call malloc(), it may not use
pthread_mutex_lock(), and it should generally avoid system calls. the
<code>process()</code> callback will be executed when the JACK server
decides it should be, and it cannot be used to time other parts of the
program.

m4_include(`trailer.html')

