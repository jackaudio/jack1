#ifndef __jack_hammerfall_h__
#define __jack_hammerfall_h__

#include <sys/time.h>

typedef struct {
    int lock_status[3];
    int sync_status[3];
    int said_that_spdif_is_fine;
    pthread_t monitor_thread;
    alsa_driver_t *driver;
    struct timespec monitor_interval;
} hammerfall_t;

jack_hardware_t *jack_alsa_hammerfall_hw_new (alsa_driver_t *driver);

#endif /* __jack_hammerfall_h__*/
