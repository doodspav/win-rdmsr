#ifndef RDMSR_H
#define RDMSR_H


#include <stdint.h>


#ifdef __cplusplus
extern "C" {
#endif


/*
 * PREREQUISITES:
 *
 * - rdmsr driver must be loaded
 *   -> or rdmsr_init will return 0
 *
 * - caller must be running with elevated permissions
 *   -> or rdmsr will raise a STATUS_ACCESS_VIOLATION exception
 */


/*
 * attempts to open a driver handle (thread-safe)
 *
 * - only needs to be called once
 * - may be called multiple times with no ill-effect
 * - after the first successful call, subsequent calls will always succeed
 *
 * - success: returns 1
 * - error: returns 0 and Last-Error code is set
 */
int rdmsr_init(void);


/*
 * attempts to read msr (thread-safe)
 *
 * - supported address: sets *out_value and returns 1
 * - unsupported address: returns 0
 * - rdmsr_init not called successfully: undefined behaviour
 */
int rdmsr(
    uint32_t address,
    uint64_t *out_value
);


#ifdef __cplusplus
}
#endif


#endif  /* !RDMSR_H */
