#include "rdmsr.h"

#include <windows.h>
#include <assert.h>


#define RDMSR_PATH "\\\\.\\rdmsr"

#define RDMSR_HEX  0x0f32

#define IOCTL_RDMSR \
    CTL_CODE((0x8000 | RDMSR_HEX), (0x800 | RDMSR_HEX), METHOD_BUFFERED, FILE_READ_ACCESS)

#define ATOMIC_LOAD_HANDLE(lvalue) \
    InterlockedCompareExchangePointer(&lvalue, NULL, NULL)


static HANDLE g_driver = NULL;


int rdmsr_init(void)
{
    HANDLE driver;

    if (ATOMIC_LOAD_HANDLE(g_driver) != NULL) { return 1; }

    driver = CreateFileA(
        RDMSR_PATH,
        GENERIC_READ, FILE_SHARE_READ,
        NULL, OPEN_EXISTING,
        0, NULL
    );

    if (driver == INVALID_HANDLE_VALUE)
    {
        if (ATOMIC_LOAD_HANDLE(g_driver) != NULL) { return 1; }
        else { return 0; }
    }

    (void) InterlockedCompareExchangePointer(
        &g_driver,
        driver,
        NULL
    );

    if (ATOMIC_LOAD_HANDLE(g_driver) != driver) { CloseHandle(driver); }

    return 1;
}


int rdmsr(
    uint32_t address,
    uint64_t *out_value
)
{
    DWORD returned;
    BOOL status;

    assert(ATOMIC_LOAD_HANDLE(g_driver) != NULL);
    assert(sizeof(address) == 4);
    assert(sizeof(*out_value) == 8);

    status = DeviceIoControl(
        ATOMIC_LOAD_HANDLE(g_driver), IOCTL_RDMSR,
        &address, sizeof(address),
        out_value, sizeof(*out_value),
        &returned, NULL
    );

    if (GetLastError() == ERROR_NOACCESS) {
        RaiseException(STATUS_ACCESS_VIOLATION, 0, 0, NULL);
    }
    else if (status == 0) {
        assert(GetLastError() == ERROR_NOT_SUPPORTED);
    }
    else {
        assert(returned == sizeof(*out_value));
    }

    return status != 0;
}
