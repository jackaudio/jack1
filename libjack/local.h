#ifndef __jack_libjack_local_h__
#define __jack_libjack_local_h__

/* Client data structure, in the client's address space. */
struct _jack_client {

    jack_control_t        *engine;
    jack_client_control_t *control;
    jack_shm_info_t        engine_shm;
    jack_shm_info_t        control_shm;

    struct pollfd*  pollfd;
    int             pollmax;
    int             graph_next_fd;
    int             request_fd;
    int             upstream_is_jackd;

    /* these two are copied from the engine when the 
     * client is created.
    */

    jack_port_type_id_t n_port_types;
    jack_shm_info_t*    port_segment;

    JSList *ports;

    pthread_t thread;
    char fifo_prefix[PATH_MAX+1];
    void (*on_shutdown)(void *arg);
    void *on_shutdown_arg;
    char thread_ok : 1;
    char first_active : 1;
    pthread_t thread_id;
    
#ifdef JACK_USE_MACH_THREADS
    /* specific ressources for server/client real-time thread communication */
    mach_port_t clienttask, bp, serverport, replyport;
    trivial_message  message;
    pthread_t process_thread;
	char rt_thread_ok : 1;
#endif

};

extern int jack_client_deliver_request (const jack_client_t *client, jack_request_t *req);
extern jack_port_t *jack_port_new (const jack_client_t *client, jack_port_id_t port_id, jack_control_t *control);

extern void *jack_zero_filled_buffer;

#endif /* __jack_libjack_local_h__ */
