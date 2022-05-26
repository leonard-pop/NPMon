#include <fltKernel.h>
#include <wdm.h>
#include <ntintsafe.h>
#include <ntddk.h>
#include "commondefs.h"

FLT_POSTOP_CALLBACK_STATUS FLTAPI PostOperationCreate(
    _Inout_ PFLT_CALLBACK_DATA Data,
    _In_ PCFLT_RELATED_OBJECTS FltObjects,
    _In_ PVOID CompletionContext,
    _In_ FLT_POST_OPERATION_FLAGS Flags);

FLT_POSTOP_CALLBACK_STATUS FLTAPI PostOperationCreateNamedPipe(
    _Inout_ PFLT_CALLBACK_DATA Data,
    _In_ PCFLT_RELATED_OBJECTS FltObjects,
    _In_ PVOID CompletionContext,
    _In_ FLT_POST_OPERATION_FLAGS Flags);

FLT_PREOP_CALLBACK_STATUS FLTAPI PreOperationRead(
     _Inout_ PFLT_CALLBACK_DATA Data,
     _In_ PCFLT_RELATED_OBJECTS FltObjects,
     _Flt_CompletionContext_Outptr_ PVOID* CompletionContext);

FLT_POSTOP_CALLBACK_STATUS FLTAPI PostOperationRead(
    _Inout_ PFLT_CALLBACK_DATA Data,
    _In_ PCFLT_RELATED_OBJECTS FltObjects,
    _In_ PVOID CompletionContext,
    _In_ FLT_POST_OPERATION_FLAGS Flags);

FLT_POSTOP_CALLBACK_STATUS FLTAPI PostOperationWrite(
    _Inout_ PFLT_CALLBACK_DATA Data,
    _In_ PCFLT_RELATED_OBJECTS FltObjects,
    _In_ PVOID CompletionContext,
    _In_ FLT_POST_OPERATION_FLAGS Flags);

