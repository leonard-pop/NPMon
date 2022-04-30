#include <windows.h>
#include <fltUser.h>
#include <stdio.h>
#include <signal.h>
#include <stdexcept>
#include <vector>
#include <map>
#include "commondefs.h"

typedef enum _OperationType {
    OPERATION_CREATE, OPERATION_CREATE_NAMED_PIPE, OPERATION_READ,
    OPERATION_WRITE, OPERATION_INVALID
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
} CreateNamedPipeOperation;

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
        CreateNamedPipeOperation *create_np_operation;
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

CreateOperation* parseCreateOperation(std::vector<MessageChunk> &chunks) {
    CreateOperation *create_operation = new CreateOperation;
    unsigned int chunk_index = 0;
    unsigned char *buffer_pos;
    buffer_pos = chunks[chunk_index].buffer + sizeof(MessageType);

    create_operation->pipe_name = nullptr;

    try {
        extractFromBufferChunks((unsigned char*)&create_operation->pid, chunks,
                sizeof(create_operation->pid), chunk_index, &buffer_pos);
        extractFromBufferChunks((unsigned char*)&create_operation->status, chunks,
                sizeof(create_operation->status), chunk_index, &buffer_pos);

        extractFromBufferChunks((unsigned char*)&create_operation->pipe_name_size,
                chunks, sizeof(create_operation->pipe_name_size),
                chunk_index, &buffer_pos);
        create_operation->pipe_name = new wchar_t[create_operation->pipe_name_size + 1];
        extractFromBufferChunks((unsigned char*)create_operation->pipe_name,
                chunks, create_operation->pipe_name_size,
                chunk_index, &buffer_pos);
        create_operation->pipe_name[create_operation->pipe_name_size - 1] = L'\0';
    } catch(std::exception) {
        if(create_operation->pipe_name != nullptr) {
            delete create_operation->pipe_name;
        }

        return nullptr;
    }

    return create_operation;
}

CreateNamedPipeOperation* parseCreateNamedPipeOperation(std::vector<MessageChunk> &chunks) {
    CreateNamedPipeOperation *create_np_operation = new CreateNamedPipeOperation;
    unsigned int chunk_index = 0;
    unsigned char *buffer_pos;
    buffer_pos = chunks[chunk_index].buffer + sizeof(MessageType);

    create_np_operation->pipe_name = nullptr;

    try {
        extractFromBufferChunks((unsigned char*)&create_np_operation->pid, chunks,
                sizeof(create_np_operation->pid), chunk_index, &buffer_pos);
        extractFromBufferChunks((unsigned char*)&create_np_operation->status, chunks,
                sizeof(create_np_operation->status), chunk_index, &buffer_pos);

        extractFromBufferChunks((unsigned char*)&create_np_operation->pipe_name_size,
                chunks, sizeof(create_np_operation->pipe_name_size),
                chunk_index, &buffer_pos);
        create_np_operation->pipe_name = new wchar_t[create_np_operation->pipe_name_size + 1];
        extractFromBufferChunks((unsigned char*)create_np_operation->pipe_name,
                chunks, create_np_operation->pipe_name_size,
                chunk_index, &buffer_pos);
        create_np_operation->pipe_name[create_np_operation->pipe_name_size - 1] = L'\0';
    } catch(std::exception) {
        if(create_np_operation->pipe_name != nullptr) {
            delete create_np_operation->pipe_name;
        }

        return nullptr;
    }

    return create_np_operation;
}

