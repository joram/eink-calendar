#pragma once

#include <time.h>

/** Set system time: NTP if WIFI_SSID is set, else build timestamp from __DATE__ / __TIME__. */
bool clockSync();
