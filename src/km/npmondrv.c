#include <fltKernel.h>
#include <wdm.h>
#include <ntintsafe.h>

FLT_PREOP_CALLBACK_STATUS FLTAPI PreOperationCreate(
    _Inout_ PFLT_CALLBACK_DATA Data,
    _In_ PCFLT_RELATED_OBJECTS FltObjects,
    _Flt_CompletionContext_Outptr_ PVOID* CompletionContext);

FLT_PREOP_CALLBACK_STATUS FLTAPI PreOperationCreateNamedPipe(
    _Inout_ PFLT_CALLBACK_DATA Data,
    _In_ PCFLT_RELATED_OBJECTS FltObjects,
    _Flt_CompletionContext_Outptr_ PVOID* CompletionContext);

FLT_PREOP_CALLBACK_STATUS FLTAPI PreOperationRead(
    _Inout_ PFLT_CALLBACK_DATA Data,
    _In_ PCFLT_RELATED_OBJECTS FltObjects,
    _Flt_CompletionContext_Outptr_ PVOID* CompletionContext);

FLT_PREOP_CALLBACK_STATUS FLTAPI PreOperationWrite(
    _Inout_ PFLT_CALLBACK_DATA Data,
    _In_ PCFLT_RELATED_OBJECTS FltObjects,
    _Flt_CompletionContext_Outptr_ PVOID* CompletionContext);

NTSTATUS FLTAPI InstanceFilterUnloadCallback(
        _In_ FLT_FILTER_UNLOAD_FLAGS Flags);

NTSTATUS FLTAPI InstanceSetupCallback(
    _In_ PCFLT_RELATED_OBJECTS  FltObjects,
    _In_ FLT_INSTANCE_SETUP_FLAGS  Flags,
    _In_ DEVICE_TYPE  VolumeDeviceType,
    _In_ FLT_FILESYSTEM_TYPE  VolumeFilesystemType);

NTSTATUS FLTAPI InstanceQueryTeardownCallback(
    _In_ PCFLT_RELATED_OBJECTS FltObjects,
    _In_ FLT_INSTANCE_QUERY_TEARDOWN_FLAGS Flags);

CONST FLT_OPERATION_REGISTRATION g_callbacks[] =
{
    {
        IRP_MJ_CREATE,
        0,
        PreOperationCreate,
        0
    },
    {
        IRP_MJ_CREATE_NAMED_PIPE,
        0,
        PreOperationCreateNamedPipe,
        0
    },
    {
        IRP_MJ_READ,
        0,
        PreOperationRead,
        0
    },
    {
        IRP_MJ_WRITE,
        0,
        PreOperationWrite,
        0
    },
    {IRP_MJ_OPERATION_END}
};

CONST FLT_REGISTRATION g_filterRegistration =
{
    sizeof(FLT_REGISTRATION),            //  Size
    FLT_REGISTRATION_VERSION,            //  Version
    FLTFL_REGISTRATION_SUPPORT_NPFS_MSFS,//  Flags
    NULL,                                //  Context registration
    g_callbacks,                         //  Operation callbacks
    InstanceFilterUnloadCallback,        //  FilterUnload
    InstanceSetupCallback,               //  InstanceSetup
    InstanceQueryTeardownCallback,       //  InstanceQueryTeardown
    NULL,                                //  InstanceTeardownStart
    NULL,                                //  InstanceTeardownComplete
    NULL,                                //  GenerateFileName
    NULL,                                //  GenerateDestinationFileName
    NULL                                 //  NormalizeNameComponent
};

PFLT_FILTER g_minifilterHandle = NULL;

NTSTATUS DriverEntry(
        IN PDRIVER_OBJECT DriverObject,
        IN PUNICODE_STRING RegistryPath)
{
    NTSTATUS status = FltRegisterFilter(DriverObject, &g_filterRegistration, &g_minifilterHandle);
    if (!NT_SUCCESS(status))
    {
        return status;
    }

    status = FltStartFiltering(g_minifilterHandle);
    if (!NT_SUCCESS(status))
    {
        FltUnregisterFilter(g_minifilterHandle);
    }

    return status;
}