ReadOperation* parseReadOperation(std::vector<MessageChunk> &chunks) {
    ReadOperation *read_operation = new ReadOperation;
    unsigned int chunk_index = 0;
    unsigned char *buffer_pos;
    buffer_pos = chunks[chunk_index].buffer + sizeof(MessageType);

    read_operation->pipe_name = nullptr;
    read_operation->buffer = nullptr;

    try {
        extractFromBufferChunks((unsigned char*)&read_operation->pid, chunks,
                sizeof(read_operation->pid), chunk_index, &buffer_pos);
        extractFromBufferChunks((unsigned char*)&read_operation->status, chunks,
                sizeof(read_operation->status), chunk_index, &buffer_pos);

        extractFromBufferChunks((unsigned char*)&read_operation->pipe_name_size,
                chunks, sizeof(read_operation->pipe_name_size),
                chunk_index, &buffer_pos);
        read_operation->pipe_name = new wchar_t[read_operation->pipe_name_size + 1];
        extractFromBufferChunks((unsigned char*)read_operation->pipe_name,
                chunks, read_operation->pipe_name_size,
                chunk_index, &buffer_pos);
        read_operation->pipe_name[read_operation->pipe_name_size - 1] = L'\0';

        extractFromBufferChunks((unsigned char*)&read_operation->buffer_size,
                chunks, sizeof(read_operation->buffer_size),
                chunk_index, &buffer_pos);
        read_operation->buffer = new unsigned char[read_operation->buffer_size];
        extractFromBufferChunks((unsigned char*)read_operation->buffer,
                chunks, read_operation->buffer_size,
                chunk_index, &buffer_pos);
    } catch(std::exception) {
        if(read_operation->pipe_name != nullptr) {
            delete read_operation->pipe_name;
        }
        if(read_operation->buffer != nullptr) {
            delete read_operation->buffer;
        }

        return nullptr;
    }

    return read_operation;
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
        case MESSAGE_CREATE:
            operation.type = OPERATION_CREATE;
            operation.details.create_operation =
                parseCreateOperation(chunks);

            if(operation.details.create_operation == nullptr) {
                printf("[!!!] Invalid message\n");
                operation.type = OPERATION_INVALID;
            }

            break;
        case MESSAGE_CREATE_NAMED_PIPE:
            operation.type = OPERATION_CREATE_NAMED_PIPE;
            operation.details.create_np_operation =
                parseCreateNamedPipeOperation(chunks);

            if(operation.details.create_np_operation == nullptr) {
                printf("[!!!] Invalid message\n");
                operation.type = OPERATION_INVALID;
            }

            break;
        case MESSAGE_READ:
            operation.type = OPERATION_READ;
            operation.details.read_operation =
                parseReadOperation(chunks);

            if(operation.details.read_operation == nullptr) {
                printf("[!!!] Invalid message\n");
                operation.type = OPERATION_INVALID;
            }

            break;
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

void printCreateOperation(CreateOperation *create_operation) {
    printf("Create operation, pid: %u, status: %x, "
            "pipe name size: %u, pipe name: %ls\n",
            create_operation->pid,
            create_operation->status,
            create_operation->pipe_name_size,
            create_operation->pipe_name);
}

void printCreateNamedPipeOperation(CreateNamedPipeOperation *create_np_operation) {
    printf("Create named pipe operation, pid: %u, status: %x, "
            "pipe name size: %u, pipe name: %ls\n",
            create_np_operation->pid,
            create_np_operation->status,
            create_np_operation->pipe_name_size,
            create_np_operation->pipe_name);
}

void printReadOperation(ReadOperation *read_operation) {
    printf("Read operation, pid: %u, status: %x, "
            "pipe name size: %u, pipe name: %ls, "
            "buffer size: %u, buffer: ",
            read_operation->pid,
            read_operation->status,
            read_operation->pipe_name_size,
            read_operation->pipe_name,
            read_operation->buffer_size);

    for(unsigned int i = 0; i < read_operation->buffer_size; i++) {
        printf("%02X", read_operation->buffer[i]);
    }

    printf("\n");
}

void printWriteOperation(WriteOperation *write_operation) {
    printf("Write operation, pid: %u, status: %x, "
            "pipe name size: %u, pipe name: %ls, "
            "buffer size: %u, buffer: ",
            write_operation->pid,
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
        case OPERATION_CREATE:
            printCreateOperation(operation.details.create_operation);
            break;
        case OPERATION_CREATE_NAMED_PIPE:
            printCreateNamedPipeOperation(operation.details.create_np_operation);
            break;
        case OPERATION_READ:
            printReadOperation(operation.details.read_operation);
            break;
        case OPERATION_WRITE:
            printWriteOperation(operation.details.write_operation);
            break;
        case OPERATION_INVALID:
            printf("Invalid operation\n");
    }
}

void cleanup(std::vector<Operation> &operations) {
    for(Operation operation: operations) {
        switch(operation.type) {
            case OPERATION_CREATE_NAMED_PIPE:
                delete operation.details.create_np_operation->pipe_name;
                delete operation.details.create_np_operation;
                break;
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
            case OPERATION_INVALID:
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
