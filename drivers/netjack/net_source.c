/** @file simple_client.c
 *
 * @brief This simple client demonstrates the basic features of JACK
 * as they would be used by many applications.
 */

#include <stdio.h>
#include <errno.h>(#include <unistd.h>
#include <stdlib.h>
#include <string.h>

#include <netinet/in.h>
#include <netdb.h>

#include <alloca.h>

#include <jack/jack.h>

#include "net_driver.h"
#include "netjack_packet.h"
#include <samplerate.h>

JSList	   *capture_ports = NULL;
JSList	   *capture_srcs = NULL;
int capture_channels = 2;
JSList	   *playback_ports = NULL;
JSList	   *playback_srcs = NULL;
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
/*
 * This Function allocates all the I/O Ports which are added the lists.
 * XXX: jack-midi is underway... so dont forget it.
 */

void alloc_ports(int n_capture, int n_playback)
{
    int port_flags = JackPortIsOutput;
    int chn;
    jack_port_t *port;
    char buf[32];

    capture_ports = NULL;
    for (chn = 0; chn < n_capture; chn++) {
        snprintf (buf, sizeof(buf) - 1, "capture_%u", chn + 1);

        port = jack_port_register (client, buf,
                                   JACK_DEFAULT_AUDIO_TYPE,
                                   port_flags, 0);

        if (!port) {
            printf("jacknet_client: cannot register port for %s", buf);
            break;
        }

        capture_srcs = jack_slist_append(capture_srcs, src_new(SRC_LINEAR, 1, NULL));
        capture_ports = jack_slist_append(capture_ports, port);
    }

    port_flags = JackPortIsInput;

    playback_ports = NULL;
    for (chn = 0; chn < n_playback; chn++) {
        snprintf (buf, sizeof(buf) - 1, "playback_%u", chn + 1);

        port = jack_port_register (client, buf,
                                   JACK_DEFAULT_AUDIO_TYPE,
                                   port_flags, 0);

        if (!port) {
            printf("jacknet_client: cannot register port for %s", buf);
            break;
        }

        playback_srcs = jack_slist_append(playback_srcs, src_new(SRC_LINEAR, 1, NULL));
        playback_ports = jack_slist_append(playback_ports, port);
    }
}


/*
 * The Sync callback... sync state is set elsewhere...
 * we will see if this is working correctly.
 * i dont really believe in it yet.
 */

