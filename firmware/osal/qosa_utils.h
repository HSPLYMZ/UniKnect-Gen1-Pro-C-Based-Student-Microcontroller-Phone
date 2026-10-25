/* at_client includes this header but uses nothing from it in the code we
 * vendor. Kept as a stub so the include line in at_client.c stays untouched.
 * Add uptime / hex helpers here if a future at_client revision needs them.
 */
#ifndef QOSA_UTILS_H
#define QOSA_UTILS_H

#include <stdint.h>

uint64_t qosa_get_uptime_milliseconds(void);

#endif /* QOSA_UTILS_H */