FLT_PREOP_CALLBACK_STATUS FLTAPI PreOperationCreate(
    _Inout_ PFLT_CALLBACK_DATA Data,
    _In_ PCFLT_RELATED_OBJECTS FltObjects,
    _Flt_CompletionContext_Outptr_ PVOID* CompletionContext)
{
    if(FltObjects->FileObject->DeviceObject->DeviceType ==
            FILE_DEVICE_NAMED_PIPE) {
        DbgPrint("Named pipe create captured: %wZ\n",
                FltObjects->FileObject->FileName);
    }

    return FLT_PREOP_SUCCESS_NO_CALLBACK;
}

FLT_PREOP_CALLBACK_STATUS FLTAPI PreOperationCreateNamedPipe(
    _Inout_ PFLT_CALLBACK_DATA Data,
    _In_ PCFLT_RELATED_OBJECTS FltObjects,
    _Flt_CompletionContext_Outptr_ PVOID* CompletionContext)
{
    DbgPrint("CreateNamedPipe captured: %wZ\n",
            &FltObjects->FileObject->FileName);

    return FLT_PREOP_SUCCESS_NO_CALLBACK;
}

FLT_PREOP_CALLBACK_STATUS FLTAPI PreOperationRead(
    _Inout_ PFLT_CALLBACK_DATA Data,
    _In_ PCFLT_RELATED_OBJECTS FltObjects,
    _Flt_CompletionContext_Outptr_ PVOID* CompletionContext)
{
    //DbgPrint("Read captured: %wZ ", &Data->Iopb->TargetFileObject->FileName);

    if(FltObjects->FileObject->DeviceObject->DeviceType ==
            FILE_DEVICE_NAMED_PIPE) {
        DbgPrint("Named pipe read captured: %wZ\n",
                FltObjects->FileObject->FileName);
    }

    return FLT_PREOP_SUCCESS_NO_CALLBACK;
}
 
FLT_PREOP_CALLBACK_STATUS FLTAPI PreOperationWrite(
    _Inout_ PFLT_CALLBACK_DATA Data,
    _In_ PCFLT_RELATED_OBJECTS FltObjects,
    _Flt_CompletionContext_Outptr_ PVOID* CompletionContext)
{
    //DbgPrint("Write captured: %wZ ", &Data->Iopb->TargetFileObject->FileName);

    if(FltObjects->FileObject->DeviceObject->DeviceType ==
            FILE_DEVICE_NAMED_PIPE) {
        DbgPrint("Named pipe write captured: %wZ\n",
                FltObjects->FileObject->FileName);
    }

    return FLT_PREOP_SUCCESS_NO_CALLBACK;
}

NTSTATUS FLTAPI InstanceFilterUnloadCallback(
    _In_ FLT_FILTER_UNLOAD_FLAGS Flags)
{
    if (NULL != g_minifilterHandle)
    {
        FltUnregisterFilter(g_minifilterHandle);
    }

    return STATUS_SUCCESS;
}

NTSTATUS FLTAPI InstanceSetupCallback(
    _In_ PCFLT_RELATED_OBJECTS  FltObjects,
    _In_ FLT_INSTANCE_SETUP_FLAGS  Flags,
    _In_ DEVICE_TYPE  VolumeDeviceType,
    _In_ FLT_FILESYSTEM_TYPE  VolumeFilesystemType)
{
    return STATUS_SUCCESS;
}

NTSTATUS FLTAPI InstanceQueryTeardownCallback(
    _In_ PCFLT_RELATED_OBJECTS FltObjects,
    _In_ FLT_INSTANCE_QUERY_TEARDOWN_FLAGS Flags)
{
    return STATUS_SUCCESS;
}