FLT_POSTOP_CALLBACK_STATUS SendReadMessageWhenSafe (
    _Inout_ PFLT_CALLBACK_DATA Data,
    _In_ PCFLT_RELATED_OBJECTS FltObjects,
    _In_ PVOID CompletionContext,
    _In_ FLT_POST_OPERATION_FLAGS Flags);

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
        0,
        PostOperationCreate,
    },
    {
        IRP_MJ_CREATE_NAMED_PIPE,
        0,
        0,
        PostOperationCreateNamedPipe,
    },
    {
        IRP_MJ_READ,
        0,
        PreOperationRead,
        PostOperationRead,
    },
    {
        IRP_MJ_WRITE,
        0,
        0,
        PostOperationWrite,
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
KGUARDED_MUTEX g_message_id_mutex;
ULONG g_next_message_id = 0;

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

    KeInitializeGuardedMutex(&g_message_id_mutex);

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

ULONG GetNewMessageID() {
    ULONG new_message_id;
    KeAcquireGuardedMutex(&g_message_id_mutex);

    new_message_id = g_next_message_id;
    g_next_message_id++;

    KeReleaseGuardedMutex(&g_message_id_mutex);

    return new_message_id;
}

void SendChunk(MessageChunk *chunk) {
    LARGE_INTEGER timeout;
    timeout.QuadPart = -1000000;
    FltSendMessage(g_minifilter_handle, &g_client_port, chunk,
            sizeof(MessageChunk), NULL, NULL, &timeout);
}

void AddToChunkBuffer(MessageChunk *chunk, char **buffer_end,
        char *data, long long size) {
    long long buffer_free = CHUNK_BUFFER_SIZE -
        ((*buffer_end) - chunk->buffer);

    while(buffer_free < size) {
        try {
            RtlCopyMemory(*buffer_end, data, buffer_free);
        } except(EXCEPTION_EXECUTE_HANDLER) {
            DbgPrint("###Exception writing to buffer, buffer_free###\n");
        }

        SendChunk(chunk);

        data += buffer_free;
        size -= buffer_free;
        *buffer_end = chunk->buffer;
        buffer_free = CHUNK_BUFFER_SIZE;
    }

    if(size) {
        try {
            RtlCopyMemory(*buffer_end, data, size);
        } except(EXCEPTION_EXECUTE_HANDLER) {
            DbgPrint("###Exception writing to buffer, size###\n");
        }
        *buffer_end += size;
    }
}

void SendMessageCreate(
    UNICODE_STRING file_name,
    HANDLE pid,
    NTSTATUS status)
{
    // ## REMOVE THIS ##
    /*
    if(file_name.Buffer == NULL || wcsstr(file_name.Buffer,
            L"testing") == NULL) {
        return;
    }
    */
    // #################
    if(file_name.Length >= 34) {
        file_name.Buffer += 17;
        file_name.Length -= 34;
    }

    ULONG message_tag = 'cgsM';
    char *buffer_end;
    long long buffer_free;
    MessageType type = MESSAGE_CREATE;
    MessageChunk *chunk =
        (MessageChunk*)ExAllocatePoolWithTag(
            PagedPool, sizeof(MessageChunk), message_tag);

    chunk->message_id = GetNewMessageID();
    chunk->final_chunk = 0;
    buffer_end = chunk->buffer;

    AddToChunkBuffer(chunk, &buffer_end, (char*)&type,
            sizeof(MessageType));
    AddToChunkBuffer(chunk, &buffer_end, (char*)&pid,
            sizeof(pid));
    AddToChunkBuffer(chunk, &buffer_end, (char*)&status,
            sizeof(status));
    AddToChunkBuffer(chunk, &buffer_end, (char*)&file_name.Length,
            sizeof(file_name.Length));
    AddToChunkBuffer(chunk, &buffer_end, (char*)file_name.Buffer,
            file_name.Length);

    buffer_free = CHUNK_BUFFER_SIZE -
        (buffer_end - chunk->buffer);
    if(buffer_free > 0) {
        RtlFillMemory(buffer_end, buffer_free, 0);
    }
    chunk->final_chunk = 1;
    SendChunk(chunk);

    ExFreePoolWithTag(chunk, message_tag);
}

void SendMessageCreateNamedPipe(
    UNICODE_STRING file_name,
    HANDLE pid,
    NTSTATUS status)
{
    // ## REMOVE THIS ##
    /*
    if(file_name.Buffer == NULL || wcsstr(file_name.Buffer,
            L"testing") == NULL) {
        return;
    }
    */
    // #################
    if(file_name.Length >= 34) {
        file_name.Buffer += 17;
        file_name.Length -= 34;
    }

    ULONG message_tag = 'pgsM';
    char *buffer_end;
    long long buffer_free;
    MessageType type = MESSAGE_CREATE_NAMED_PIPE;
    MessageChunk *chunk =
        (MessageChunk*)ExAllocatePoolWithTag(
            PagedPool, sizeof(MessageChunk), message_tag);

    chunk->message_id = GetNewMessageID();
    chunk->final_chunk = 0;
    buffer_end = chunk->buffer;

    AddToChunkBuffer(chunk, &buffer_end, (char*)&type,
            sizeof(MessageType));
    AddToChunkBuffer(chunk, &buffer_end, (char*)&pid,
            sizeof(pid));
    AddToChunkBuffer(chunk, &buffer_end, (char*)&status,
            sizeof(status));
    AddToChunkBuffer(chunk, &buffer_end, (char*)&file_name.Length,
            sizeof(file_name.Length));
    AddToChunkBuffer(chunk, &buffer_end, (char*)file_name.Buffer,
            file_name.Length);

    buffer_free = CHUNK_BUFFER_SIZE -
        (buffer_end - chunk->buffer);
    if(buffer_free > 0) {
        RtlFillMemory(buffer_end, buffer_free, 0);
    }
    chunk->final_chunk = 1;
    SendChunk(chunk);

    ExFreePoolWithTag(chunk, message_tag);
}

void SendMessageRead(
    UNICODE_STRING file_name,
    HANDLE pid,
    NTSTATUS status,
    ULONG read_length,
    char* read_buffer)
{
    // ## REMOVE THIS ##
    /*
    if(file_name.Buffer == NULL || wcsstr(file_name.Buffer,
            L"testing") == NULL) {
        return;
    }
    */
    // #################
    if(file_name.Length >= 34) {
        file_name.Buffer += 17;
        file_name.Length -= 34;
    }

    ULONG message_tag = 'rgsM';
    char *buffer_end;
    long long buffer_free;
    MessageType type = MESSAGE_READ;
    MessageChunk *chunk =
        (MessageChunk*)ExAllocatePoolWithTag(
            PagedPool, sizeof(MessageChunk), message_tag);

    chunk->message_id = GetNewMessageID();
    chunk->final_chunk = 0;
    buffer_end = chunk->buffer;

    AddToChunkBuffer(chunk, &buffer_end, (char*)&type,
            sizeof(MessageType));
    AddToChunkBuffer(chunk, &buffer_end, (char*)&pid,
            sizeof(pid));
    AddToChunkBuffer(chunk, &buffer_end, (char*)&status,
            sizeof(status));
    AddToChunkBuffer(chunk, &buffer_end, (char*)&file_name.Length,
            sizeof(file_name.Length));
    AddToChunkBuffer(chunk, &buffer_end, (char*)file_name.Buffer,
            file_name.Length);
    AddToChunkBuffer(chunk, &buffer_end, (char*)&read_length,
            sizeof(read_length));
    AddToChunkBuffer(chunk, &buffer_end, (char*)read_buffer,
            read_length);

    buffer_free = CHUNK_BUFFER_SIZE -
        (buffer_end - chunk->buffer);
    if(buffer_free > 0) {
        RtlFillMemory(buffer_end, buffer_free, 0);
    }
    chunk->final_chunk = 1;
    SendChunk(chunk);

    ExFreePoolWithTag(chunk, message_tag);
}

void SendMessageWrite(
    UNICODE_STRING file_name,
    HANDLE pid,
    NTSTATUS status,
    ULONG write_length,
    char* write_buffer)
{
    // ## REMOVE THIS ##
    /*
    if(file_name.Buffer == NULL || wcsstr(file_name.Buffer,
            L"testing") == NULL) {
        return;
    }
    */
    // #################
    if(file_name.Length >= 34) {
        file_name.Buffer += 17;
        file_name.Length -= 34;
    }

    ULONG message_tag = 'wgsM';
    char *buffer_end;
    long long buffer_free;
    MessageType type = MESSAGE_WRITE;
    MessageChunk *chunk =
        (MessageChunk*)ExAllocatePoolWithTag(
            PagedPool, sizeof(MessageChunk), message_tag);

    chunk->message_id = GetNewMessageID();
    chunk->final_chunk = 0;
    buffer_end = chunk->buffer;

    AddToChunkBuffer(chunk, &buffer_end, (char*)&type,
            sizeof(MessageType));
    AddToChunkBuffer(chunk, &buffer_end, (char*)&pid,
            sizeof(pid));
    AddToChunkBuffer(chunk, &buffer_end, (char*)&status,
            sizeof(status));
    AddToChunkBuffer(chunk, &buffer_end, (char*)&file_name.Length,
            sizeof(file_name.Length));
    AddToChunkBuffer(chunk, &buffer_end, (char*)file_name.Buffer,
            file_name.Length);
    AddToChunkBuffer(chunk, &buffer_end, (char*)&write_length,
            sizeof(write_length));
    AddToChunkBuffer(chunk, &buffer_end, (char*)write_buffer,
            write_length);

    buffer_free = CHUNK_BUFFER_SIZE -
        (buffer_end - chunk->buffer);
    if(buffer_free > 0) {
        RtlFillMemory(buffer_end, buffer_free, 0);
    }
    chunk->final_chunk = 1;
    SendChunk(chunk);

    ExFreePoolWithTag(chunk, message_tag);
}

FLT_POSTOP_CALLBACK_STATUS FLTAPI PostOperationCreate(
    _Inout_ PFLT_CALLBACK_DATA Data,
    _In_ PCFLT_RELATED_OBJECTS FltObjects,
    _In_ PVOID CompletionContext,
    _In_ FLT_POST_OPERATION_FLAGS Flags)
{
    UNREFERENCED_PARAMETER(CompletionContext);
    UNREFERENCED_PARAMETER(Flags);
    // ## REMOVE THIS ##
    /*
    if(FltObjects->FileObject->FileName.Buffer == NULL ||
            wcsstr(FltObjects->FileObject->FileName.Buffer,
            L"testing") == NULL) {
        return FLT_POSTOP_FINISHED_PROCESSING;
    }
    */
    // #################

    if(FltObjects->FileObject->DeviceObject->DeviceType !=
            FILE_DEVICE_NAMED_PIPE) {
        return FLT_POSTOP_FINISHED_PROCESSING;
    }

    HANDLE pid = FltGetRequestorProcessId(Data);
    FLT_FILE_NAME_INFORMATION *file_name_information = NULL;
    NTSTATUS status;

    status = FltGetFileNameInformation(Data,
            FLT_FILE_NAME_OPENED | FLT_FILE_NAME_QUERY_ALWAYS_ALLOW_CACHE_LOOKUP,
            &file_name_information);

    if(!NT_SUCCESS(status)) {
        DbgPrint("Failed getting file name with error: %x\n", status);
        return FLT_POSTOP_FINISHED_PROCESSING;
    }

    if(file_name_information->Name.Length == 34) {
        return FLT_POSTOP_FINISHED_PROCESSING;
    }

    DbgPrint("Create captured: %wZ, status: %x, pid %lu\n",
            file_name_information->Name,
            Data->IoStatus.Status,
            pid);

    if(g_client_port) {
        SendMessageCreate(file_name_information->Name,
                pid,
                Data->IoStatus.Status);
    }

    return FLT_POSTOP_FINISHED_PROCESSING;
}

FLT_POSTOP_CALLBACK_STATUS FLTAPI PostOperationCreateNamedPipe(
    _Inout_ PFLT_CALLBACK_DATA Data,
    _In_ PCFLT_RELATED_OBJECTS FltObjects,
    _In_ PVOID CompletionContext,
    _In_ FLT_POST_OPERATION_FLAGS Flags)
{
    UNREFERENCED_PARAMETER(CompletionContext);
    UNREFERENCED_PARAMETER(Flags);
    // ## REMOVE THIS ##
    /*
    if(FltObjects->FileObject->FileName.Buffer == NULL ||
            wcsstr(FltObjects->FileObject->FileName.Buffer,
            L"testing") == NULL) {
        return FLT_POSTOP_FINISHED_PROCESSING;
    }
    */
    // #################

    HANDLE pid = FltGetRequestorProcessId(Data);
    FLT_FILE_NAME_INFORMATION *file_name_information = NULL;
    NTSTATUS status;

    status = FltGetFileNameInformation(Data,
            FLT_FILE_NAME_OPENED | FLT_FILE_NAME_QUERY_ALWAYS_ALLOW_CACHE_LOOKUP,
            &file_name_information);

    if(!NT_SUCCESS(status)) {
        DbgPrint("Failed getting file name with error: %x\n", status);
        return FLT_POSTOP_FINISHED_PROCESSING;
    }

    if(file_name_information->Name.Length == 34) {
        return FLT_POSTOP_FINISHED_PROCESSING;
    }

    DbgPrint("Create namd pipe captured: %wZ, status: %x, pid %lu\n",
            file_name_information->Name,
            Data->IoStatus.Status,
            pid);

    if(g_client_port) {
        SendMessageCreateNamedPipe(file_name_information->Name,
                pid,
                Data->IoStatus.Status);
    }

    return FLT_POSTOP_FINISHED_PROCESSING;
}

FLT_PREOP_CALLBACK_STATUS FLTAPI PreOperationRead(
     _Inout_ PFLT_CALLBACK_DATA Data,
     _In_ PCFLT_RELATED_OBJECTS FltObjects,
     _Flt_CompletionContext_Outptr_ PVOID* CompletionContext)
{
    UNREFERENCED_PARAMETER(CompletionContext);
    // ## REMOVE THIS ##
    /*
    if(FltObjects->FileObject->FileName.Buffer == NULL ||
            wcsstr(FltObjects->FileObject->FileName.Buffer,
            L"testing") == NULL) {
        return FLT_POSTOP_FINISHED_PROCESSING;
    }
    */
    // #################

    if(FltObjects->FileObject->DeviceObject->DeviceType !=
            FILE_DEVICE_NAMED_PIPE) {
        return FLT_POSTOP_FINISHED_PROCESSING;
    }

    /*
    ULONG read_length_requested = Data->Iopb->Parameters.Read.Length,
        read_length_got = Data->IoStatus.Information;
    char* read_buffer = (char*)Data->Iopb->Parameters.Read.ReadBuffer;
    HANDLE pid = FltGetRequestorProcessId(Data);
    FLT_FILE_NAME_INFORMATION *file_name_information;
    NTSTATUS status;

    status = FltGetFileNameInformation(Data,
            FLT_FILE_NAME_NORMALIZED,
            &file_name_information);

    if(!NT_SUCCESS(status)) {
        DbgPrint("Failed getting file name during preop read with error: %x\n", status);
        return FLT_POSTOP_FINISHED_PROCESSING;
    }

    status = FltParseFileNameInformation(file_name_information);

    DbgPrint("Preop Named pipe read captured: %wZ, status: %x, pid %lu, requested: %lu, got: %u"
            ", query name: %wZ, query name size: %u, parsed: final-%x extension-%x stream-%x parent-%x\n",
            FltObjects->FileObject->FileName,
            Data->IoStatus.Status,
            pid,
            read_length_requested,
            read_length_got,
            file_name_information->Name,
            file_name_information->Name.Length,
            file_name_information->NamesParsed & FLTFL_FILE_NAME_PARSED_FINAL_COMPONENT,
            file_name_information->NamesParsed & FLTFL_FILE_NAME_PARSED_EXTENSION,
            file_name_information->NamesParsed & FLTFL_FILE_NAME_PARSED_STREAM,
            file_name_information->NamesParsed & FLTFL_FILE_NAME_PARSED_PARENT_DIR);
    */

    /*
    if(g_client_port) {
        SendMessageRead(FltObjects->FileObject->FileName,
                pid,
                Data->IoStatus.Status,
                read_length_got,
                read_buffer);
    }
    */

    return FLT_PREOP_SUCCESS_WITH_CALLBACK;
}

FLT_POSTOP_CALLBACK_STATUS FLTAPI PostOperationRead(
    _Inout_ PFLT_CALLBACK_DATA Data,
    _In_ PCFLT_RELATED_OBJECTS FltObjects,
    _In_ PVOID CompletionContext,
    _In_ FLT_POST_OPERATION_FLAGS Flags)
{
    UNREFERENCED_PARAMETER(CompletionContext);
    UNREFERENCED_PARAMETER(Flags);
    // ## REMOVE THIS ##
    /*
    if(FltObjects->FileObject->FileName.Buffer == NULL ||
            wcsstr(FltObjects->FileObject->FileName.Buffer,
            L"testing") == NULL) {
        return FLT_POSTOP_FINISHED_PROCESSING;
    }
    */
    // #################

    if(FltObjects->FileObject->DeviceObject->DeviceType !=
            FILE_DEVICE_NAMED_PIPE) {
        return FLT_POSTOP_FINISHED_PROCESSING;
    }

    ULONG read_length_requested = Data->Iopb->Parameters.Read.Length,
        read_length_got = Data->IoStatus.Information;
    //char* read_buffer = (char*)Data->Iopb->Parameters.Read.ReadBuffer;
    HANDLE pid = FltGetRequestorProcessId(Data);
    FLT_FILE_NAME_INFORMATION *file_name_information = NULL;
    NTSTATUS status;

    status = FltGetFileNameInformation(Data,
            FLT_FILE_NAME_OPENED | FLT_FILE_NAME_QUERY_ALWAYS_ALLOW_CACHE_LOOKUP,
            &file_name_information);

    if(!NT_SUCCESS(status)) {
        DbgPrint("Failed getting file name with error: %x\n", status);
        return FLT_POSTOP_FINISHED_PROCESSING;
    }

    if(file_name_information->Name.Length == 34) {
        return FLT_POSTOP_FINISHED_PROCESSING;
    }

    DbgPrint("Postop Named pipe read captured: %wZ, status: %x, pid %lu, requested: %lu, got: %u, name size %u\n",
            //FltObjects->FileObject->FileName,
            file_name_information->Name,
            Data->IoStatus.Status,
            pid,
            read_length_requested,
            read_length_got,
            file_name_information->Name.Length);

    FLT_POSTOP_CALLBACK_STATUS retValue = FLT_POSTOP_FINISHED_PROCESSING;

    if(g_client_port) {
        SendMessageRead(file_name_information->Name,
                pid,
                Data->IoStatus.Status,
                read_length_got,
                NULL);

        /*
        if(!FltDoCompletionProcessingWhenSafe(Data,
                FltObjects,
                CompletionContext,
                Flags,
                SendReadMessageWhenSafe,
                &retValue)) {
            DbgPrint("Failed to do completion processing when safe\n");
        }
        */
    }

    return retValue;
}

FLT_POSTOP_CALLBACK_STATUS SendReadMessageWhenSafe (
    _Inout_ PFLT_CALLBACK_DATA Data,
    _In_ PCFLT_RELATED_OBJECTS FltObjects,
    _In_ PVOID CompletionContext,
    _In_ FLT_POST_OPERATION_FLAGS Flags) {
    UNREFERENCED_PARAMETER(CompletionContext);
    UNREFERENCED_PARAMETER(Flags);

    NTSTATUS status = FltLockUserBuffer( Data );
    ULONG read_length_got = Data->IoStatus.Information;
    char* read_buffer = (char*)Data->Iopb->Parameters.Read.ReadBuffer;
    HANDLE pid = FltGetRequestorProcessId(Data);

    if(!NT_SUCCESS(status)) {
        DbgPrint("Failed to lock user buffer\n");
        return FLT_POSTOP_FINISHED_PROCESSING;
    }

    read_buffer = (char*) MmGetSystemAddressForMdlSafe(Data->Iopb->Parameters.Read.MdlAddress,
        NormalPagePriority | MdlMappingNoExecute );

    if(read_buffer == NULL) {
        DbgPrint("Failed to get system address for mdl\n");
        return FLT_POSTOP_FINISHED_PROCESSING;
    }

    SendMessageRead(FltObjects->FileObject->FileName,
            pid,
            Data->IoStatus.Status,
            read_length_got,
            read_buffer);

    return FLT_POSTOP_FINISHED_PROCESSING;
}

FLT_POSTOP_CALLBACK_STATUS FLTAPI PostOperationWrite(
    _Inout_ PFLT_CALLBACK_DATA Data,
    _In_ PCFLT_RELATED_OBJECTS FltObjects,
    _In_ PVOID CompletionContext,
    _In_ FLT_POST_OPERATION_FLAGS Flags)
{
    UNREFERENCED_PARAMETER(CompletionContext);
    UNREFERENCED_PARAMETER(Flags);
    // ## REMOVE THIS ##
    /*
    if(FltObjects->FileObject->FileName.Buffer == NULL ||
            wcsstr(FltObjects->FileObject->FileName.Buffer,
            L"testing") == NULL) {
        return FLT_POSTOP_FINISHED_PROCESSING;
    }
    */
    // #################

    if(FltObjects->FileObject->DeviceObject->DeviceType !=
            FILE_DEVICE_NAMED_PIPE) {
        return FLT_POSTOP_FINISHED_PROCESSING;
    }

    ULONG write_length_requested = Data->Iopb->Parameters.Write.Length,
        write_length_got = Data->IoStatus.Information;
    char* write_buffer = (char*)Data->Iopb->Parameters.Write.WriteBuffer;
    HANDLE pid = FltGetRequestorProcessId(Data);
    FLT_FILE_NAME_INFORMATION *file_name_information;
    NTSTATUS status;

    status = FltGetFileNameInformation(Data,
            FLT_FILE_NAME_OPENED | FLT_FILE_NAME_QUERY_ALWAYS_ALLOW_CACHE_LOOKUP,
            &file_name_information);

    if(!NT_SUCCESS(status)) {
        DbgPrint("Failed getting file name with error: %x\n", status);
        return FLT_POSTOP_FINISHED_PROCESSING;
    }

    if(file_name_information->Name.Length == 34) {
        return FLT_POSTOP_FINISHED_PROCESSING;
    }

    DbgPrint("Named pipe write captured: %wZ, status: %x, pid %lu, requested: %lu, got: %lu, name size: %u\n",
            //FltObjects->FileObject->FileName,
            file_name_information->Name,
            Data->IoStatus.Status,
            pid,
            write_length_requested,
            write_length_got,
            file_name_information->Name.Length);

    if(g_client_port) {
        SendMessageWrite(file_name_information->Name,
                pid,
                Data->IoStatus.Status,
                write_length_got,
                write_buffer);
    }

    return FLT_POSTOP_FINISHED_PROCESSING;
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

    DbgPrint("Client disconnecting: 0x%p\n", g_client_port);

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
