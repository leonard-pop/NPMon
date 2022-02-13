#include <fltKernel.h>
#include <wdm.h>
#include <ntintsafe.h>
#include "commondefs.h"

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

NTSTATUS ClientConnectedCallback(
    _In_ PFLT_PORT ClientPort,
    _In_ PVOID ServerPortCookie,
    _In_ PVOID ConnectionContext,
    _In_ ULONG SizeOfContext,
    _Out_ PVOID *ConnectionPortCookie);

void ClientDisconnectedCallback(
    _In_ PVOID Cookie);

NTSTATUS MessageCallback(
    _In_ PVOID PortCookie,
    _In_ PVOID InputBuffer OPTIONAL,
    _In_ ULONG InputBufferLength,
    _Out_ PVOID OutputBuffer OPTIONAL,
    _In_ ULONG OutputBufferLength,
    _Out_ PULONG ReturnOutputBufferLength);

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

CONST FLT_REGISTRATION g_filter_registration =
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

PFLT_FILTER g_minifilter_handle = NULL;
PFLT_PORT g_server_port = NULL, g_client_port = NULL;
KGUARDED_MUTEX comm_mutex;

NTSTATUS DriverEntry(
    IN PDRIVER_OBJECT DriverObject,
    IN PUNICODE_STRING RegistryPath)
{
    UNREFERENCED_PARAMETER(DriverObject);
    UNREFERENCED_PARAMETER(RegistryPath);

    NTSTATUS status = STATUS_SUCCESS;
    PSECURITY_DESCRIPTOR port_security_descriptor = NULL;
    OBJECT_ATTRIBUTES port_object_attributes;
    UNICODE_STRING port_name;

    status = FltRegisterFilter(DriverObject, &g_filter_registration, &g_minifilter_handle);
    if(!NT_SUCCESS(status))
    {
        goto done;
    }

    status = FltStartFiltering(g_minifilter_handle);
    if(!NT_SUCCESS(status))
    {
        goto done;
    }

    status = FltBuildDefaultSecurityDescriptor(&port_security_descriptor,
            FLT_PORT_ALL_ACCESS);
    if(!NT_SUCCESS(status)) {
        goto done;
    }

    RtlInitUnicodeString(&port_name, COMM_PORT_NAME);
    InitializeObjectAttributes(&port_object_attributes, &port_name,
            OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, NULL,
            port_security_descriptor);

    status = FltCreateCommunicationPort(g_minifilter_handle, &g_server_port,
            &port_object_attributes, NULL, ClientConnectedCallback,
            ClientDisconnectedCallback, MessageCallback, 1);
    if(!NT_SUCCESS(status)) {
        goto done;
    }

    ExInitializeFastMutex(&comm_mutex);

done:
    if(port_security_descriptor)
    {
        FltFreeSecurityDescriptor(port_security_descriptor);
    }
    if(!NT_SUCCESS(status))
    {
        if(g_minifilter_handle)
        {
            FltUnregisterFilter(g_minifilter_handle);
        }
    }

    return status;
}

FLT_PREOP_CALLBACK_STATUS FLTAPI PreOperationCreate(
    _Inout_ PFLT_CALLBACK_DATA Data,
    _In_ PCFLT_RELATED_OBJECTS FltObjects,
    _Flt_CompletionContext_Outptr_ PVOID* CompletionContext)
{
    UNREFERENCED_PARAMETER(Data);
    UNREFERENCED_PARAMETER(CompletionContext);

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
    UNREFERENCED_PARAMETER(Data);
    UNREFERENCED_PARAMETER(CompletionContext);

    DbgPrint("CreateNamedPipe captured: %wZ\n",
            &FltObjects->FileObject->FileName);

    return FLT_PREOP_SUCCESS_NO_CALLBACK;
}

FLT_PREOP_CALLBACK_STATUS FLTAPI PreOperationRead(
    _Inout_ PFLT_CALLBACK_DATA Data,
    _In_ PCFLT_RELATED_OBJECTS FltObjects,
    _Flt_CompletionContext_Outptr_ PVOID* CompletionContext)
{
    UNREFERENCED_PARAMETER(Data);
    UNREFERENCED_PARAMETER(CompletionContext);

    //DbgPrint("Read captured: %wZ ", &Data->Iopb->TargetFileObject->FileName);

    if(FltObjects->FileObject->DeviceObject->DeviceType !=
            FILE_DEVICE_NAMED_PIPE) {
        return FLT_PREOP_SUCCESS_NO_CALLBACK;
    }

    NTSTATUS status;
    PMDL *ReadMdl = NULL;
    PVOID ReadAddress = NULL;

    DbgPrint("Named pipe read captured: %wZ\n",
            FltObjects->FileObject->FileName);

    return FLT_PREOP_SUCCESS_NO_CALLBACK;
}

