/** @file net_source.c
 *
 * @brief This is an in process version of the netjack client
 */
#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>

#include <netinet/in.h>
#include <netdb.h>

#include <alloca.h>

#include <jack/jack.h>

#include "net_driver.h"
#include "netjack_packet.h"
#include <samplerate.h>

JSList *capture_ports = NULL;
JSList *capture_srcs = NULL;
int capture_channels = 2;
JSList *playback_ports = NULL;
JSList *playback_srcs = NULL;
int playback_channels = 2;

int latency = 1;
jack_nframes_t factor = 1;
int bitdepth = 0;
int reply_port = 0;
jack_client_t *client;

int outsockfd;
int insockfd;
struct sockaddr destaddr;
struct sockaddr bindaddr;

int recv_channels;
int recv_smaple_format;

int sync_state;
jack_transport_state_t last_transport_state;

int framecnt = 0;
int cont_miss = 0;

SRC_STATE *src_state;

/**
 * This Function allocates all the I/O Ports which are added the lists.
 * XXX: jack-midi is underway... so dont forget it.
 */
void
alloc_ports (int n_capture, int n_playback)
{
    int port_flags = JackPortIsOutput;
    int chn;
    jack_port_t *port;
    char buf[32];

    capture_ports = NULL;
    for (chn = 0; chn < n_capture; chn++)
    {
        snprintf (buf, sizeof(buf) - 1, "capture_%u", chn + 1);
        port = jack_port_register (client, buf, JACK_DEFAULT_AUDIO_TYPE, port_flags, 0);
        if (!port)
        {
            jack_error ("jacknet_client: cannot register port for %s", buf);
            break;
        }
        capture_srcs = jack_slist_append(capture_srcs, src_new(SRC_LINEAR, 1, NULL));
        capture_ports = jack_slist_append(capture_ports, port);
    }

    port_flags = JackPortIsInput;

    playback_ports = NULL;
    for (chn = 0; chn < n_playback; chn++)
    {
        snprintf (buf, sizeof (buf) - 1, "playback_%u", chn + 1);
        port = jack_port_register (client, buf, JACK_DEFAULT_AUDIO_TYPE, port_flags, 0);
        if (!port)
        {
            jack_error ("jacknet_client: cannot register port for %s", buf);
            break;
        }
        playback_srcs = jack_slist_append (playback_srcs, src_new (SRC_LINEAR, 1, NULL));
        playback_ports = jack_slist_append (playback_ports, port);
    }
}

/**
 * The Sync callback... sync state is set elsewhere...
 * we will see if this is working correctly.
 * i dont really believe in it yet.
 */
int 
sync_cb (jack_transport_state_t state, jack_position_t *pos, void *arg)
{
    static int latency_count = 0;
    int retval = sync_state;
    if (latency_count)
    {
        latency_count--;
        retval = 0;
    }
    else if (state == JackTransportStarting && last_transport_state != JackTransportStarting)
    {
        retval = 0;
        latency_count = latency - 1;
    }
    last_transport_state = state;
    return retval;
}


/**
 * The process callback for this JACK application.
 * It is called by JACK at the appropriate times.
 */