int sync_cb(jack_transport_state_t state, jack_position_t *pos, void *arg)
{
    static int latency_count = 0;
    int retval = sync_state;

    if (latency_count) {
        latency_count--;
        retval = 0;
    } else if (state == JackTransportStarting && last_transport_state != JackTransportStarting) {
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

    int rx_bufsize =  get_sample_size(bitdepth) * capture_channels * net_period + sizeof(jacknet_packet_header);
    int tx_bufsize =  get_sample_size(bitdepth) * playback_channels * net_period + sizeof(jacknet_packet_header);

    jack_default_audio_sample_t *buf;
    jack_port_t *port;
    JSList *node;
    channel_t chn;
    int size;

    jack_position_t local_trans_pos;

    uint32_t *packet_buf, *packet_bufX;

    // Allocate a buffer where both In and Out Buffer will fit
    packet_buf = alloca((rx_bufsize > tx_bufsize) ? rx_bufsize : tx_bufsize);


    jacknet_packet_header *pkthdr = (jacknet_packet_header *)packet_buf;

    packet_bufX = packet_buf + sizeof(jacknet_packet_header) / sizeof(uint32_t);

    //////////////// receive ////////////////
    ////////////////         ////////////////
ReadAgain:
    if (reply_port)
        size = netjack_recv(insockfd, (char *)packet_buf, rx_bufsize, MSG_DONTWAIT, 1400);
    else
        size = netjack_recv(outsockfd, (char *)packet_buf, rx_bufsize, MSG_DONTWAIT, 1400);

    packet_header_ntoh(pkthdr);
    // evaluate rcvd data.

    // XXX: think about this a little more...
    //if((size == rx_bufsize) && (framecnt - pkthdr->framecnt) >= latency) {
    if ((size == rx_bufsize)) {

        cont_miss = 0;
        if ((framecnt - pkthdr->framecnt) > latency) {
            printf("FRAMCNT_DIFF = %d  -----  A Packet was lost, or did came too late (try -l %d) \n", pkthdr->framecnt - framecnt, framecnt - pkthdr->framecnt);
            goto ReadAgain;
        }

        // packet has expected size

        render_payload_to_jack_ports(bitdepth, packet_bufX, net_period, capture_ports, capture_srcs, nframes);
        // Now evaluate packet header;
        if (sync_state != pkthdr->sync_state) printf("sync = %d\n", pkthdr->sync_state);
        sync_state = pkthdr->sync_state;
    } else {
        printf("Packet Miss: (expected: %d, got: %d) framecnt=%d\n", rx_bufsize, size, framecnt);
        cont_miss += 1;
        chn = 0;
        node = capture_ports;
        while (node != NULL) {
            int i;
            port = (jack_port_t *) node->data;
            buf = jack_port_get_buffer (port, nframes);

            for (i = 0; i < nframes; i++) {
                buf[i] = 0.0;
            }
            node = jack_slist_next (node);
            chn++;
        }
    }
    ////////////////
    ////////////////

    // reset packet_bufX... and then send...

    packet_bufX = packet_buf + sizeof(jacknet_packet_header) / sizeof(jack_default_audio_sample_t);

    //////////////// send ////////////////
    ////////////////      ////////////////

    render_jack_ports_to_payload(bitdepth, playback_ports, playback_srcs, nframes, packet_bufX, net_period);

    // fill in packet hdr

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

    exit (1);
}

void
init_sockaddr_in (struct sockaddr_in *name , const char *hostname , uint16_t port)
{
    name->sin_family = AF_INET ;
    name->sin_port = htons (port) ;
    if (hostname) {
        struct hostent *hostinfo = gethostbyname (hostname) ;
        if (hostinfo == NULL) {
            fprintf (stderr , "init_sockaddr_in: unknown host: %s.\n" , hostname) ;
        }
        name->sin_addr = *(struct in_addr *) hostinfo->h_addr ;
    } else {
        name->sin_addr.s_addr = htonl (INADDR_ANY) ;
    }
}

void
printUsage()
{
    fprintf(stderr, "usage: net_source [-n <jack name>] [-s <socket>] [-C <num channels>] [-P <num channels>] -p <host peer>\n"
            "\n"
            "  -n <jack name> - reports a different name to jack\n"
            "  -s <socket> select another socket than the default (3000).\n"
            "  -p <host peer> the hostname of the \"other\" machine running the jack-slave.\n"
            "  -P <num channels> number of playback channels.\n"
            "  -C <num channels> number of capture channels.\n"
            "  -l <latency in periods> number of packets on the wire to approach\n"
            "  -r <reply port> When using a firewall use this port for incoming packets\n"
            "  -f <downsample ratio> downsample data in the wire by this factor.\n"
            "  -b <bitdepth> Set transport to use 16bit or 8bit\n"
            "\n");
}

int
main (int argc, char *argv[])
{
    char jack_name[30] = "net_source";
    char *peer_ip = "localhost";
    int peer_socket = 3000;

    if (argc < 3) {
        printUsage();
        return 1;
    }
    extern char *optarg;
    extern int optind, optopt;
    int errflg = 0;
    int c;

    while ((c = getopt(argc, argv, ":n:p:s:C:P:l:r:f:b:")) != -1) {
        switch (c) {
            case 'n':
                strcpy(jack_name, optarg);
                break;
            case 'p':
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
            case ':':       /* -n or -p without operand */
                fprintf(stderr,
                        "Option -%c requires an operand\n", optopt);
                errflg++;
                break;
            case '?':
                fprintf(stderr,
                        "Unrecognized option: -%c\n", optopt);
                errflg++;
        }
    }
    if (errflg) {
        printUsage();
        exit(2);
    }

    //src_state = src_new(SRC_LINEAR, 1, NULL);

    outsockfd = socket(PF_INET, SOCK_DGRAM, 0);
    insockfd = socket(PF_INET, SOCK_DGRAM, 0);
    init_sockaddr_in((struct sockaddr_in *)&destaddr, peer_ip, peer_socket);
    if (reply_port) {
        init_sockaddr_in((struct sockaddr_in *)&bindaddr, NULL, reply_port);
        bind(insockfd, &bindaddr, sizeof(bindaddr));
    }
    /* try to become a client of the JACK server */

    if ((client = jack_client_new (jack_name)) == 0) {
        fprintf (stderr, "jack server not running?\n");
        return 1;
    }

    /*
       send a ping to the peer 
       -- this needs to be made more robust --
       */

    //sendto(outsockfd, "x", 1, 0, &destaddr, sizeof(destaddr));

    /* tell the JACK server to call `process()' whenever
       there is work to be done.
       */

    jack_set_process_callback (client, process, 0);
    jack_set_sync_callback (client, sync_cb, 0);

    /* tell the JACK server to call `jack_shutdown()' if
       it ever shuts down, either entirely, or if it
       just decides to stop calling us.
       */

    jack_on_shutdown (client, jack_shutdown, 0);


    /* display the current sample rate.
    */

    printf ("engine sample rate: %" PRIu32 "\n",
            jack_get_sample_rate (client));

    alloc_ports(capture_channels, playback_channels);

    jack_nframes_t net_period = (float) jack_get_buffer_size(client) / (float) factor;
    int rx_bufsize =  get_sample_size(bitdepth) * capture_channels * net_period + sizeof(jacknet_packet_header);
    global_packcache = packet_cache_new(latency + 5, rx_bufsize, 1400);

    /* tell the JACK server that we are ready to roll */

    if (jack_activate (client)) {
        fprintf (stderr, "cannot activate client");
        return 1;
    }

    // Now sleep forever.......

    while (1)
        sleep(100);

    // Never reached. Well we will be a GtkApp someday....

    packet_cache_free(global_packcache);
    jack_client_close(client);
    exit (0);
}

