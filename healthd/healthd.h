/*
 * Copyright (C) 2013 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef _HEALTHD_H_
#define _HEALTHD_H_

#include <batteryservice/BatteryService.h>

// The following are implemented in libhealthd_board to handle board-specific
// behavior.
//
// Set periodic poll intervals in seconds.
//
// fast_interval is used while the device is not in suspend, or in suspend and
// connected to a charger (to watch for battery overheat due to charging).
// The default value is 60 (1 minute).  Value -1 turns off fast_interval
// polling.
//
// slow_interval is used when the device is in suspend and not connected to a
// charger (to watch for a battery drained to zero remaining capacity).  The
// default value is 600 (10 minutes).  Value -1 tuns off slow interval
// polling.
//
// To use the default values, this function can simply return without
// modifying the parameters.

void healthd_board_poll_intervals(int *fast_interval, int *slow_interval);

// Process updated battery property values.  This function is called when
// the kernel sends updated battery status via a uevent from the power_supply
// subsystem, or when updated values are polled by healthd, as for periodic
// poll of battery state.
//
// props are the battery properties read from the kernel.  These values may
// be modified in this call, prior to sending the modified values to the
// Android runtime.
//
// Return 0 to indicate the usual kernel log battery status heartbeat message
// is to be logged, or non-zero to prevent logging this information.

int healthd_board_battery_update(struct android::BatteryProperties *props);

#endif /* _HEALTHD_H_ */