int
process (jack_nframes_t nframes, void *arg)
{
    //static int old_count = 0;

    //SRC_DATA src;
    jack_nframes_t net_period = (float) nframes / (float) factor;

    //jack_position_t jack_pos;

    int rx_bufsize =  get_sample_size (bitdepth) * capture_channels * net_period + sizeof (jacknet_packet_header);
    int tx_bufsize =  get_sample_size (bitdepth) * playback_channels * net_period + sizeof (jacknet_packet_header);

    jack_default_audio_sample_t *buf;
    jack_port_t *port;
    JSList *node;
    channel_t chn;
    int size;

    jack_position_t local_trans_pos;

    uint32_t *packet_buf, *packet_bufX;

    /* Allocate a buffer where both In and Out Buffer will fit */
    packet_buf = alloca ((rx_bufsize > tx_bufsize) ? rx_bufsize : tx_bufsize);


    jacknet_packet_header *pkthdr = (jacknet_packet_header*) packet_buf;

    packet_bufX = packet_buf + sizeof (jacknet_packet_header) / sizeof (uint32_t);

    /* ---- receive ---- */
ReadAgain:
    if (reply_port)
        size = netjack_recv(insockfd, (char *)packet_buf, rx_bufsize, MSG_DONTWAIT, 1400);
    else
        size = netjack_recv(outsockfd, (char *)packet_buf, rx_bufsize, MSG_DONTWAIT, 1400);

    packet_header_ntoh(pkthdr);
    // evaluate rcvd data.

    // XXX: think about this a little more...
    //if((size == rx_bufsize) && (framecnt - pkthdr->framecnt) >= latency) {
    if ((size == rx_bufsize))
    {
        cont_miss = 0;
        if ((framecnt - pkthdr->framecnt) > latency)
        {
            jack_log("FRAMCNT_DIFF = %d  -----  A Packet was lost, or did came too late (try -l %d) ", pkthdr->framecnt - framecnt, framecnt - pkthdr->framecnt);
            goto ReadAgain;
        }
        // packet has expected size
        render_payload_to_jack_ports(bitdepth, packet_bufX, net_period, capture_ports, capture_srcs, nframes);
        // Now evaluate packet header;
        if (sync_state != pkthdr->sync_state)
        {
            jack_log("sync = %d", pkthdr->sync_state);
        }
        sync_state = pkthdr->sync_state;
    }
    else
    {
        jack_log("Packet Miss: (expected: %d, got: %d) framecnt=%d", rx_bufsize, size, framecnt);
        cont_miss += 1;
        chn = 0;
        node = capture_ports;
        while (node != NULL)
        {
            int i;
            port = (jack_port_t *) node->data;
            buf = jack_port_get_buffer (port, nframes);
            for (i = 0; i < nframes; i++)
            {
                buf[i] = 0.0;
            }
            node = jack_slist_next (node);
            chn++;
        }
    }

    /* reset packet_bufX... and then send... */
    packet_bufX = packet_buf + sizeof(jacknet_packet_header) / sizeof(jack_default_audio_sample_t);

    /* ---- send ---- */
    render_jack_ports_to_payload(bitdepth, playback_ports, playback_srcs, nframes, packet_bufX, net_period);
    /* fill in packet hdr */
    pkthdr->transport_state = jack_transport_query(client, &local_trans_pos);
    pkthdr->transport_frame = local_trans_pos.frame;
    pkthdr->framecnt = framecnt;
    pkthdr->latency = latency;
    pkthdr->reply_port = reply_port;

    pkthdr->sample_rate = jack_get_sample_rate(client);
    pkthdr->period_size = nframes;

    packet_header_hton(pkthdr);
    if (cont_miss < 10) {
        netjack_sendto(outsockfd, (char *)packet_buf, tx_bufsize, 0, &destaddr, sizeof(destaddr), 1400);
    } else {
        if (cont_miss > 50)
            cont_miss = 5;
    }

    framecnt++;
    return 0;
}

/**
 * This is the shutdown callback for this JACK application.
 * It is called by JACK if the server ever shuts down or
 * decides to disconnect the client.
 */
void
jack_shutdown (void *arg)
{
	jack_error ("jack_shutdown was called");
}

void
init_sockaddr_in (struct sockaddr_in *name, const char *hostname, uint16_t port)
{
    name->sin_family = AF_INET ;
    name->sin_port = htons (port) ;
    if (hostname)
    {
        struct hostent *hostinfo = gethostbyname (hostname);
        if (hostinfo == NULL)
        {
            jack_error ("init_sockaddr_in: unknown host: %s", hostname);
        }
        name->sin_addr = *(struct in_addr *) hostinfo->h_addr;
    }
    else
    {
        name->sin_addr.s_addr = htonl (INADDR_ANY) ;
    }
}

/**
 * This required entry point is called after the client is loaded by
 * jack_internal_client_load().
 *
 * @param client pointer to JACK client structure.
 * @param load_init character string passed to the load operation.
 *
 * @return 0 if successful; otherwise jack_finish() will be called and
 * the client terminated immediately.
 */
