#ifndef __jack_hardware_h__
#define __jack_hardware_h__

#include <jack/types.h>

struct _jack_hardware;

typedef void (*JackHardwareReleaseFunction)(struct _jack_hardware *);
typedef int (*JackHardwareSetInputMonitorMaskFunction)(struct _jack_hardware *, unsigned long);
typedef int (*JackHardwareChangeSampleClockFunction)(struct _jack_hardware *, SampleClockMode);

typedef struct _jack_hardware {

    unsigned long capabilities;
    unsigned long input_monitor_mask;

    JackHardwareChangeSampleClockFunction change_sample_clock;
    JackHardwareSetInputMonitorMaskFunction set_input_monitor_mask;
    JackHardwareReleaseFunction release;

    void *private;

} jack_hardware_t;

jack_hardware_t * jack_hardware_new ();

#endif /* __jack_hardware_h__ */
