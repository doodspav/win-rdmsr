#include <ntifs.h>
#include <ntddk.h>


#define DRIVER_NAME L"rdmsr"

#define NT_DEVICE_NAME  L"\\Device\\" DRIVER_NAME
#define SYM_DEVICE_NAME L"\\??\\" DRIVER_NAME

#define RDMSR_HEX 0xf32

#define IOCTL_RDMSR \
    CTL_CODE((0x8000 | RDMSR_HEX), (0x800 | RDMSR_HEX), METHOD_BUFFERED, FILE_READ_ACCESS)


DRIVER_INITIALIZE DriverEntry;

static DRIVER_UNLOAD RdmsrUnloadDriver;

_Dispatch_type_(IRP_MJ_CREATE)
_Dispatch_type_(IRP_MJ_CLOSE)
static DRIVER_DISPATCH RdmsrCreateClose;

_Dispatch_type_(IRP_MJ_DEVICE_CONTROL)
static DRIVER_DISPATCH RdmsrDeviceControl;

static BOOLEAN SupportsMsr(VOID);

static BOOLEAN IsAdmin(_In_ PIRP Irp);
static BOOLEAN IsImpersonatingAdmin(_In_ PIRP Irp);

static _Ret_maybenull_ PETHREAD IrpInitiatorThread(_In_ PIRP Irp);
static _Ret_maybenull_ PEPROCESS IrpInitiatorProcess(_In_ PIRP Irp);


NTSTATUS
DriverEntry(
    PDRIVER_OBJECT  DriverObject,
    PUNICODE_STRING RegistryPath
)
{
    NTSTATUS ntStatus = STATUS_NOT_SUPPORTED;

    UNICODE_STRING ntDeviceName  = RTL_CONSTANT_STRING(NT_DEVICE_NAME);
    UNICODE_STRING symDeviceName = RTL_CONSTANT_STRING(SYM_DEVICE_NAME);
    PDEVICE_OBJECT deviceObject  = NULL;

    UNREFERENCED_PARAMETER(RegistryPath);

    if (!SupportsMsr())
    {
        return ntStatus;
    }

    ntStatus = IoCreateDevice(
        DriverObject,
        0,
        &ntDeviceName,
        FILE_DEVICE_UNKNOWN,
        FILE_DEVICE_SECURE_OPEN,
        FALSE,
        &deviceObject
    );

    if (!NT_SUCCESS(ntStatus))
    {
        return ntStatus;
    }

    DriverObject->MajorFunction[IRP_MJ_CREATE] = RdmsrCreateClose;
    DriverObject->MajorFunction[IRP_MJ_CLOSE]  = RdmsrCreateClose;
    DriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = RdmsrDeviceControl;
    DriverObject->DriverUnload = RdmsrUnloadDriver;

    ntStatus = IoCreateSymbolicLink(
        &symDeviceName,
        &ntDeviceName
    );

    if (!NT_SUCCESS(ntStatus))
    {
        IoDeleteDevice(deviceObject);
    }

    return ntStatus;
}


static VOID
RdmsrUnloadDriver(
    _In_ PDRIVER_OBJECT DriverObject
)
{
    PDEVICE_OBJECT deviceObject  = DriverObject->DeviceObject;
    UNICODE_STRING symDeviceName = RTL_CONSTANT_STRING(SYM_DEVICE_NAME);

    PAGED_CODE();

    IoDeleteSymbolicLink(&symDeviceName);

    if (deviceObject != NULL)
    {
        IoDeleteDevice(deviceObject);
    }
}


static NTSTATUS
RdmsrCreateClose(
    PDEVICE_OBJECT DeviceObject,
    PIRP           Irp
)
{
    UNREFERENCED_PARAMETER(DeviceObject);

    PAGED_CODE();

    Irp->IoStatus.Status = STATUS_SUCCESS;
    Irp->IoStatus.Information = 0;

    IoCompleteRequest(Irp, IO_NO_INCREMENT);

    return STATUS_SUCCESS;
}


