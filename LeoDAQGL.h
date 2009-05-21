#ifndef LeoDAQGL_H
#define LeoDAQGL_H

/**
   @file LeoDAQGL.h - some constants and other shared values.
*/

#include "Util.h"
#include "TypeDefs.h"
#include "Version.h"

#define INTAN_SRATE 29630
#define DAQ_TIMEOUT 2.5
#define FAST_SETTLE_TIME_MS 1500
#define MUX_CHANS_PER_PHYS_CHAN 15
#define LOCK_TIMEOUT_MS 2000
#define TASK_READ_FREQ_HZ 10
#define APPNAME "LeoDAQGL"
#define DOWNSAMPLE_TARGET_HZ 1000
#define DEFAULT_GRAPH_TIME_SECS 3.0

#endif
