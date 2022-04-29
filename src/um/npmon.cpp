#include <windows.h>
#include <fltUser.h>
#include <stdio.h>
#include <signal.h>
#include <stdexcept>
#include <vector>
#include <map>
#include "commondefs.h"

typedef enum _OperationType {
    OPERATION_CREATE, OPERATION_READ, OPERATION_WRITE,
    OPERATION_INVALID
} OperationType;

#pragma pack(push, 1)
typedef struct {
    FILTER_MESSAGE_HEADER header;
    MessageChunk chunk;
} Message;
#pragma pack(pop)

typedef struct {
    HANDLE pid;
    NTSTATUS status;
    USHORT pipe_name_size;
    wchar_t *pipe_name;
} CreateOperation;

typedef struct {
    HANDLE pid;
    NTSTATUS status;
    USHORT pipe_name_size;
    wchar_t *pipe_name;
    ULONG buffer_size;
    unsigned char *buffer;
} ReadOperation;

typedef struct {
    HANDLE pid;
    NTSTATUS status;
    USHORT pipe_name_size;
    wchar_t *pipe_name;
    ULONG buffer_size;
    unsigned char *buffer;
} WriteOperation;

typedef struct {
    OperationType type;
    union {
        CreateOperation *create_operation;
        ReadOperation *read_operation;
        WriteOperation *write_operation;
    } details;
} Operation;

volatile sig_atomic_t running = 1;

void stop(int sig) {
    running = 0;
    signal(sig, SIG_IGN);
}

void extractFromBufferChunks(unsigned char* destination, std::vector<MessageChunk> &chunks,
        size_t size, unsigned int &chunk_index, unsigned char **buffer_pos) {
    size_t buffer_remaining = CHUNK_BUFFER_SIZE -
        ((*buffer_pos) - chunks[chunk_index].buffer);

    while(buffer_remaining < size) {
        memcpy(destination, *buffer_pos, buffer_remaining);

        chunk_index++;
        if(chunk_index >= chunks.size()) {
            throw std::runtime_error("Message not terminated");
        }

        destination += buffer_remaining;
        size -= buffer_remaining;
        *buffer_pos = chunks[chunk_index].buffer;
        buffer_remaining = CHUNK_BUFFER_SIZE;
    }

    if(size) {
        memcpy(destination, *buffer_pos, size);
        *buffer_pos += size;
    }
}

WriteOperation* parseWriteOperation(std::vector<MessageChunk> &chunks) {
    WriteOperation *write_operation = new WriteOperation;
    unsigned int chunk_index = 0;
    unsigned char *buffer_pos;
    buffer_pos = chunks[chunk_index].buffer + sizeof(MessageType);

    write_operation->pipe_name = nullptr;
    write_operation->buffer = nullptr;

    try {
        extractFromBufferChunks((unsigned char*)&write_operation->pid, chunks,
                sizeof(write_operation->pid), chunk_index, &buffer_pos);
        extractFromBufferChunks((unsigned char*)&write_operation->status, chunks,
                sizeof(write_operation->status), chunk_index, &buffer_pos);

        extractFromBufferChunks((unsigned char*)&write_operation->pipe_name_size,
                chunks, sizeof(write_operation->pipe_name_size),
                chunk_index, &buffer_pos);
        write_operation->pipe_name = new wchar_t[write_operation->pipe_name_size + 1];
        extractFromBufferChunks((unsigned char*)write_operation->pipe_name,
                chunks, write_operation->pipe_name_size,
                chunk_index, &buffer_pos);
        write_operation->pipe_name[write_operation->pipe_name_size - 1] = L'\0';

        extractFromBufferChunks((unsigned char*)&write_operation->buffer_size,
                chunks, sizeof(write_operation->buffer_size),
                chunk_index, &buffer_pos);
        write_operation->buffer = new unsigned char[write_operation->buffer_size];
        extractFromBufferChunks((unsigned char*)write_operation->buffer,
                chunks, write_operation->buffer_size,
                chunk_index, &buffer_pos);
    } catch(std::exception) {
        if(write_operation->pipe_name != nullptr) {
            delete write_operation->pipe_name;
        }
        if(write_operation->buffer != nullptr) {
            delete write_operation->buffer;
        }

        return nullptr;
    }

    return write_operation;
}

Operation parseMessageChunks(std::vector<MessageChunk> &chunks) {
    Operation operation;

    switch(*(MessageType*)chunks[0].buffer) {
        case MESSAGE_WRITE:
            operation.type = OPERATION_WRITE;
            operation.details.write_operation =
                parseWriteOperation(chunks);
            if(operation.details.write_operation == nullptr) {
                printf("[!!!] Invalid message\n");
                operation.type = OPERATION_INVALID;
            }
            break;
        default:
            operation.type = OPERATION_INVALID;
    }

    return operation;
}

void printWriteOperation(WriteOperation *write_operation) {
    printf("Write operation, status: %x, pipe name size: %u, pipe name: %ls, buffer size: %u, buffer: ",
            write_operation->status,
            write_operation->pipe_name_size,
            write_operation->pipe_name,
            write_operation->buffer_size);

    for(unsigned int i = 0; i < write_operation->buffer_size; i++) {
        printf("%02X", write_operation->buffer[i]);
    }

    printf("\n");
}

void printOperation(const Operation &operation) {
    switch(operation.type) {
        case OPERATION_WRITE:
            printWriteOperation(operation.details.write_operation);
            break;
        case OPERATION_INVALID:
            printf("Invalid operation\n");
        default:
            printf("Placeholder\n");
    }
}

void cleanup(std::vector<Operation> &operations) {
    for(Operation operation: operations) {
        switch(operation.type) {
            case OPERATION_CREATE:
                delete operation.details.create_operation->pipe_name;
                delete operation.details.create_operation;
                break;
            case OPERATION_READ:
                delete operation.details.read_operation->pipe_name;
                delete operation.details.read_operation->buffer;
                delete operation.details.read_operation;
                break;
            case OPERATION_WRITE:
                delete operation.details.write_operation->pipe_name;
                delete operation.details.write_operation->buffer;
                delete operation.details.write_operation;
                break;
        }
    }
}

int main(void) {
    Message message;
    wchar_t port_name[] = COMM_PORT_NAME;
    HANDLE client_port;
    HRESULT result;
    std::vector<Operation> operations;
    std::map< unsigned int, std::vector<MessageChunk> > chunk_map;

    signal(SIGINT, stop);

    printf("[*] Connecting to communication port\n");
    result = FilterConnectCommunicationPort(port_name, 0, NULL, 0,
            NULL, &client_port);

    if(result != S_OK) {
        printf("[X] Error connecting: %lx\n", result);
        return 1;
    }

    printf("[*] Success\n");

    while(running) {
        result = FilterGetMessage(client_port,
                &message.header,
                sizeof(Message),
                NULL);

        if(result != S_OK) {
            printf("[!!!] Error getting message: %lx\n", result);
            return 1;
        }

        chunk_map[message.chunk.message_id].push_back(message.chunk);
        printf("[*] Got chunk\n");

        if(message.chunk.final_chunk) {
            printf("[*] Parsing chunks\n");
            operations.push_back(parseMessageChunks(chunk_map[message.chunk.message_id]));
            chunk_map.erase(message.chunk.message_id);

            printOperation(operations[operations.size() - 1]);
        }
    }

    printf("[*] Closig communicaion port\n");
    CloseHandle(client_port);

    printf("[*] Cleanup\n");
    cleanup(operations);

    printf("[*] Exitting\n");
    return 0;
}