static NTSTATUS
RdmsrDeviceControl(
    PDEVICE_OBJECT DeviceObject,
    PIRP           Irp
)
{
    NTSTATUS ntStatus = STATUS_SUCCESS;

    PIO_STACK_LOCATION irpSp;
    ULONG inBufLen, outBufLen;
    PCHAR inBuf, outBuf;

    INT32 msrAddress;
    INT64 msrValue;

    UNREFERENCED_PARAMETER(DeviceObject);

    PAGED_CODE();

    if ( (Irp->RequestorMode == UserMode)
         && !IsAdmin(Irp)
         && !IsImpersonatingAdmin(Irp) )
    {
        ntStatus = STATUS_ACCESS_VIOLATION;
        goto End;
    }

    irpSp = IoGetCurrentIrpStackLocation(Irp);

    switch (irpSp->Parameters.DeviceIoControl.IoControlCode)
    {
    case IOCTL_RDMSR:

        inBufLen  = irpSp->Parameters.DeviceIoControl.InputBufferLength;
        outBufLen = irpSp->Parameters.DeviceIoControl.OutputBufferLength;

        if ((inBufLen != sizeof(msrAddress)) || (outBufLen != sizeof(msrValue)))
        {
            ntStatus = STATUS_INVALID_BUFFER_SIZE;
            break;
        }

        inBuf  = Irp->AssociatedIrp.SystemBuffer;
        outBuf = Irp->AssociatedIrp.SystemBuffer;

        RtlCopyBytes(&msrAddress, inBuf, inBufLen);

        __try {
            msrValue = __readmsr(msrAddress);
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            ntStatus = STATUS_NOT_SUPPORTED;
            break;
        }

        RtlCopyBytes(outBuf, &msrValue, outBufLen);
        
        Irp->IoStatus.Information = outBufLen;
        break;

    default:

        ntStatus = STATUS_INVALID_DEVICE_REQUEST;
        break;
    }

End:
    Irp->IoStatus.Status = ntStatus;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return ntStatus;
}


static BOOLEAN
SupportsMsr(
    VOID
)
{
    BOOLEAN result = TRUE;

    PAGED_CODE();

    __try {
        (void)__readmsr(0);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        result = (GetExceptionCode() != STATUS_ILLEGAL_INSTRUCTION);
    }

    return result;
}


static BOOLEAN
IsAdmin(
    _In_ PIRP Irp
)
{
    BOOLEAN result = FALSE;
    
    PEPROCESS process;
    PACCESS_TOKEN token;

    PAGED_CODE();

    process = IrpInitiatorProcess(Irp);
    if (process == NULL) { process = IoGetRequestorProcess(Irp); }
    if (process == NULL) { process = IoGetCurrentProcess(); }

    token = PsReferencePrimaryToken(process);
    if (token != NULL)
    {
        result = SeTokenIsAdmin(token);
        PsDereferencePrimaryToken(token);
    }

    return result;
}


static BOOLEAN
IsImpersonatingAdmin(
    _In_ PIRP Irp
)
{
    BOOLEAN result = FALSE;

    PETHREAD thread;
    PACCESS_TOKEN token;
    SECURITY_IMPERSONATION_LEVEL impersonationLevel;
    BOOLEAN copyOnOpen, effectiveOnly;

    PAGED_CODE();

    thread = IrpInitiatorThread(Irp);
    if (thread == NULL) { goto End; }

    token = PsReferenceImpersonationToken(
        thread,
        &copyOnOpen,
        &effectiveOnly,
        &impersonationLevel
    );

    if (token != NULL)
    {
        result = SeTokenIsAdmin(token);
        PsDereferenceImpersonationToken(token);
    }

End:
    return result;
}


static _Ret_maybenull_ PETHREAD 
IrpInitiatorThread(_In_ PIRP Irp)
{
    PAGED_CODE();

    return Irp->Tail.Overlay.Thread;
}


static _Ret_maybenull_ PEPROCESS 
IrpInitiatorProcess(_In_ PIRP Irp)
{
    PETHREAD thread = IrpInitiatorThread(Irp);
    PEPROCESS process = NULL;

    PAGED_CODE();

    if (thread != NULL)
    {
        process = IoThreadToProcess(thread);
    }

    return process;
}