FLT_PREOP_CALLBACK_STATUS FLTAPI PreOperationWrite(
    _Inout_ PFLT_CALLBACK_DATA Data,
    _In_ PCFLT_RELATED_OBJECTS FltObjects,
    _Flt_CompletionContext_Outptr_ PVOID* CompletionContext)
{
    UNREFERENCED_PARAMETER(CompletionContext);

    //DbgPrint("Write captured: %wZ ", &Data->Iopb->TargetFileObject->FileName);

    if(FltObjects->FileObject->DeviceObject->DeviceType !=
            FILE_DEVICE_NAMED_PIPE) {
        return FLT_PREOP_SUCCESS_NO_CALLBACK;
    }

    NTSTATUS status;
    PMDL *mdl = NULL;
    PVOID mdl_address = NULL;
    ULONG write_length = Data->Iopb->Parameters.Write.Length;
    PVOID write_buffer = Data->Iopb->Parameters.Write.WriteBuffer;

    DbgPrint("Named pipe write captured: %wZ, length: %lu, buffer: %.*wZ\n",
            FltObjects->FileObject->FileName, write_length,
            write_length, write_buffer);

    if(g_client_port) {
        KeAcquireGuardedMutex(&comm_mutex);

        DbgPrint("Sending buffer to connected client (length %lu)\n", write_length);

        FltSendMessage(g_minifilter_handle, &g_client_port,
                &write_length, sizeof(write_length), NULL, NULL, NULL);

        if(write_length && write_length <= 2048) {
            FltSendMessage(g_minifilter_handle, &g_client_port,
                    write_buffer, write_length, NULL, NULL, NULL);
        }

        KeReleaseGuardedMutex(&comm_mutex);
    }

    return FLT_PREOP_SUCCESS_NO_CALLBACK;
}

NTSTATUS FLTAPI InstanceFilterUnloadCallback(
    _In_ FLT_FILTER_UNLOAD_FLAGS Flags)
{
    UNREFERENCED_PARAMETER(Flags);

    if(NULL != g_minifilter_handle)
    {
        if(g_server_port)
        {
            FltCloseCommunicationPort(g_server_port);
        }

        if(g_client_port)
        {
            FltCloseClientPort(g_minifilter_handle, &g_client_port);
        }

        FltUnregisterFilter(g_minifilter_handle);
    }

    return STATUS_SUCCESS;
}

NTSTATUS FLTAPI InstanceSetupCallback(
    _In_ PCFLT_RELATED_OBJECTS  FltObjects,
    _In_ FLT_INSTANCE_SETUP_FLAGS  Flags,
    _In_ DEVICE_TYPE  VolumeDeviceType,
    _In_ FLT_FILESYSTEM_TYPE  VolumeFilesystemType)
{
    UNREFERENCED_PARAMETER(FltObjects);
    UNREFERENCED_PARAMETER(Flags);
    UNREFERENCED_PARAMETER(VolumeDeviceType);
    UNREFERENCED_PARAMETER(VolumeFilesystemType);

    return STATUS_SUCCESS;
}

NTSTATUS FLTAPI InstanceQueryTeardownCallback(
    _In_ PCFLT_RELATED_OBJECTS FltObjects,
    _In_ FLT_INSTANCE_QUERY_TEARDOWN_FLAGS Flags)
{
    UNREFERENCED_PARAMETER(FltObjects);
    UNREFERENCED_PARAMETER(Flags);

    return STATUS_SUCCESS;
}

NTSTATUS ClientConnectedCallback(
    _In_ PFLT_PORT ClientPort,
    _In_ PVOID ServerPortCookie,
    _In_ PVOID ConnectionContext,
    _In_ ULONG SizeOfContext,
    _Out_ PVOID *ConnectionPortCookie)
{
    UNREFERENCED_PARAMETER(ServerPortCookie);
    UNREFERENCED_PARAMETER(ConnectionContext);
    UNREFERENCED_PARAMETER(SizeOfContext);
    UNREFERENCED_PARAMETER(ConnectionPortCookie);

    if(!InterlockedCompareExchangePointer(
            (volatile PVOID*)&g_client_port, ClientPort,
            NULL))
    {
        DbgPrint("Client connected: 0x%p\n", g_client_port);
    }
    else
    {
        DbgPrint("Client cannot connect (connection already open): 0x%p\n", ClientPort);
        FltCloseClientPort(g_minifilter_handle, &ClientPort);

        return STATUS_INSUFFICIENT_RESOURCES;
    }

    return STATUS_SUCCESS;
}

void ClientDisconnectedCallback(
    _In_ PVOID Cookie)
{
    UNREFERENCED_PARAMETER(Cookie);

    DbgPrint("Client disconnecting: 0x%p", g_client_port);

    FltCloseClientPort(g_minifilter_handle, &g_client_port);
    g_client_port = NULL;
}

NTSTATUS MessageCallback(
    _In_ PVOID PortCookie,
    _In_ PVOID InputBuffer OPTIONAL,
    _In_ ULONG InputBufferLength,
    _Out_ PVOID OutputBuffer OPTIONAL,
    _In_ ULONG OutputBufferLength,
    _Out_ PULONG ReturnOutputBufferLength)
{
    UNREFERENCED_PARAMETER(PortCookie);
    UNREFERENCED_PARAMETER(InputBuffer);
    UNREFERENCED_PARAMETER(InputBufferLength);
    UNREFERENCED_PARAMETER(OutputBuffer);
    UNREFERENCED_PARAMETER(OutputBufferLength);
    UNREFERENCED_PARAMETER(ReturnOutputBufferLength);

    return STATUS_SUCCESS;
}
