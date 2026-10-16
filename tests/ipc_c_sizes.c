/* Kinestop host tests - compiles common/kinestop_ipc.h as C (its
 * _Static_assert branch) and reports the struct sizes the C compiler sees.
 *
 * Part of the sysmodule/overlay split of the original Kinestop by SertAy
 * (https://github.com/sertay1/Kinestop). */

#include <stddef.h>
#include "kinestop_ipc.h"

int kinestop_ipc_c_sizes(size_t* wire, size_t* status, size_t* calib) {
    *wire = sizeof(KinestopWireConfig);
    *status = sizeof(KinestopStatus);
    *calib = sizeof(KinestopCalibration);
    return 1;
}
