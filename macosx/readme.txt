
Darwin/MacOSX port for Jack : architecture changes in the implementation
========================================================================

Shared memory
=============

The system V shared memory is not very reliable on Darwin/MacOSX. The POSIX shared memory API is used instead.


Jack server audio cycle
========================

On Linux, the jack server audio cycle (jack_run_cycle function) is called in a real-time SCHED_FIFO thread. 
On Darwin/MacOSX, the jack_run_cycle is directly called inside the CoreAudio audio callback.


External client activation
===========================

Jack Linux implementation use system fifo/pipe to trigger the clients real-time callback from the server : 
the first client of an external subgraph is triggered, does it's job and wakes up the next one in the list,
and so on until the last client that wakes up the Jack server. This avoid uneeded context switches between 
the server and clients and thus is more efficient.

This Linux implementation works also on Darwin/MacOSX but is not very efficient : audio gliches occur quite 
frequently.

A more efficient system for external client activation has been developed. It use low-level mach messages 
system to implement fast IPC between the Jack server and the running clients. The Darwin/MacOSX has a very 
efficient Remote Procedure Call (RPC) implementation that can be used : the Jack server activate each external
client in turn in a loop.

On the client side, each client uses an additionnal thread only used for the real-time process callback,
that will be triggered by the Jack server using this fast RPC mechanism.


Real-time threads
==================

The Darwin/MacOSX system use a special class of scheduling for real-time threads : 

Since the server audio cycle is called directly from the CoreAudio callback, there is nothing special 
to do on the server side. On the client side, the thread used to call the "process" callback from the 
server is made real-time, using the mach thread API.


Compilation and installation
=============================

- In the jack/jack folder, you'll have to produce the version.h manually from the version.h.in file ):
Edit the version.h.in, replace the JACK_PROTOCOL_VERSION value with the one found in configure.in and 
save as a new version.h file. You should get something like "#define jack_protocol_version 6" in the file.

Several packages need to be installed before compiling Jack :

- Fink dlcompat (fink.sourceforge.net) : this package define the dlopen, dlsym... API used in Jack to 
load drivers and internal clients. The package has to be installed before compiling Jack.

- fakepoll is a implementation of the poll function using select. The Fink version of poll does not
 work correctly, thus the public domain "fakepoll" code has been used instead. It is directly included
  in the Jack Darwin/MacOSX port.

- PortAudio (www.portaudio.com) : PortAudio is a free, cross platform, open-source, audio I/O library. 
The Jack CoreAudio driver actually is implemented using PortAudio. The PortAudio source code for MacOSX 
has to be installed and compiled to produce a framework called "PortAudio.framework" that will be used 
in the Jack link phase.


Several targets are avaiblable in the Project Builder project :

- jackd :  build the Jack server ("jackd" executable)
- jack framework : build the "Jack.framework" library.
- driver :  build the PortAudio driver as a "jack_portaudio.so" shared library.
- jack_metro :  build the "jack_metro" executable.
- jack_lsp :  build the "jack_lsp" executable.
- jack_connect :  build the "jack_connect" executable.
- jack_disconnect :  build the "jack_disconnect" executable.

Server, driver and library installation :
-----------------------------------------

First copy the Jack.framework in /Library/Framework. Then as root : 

cp  	jack_portaudio 	/usr/local/lib
cp 	jackd 		/usr/local/bin

Launching Jack server :
-----------------------

By default buffer size is 128 frames and sample rate is 44100.

jackd  -v -R -d coreaudio 

To setup a 32 frames buffer and a 4800 Hz sample rate : 

jackd  -v -R -d coreaudio -p 32 -r 48000  

Performances
=============

The Darwin/MacOSX implementation is quite efficient : on a G4/867 Mhz, the Jack server can run with 
a 32 frames buffer size without noticable problems.


Known problems or unimplemented features
=========================================

- thread cancellation : the pthread API pthread_cancel is not completely available on Darwin/MacOSX. 
Thread cannot be cancelled in the general case : they must use explicit cancelation points like 
"pthread_testcancel" (see the "jack_client_thread" function)

- xruns detection and report : not implemented.


Possible improvements
======================

- The audio driver is built on top of PortAudio. It may be more efficient to directly use the 
CoreAudio API in order to avoid additionnal buffer copy and interleaving/desinterleaving operations.

- The project uses Project Builder. It would be helpful to work on the autoconf, automake ... tools 
as in the Linux version. Is this case, the macosx/config.h file would have to be removed and generated 
by configure, and jack/version.h will be generated automatically from jack/version.h.in

- Better separation of Linux and Darwin/MacOSX only code.

The Jack port for Darwin/MacOSX version has be done by :

Grame Research Laboratory
9, rue du Garet 69001 Lyon - France
Mail : letz@grame.fr

