# User-Space API

This library wraps the functionality of the corresponding `rdmsr` driver.

## Prerequisites

- `rdmsr` driver is loaded
- `rdmsr_init()` caller needs: `FILE_READ_ACCESS` permissions for the driver device
- `rdmsr(...)` caller needs: administrator level or higher privileges
- `rdmsr_init()` must succeed before `rdmsr(...)` can be called

## Interface

The following two functions are provided:
- both functions are thread-safe (including with each other)
- both functions can be called multiple times
- subsequent calls to a successful `rdmsr_init()` call will always succeed

```c
/*
 * attempts to open a driver handle
 * - success: returns 1
 * - error: returns 0 and Last-Error code is set
 */
int rdmsr_init(void);
```
Reasons for failure:
- `rdmsr` driver was not loaded
- the caller does not have `FILE_READ_ACCESS` permissions for the driver device
 
```c
/*
 * attempts to read an MSR
 * - supported address: sets *out_value and returns 1
 * - unsupported address: returns 0
 * - this function may also "crash" if prerequisites are not met
 */
int rdmsr(
    uint32_t address,
    uint64_t *out_value
);
```
Reasons for crash:
- `assert()` triggered if `rdmsr_init()` has not been called successfully
- `STATUS_ACCESS_VIOLATION` exception raised if caller does not have adequate privileges

The exception can be caught and checked for using SEH.