int
jack_initialize (jack_client_t *int_client, const char *load_init)
{
    int argc = 2; // getopt() starts parsing at the third element of argv
    char *argv[32];
    char *load_init_copy, *current_arg, *jack_name, *peer_ip;
    extern char *optarg;
    extern int optind, optopt;
    int index, option, errflag = 0, peer_socket;
    
    /* Is that any good ? */
    client = int_client;

    /* We'll use a copy of load_init to determine how much
        should be allocated for each argument */
    load_init_copy = strdup (load_init); // Make a copy of our init string

    /* Fill in some defaults */
    jack_name = (char*) malloc (10);
    peer_ip = (char*) malloc (9);
    sprintf (jack_name, "net_source");
    sprintf (peer_ip,   "localhost");
    peer_socket = 3000;

    /* Count the length of out first argument, and allocate accordingly */
    current_arg = strtok (load_init_copy, " ");
    argv[argc] = (char*) malloc (strlen (current_arg) * sizeof (char) + 1);
    /* Same for the rest of the list */
    while (sscanf (load_init, "%s%n", argv[argc], &index) == 1)
    {
        load_init += index; // Move on to the next arg
        argc++;
        if ((current_arg = strtok (NULL, " ")) != NULL)
        {
            argv[argc] = (char*) malloc (strlen (current_arg) * sizeof (char) + 1);
        }
    }

    /* let's make sure we can run multiple in process clients
        and still have our parametters parsed by getopt */
    optind = 2;
    /* We may now run our getopt loop just as if all of it came from a real command line */
    while ((option = getopt(argc, argv, ":n:p:s:P:C:l:r:f:b:")) != -1)
    {
        switch (option) 
    {
            case 'n':
                free(jack_name);
                jack_name = optarg;
                break;
            case 'p':
                free(peer_ip);
                peer_ip = optarg;
                break;
            case 's':
                peer_socket = atoi(optarg);
                break;
            case 'P':
                playback_channels = atoi(optarg);
                break;
            case 'C':
                capture_channels = atoi(optarg);
                break;
            case 'l':
                latency = atoi(optarg);
                break;
            case 'r':
                reply_port = atoi(optarg);
                break;
            case 'f':
                factor = atoi(optarg);
                break;
            case 'b':
                bitdepth = atoi(optarg);
                break;
            case ':': // -n or -p without operand
                jack_error ("Option -%c requires an operand, ignoring\n", optopt);
                errflag++;
                break;
            case '?':
                jack_error ("Unrecognized option: -%c, ignoring.\n"
                    "TODO : send the format to jack_load application...\n"
                    "Go bug the devs about it", optopt);
                errflag++;
        }
    }

    jack_info ("Here's our netclient setup:\n"
        "jack name: `%s'\n"
        "peer ip/hostname: `%s'\n"
        "peer_socket: `%i'\n"
        "playback channels: `%i'\n"
        "capture channels: `%i'\n"
        "latency: `%i'\n"
        "reply port: `%i'\n"
        "factor: `%i'\n"
        "bitdepth: `%i'\n",
        jack_name,
        peer_ip,
        peer_socket,
        playback_channels,
        capture_channels,
        latency,
        reply_port,
        factor,
        bitdepth);

    /* Yet another line to understand...
    src_state = src_new(SRC_LINEAR, 1, NULL); */

    outsockfd = socket (PF_INET, SOCK_DGRAM, 0);
    insockfd = socket (PF_INET, SOCK_DGRAM, 0);
    init_sockaddr_in ((struct sockaddr_in *) &destaddr, peer_ip, peer_socket);
    if (reply_port) {
        init_sockaddr_in ((struct sockaddr_in *) &bindaddr, NULL, reply_port);
        bind(insockfd, &bindaddr, sizeof (bindaddr));
    }

    /* Send a ping to the peer to measure round trip time ? 
        What if there are some ACLs on the remote host ?
        sendto(outsockfd, "x", 1, 0, &destaddr, sizeof(destaddr)); */

    /* Tell the JACK server to call `process()' whenever there is work to be done */
    jack_set_process_callback (client, process, 0);
    jack_set_sync_callback (client, sync_cb, 0);

    /* Tell the JACK server to call `jack_shutdown()' if
       it ever shuts down, either entirely, or if it
       just decides to stop calling us*/
    jack_on_shutdown (client, jack_shutdown, 0);

    /* Display the current sample rate
        TODO : Shall we remove that ? Who cares really... we're 'in process' ! */
    jack_log ("engine sample rate: %d", jack_get_sample_rate (client));
    alloc_ports (capture_channels, playback_channels);

    /* TODO : figure out what this does */
    jack_nframes_t net_period = (float) jack_get_buffer_size (client) / (float) factor;
    int rx_bufsize =  get_sample_size (bitdepth) * capture_channels * net_period + sizeof (jacknet_packet_header);
    global_packcache = packet_cache_new (latency + 5, rx_bufsize, 1400);

    /* Cleanup the mess */
    for (index = 2; index < argc; index++)
    {
        free (argv[index]);
    }

    /* tell the JACK server that we are ready to roll,
        exit gracefully if we got something wrong before that */
    if (errflag > 0)
    {
        jack_error ("It seems something went badly wrong");
        return -1;
    }
    if (jack_activate (client))
    {
        jack_error ("Cannot activate client");
        return -1;
    }
    return 0;
}

/**
 * This required entry point is called immediately before the client
 * is unloaded, which could happen due to a call to
 * jack_internal_client_unload(), or a nonzero return from either
 * jack_initialize() or inprocess().
 *
 * @param arg the same parameter provided to inprocess().
 */
void
jack_finish (void *arg)
{
    jack_info ("netsource client exits");
    packet_cache_free (global_packcache);
}
