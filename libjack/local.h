#ifndef __jack_libjack_local_h__
#define __jack_libjack_local_h__

struct _jack_client {

    jack_control_t        *engine;
    jack_client_control_t *control;
    struct pollfd *pollfd;
    int pollmax;
    int graph_next_fd;
    int request_fd;
    JSList *port_segments;
    JSList *ports;
    pthread_t thread;
    char fifo_prefix[PATH_MAX+1];
    void (*on_shutdown)(void *arg);
    void *on_shutdown_arg;
    char thread_ok : 1;
    char first_active : 1;
    float cpu_mhz;
    pthread_t thread_id;

};

extern int jack_client_deliver_request (const jack_client_t *client, jack_request_t *req);
extern jack_port_t *jack_port_new (const jack_client_t *client, jack_port_id_t port_id, jack_control_t *control);

extern void*                 jack_zero_filled_buffer;

#endif /* __jack_libjack_local_h__ */
