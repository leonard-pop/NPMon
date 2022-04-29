#define COMM_PORT_NAME L"\\npmon_comm"
#define CHUNK_BUFFER_SIZE 2048

typedef enum _MessageType {
    MESSAGE_CREATE, MESSAGE_CREATE_NAMED_PIPE, MESSAGE_READ,
    MESSAGE_WRITE
} MessageType;

#pragma pack(push, 1)
typedef struct _Chunk {
    DWORD message_id;
    char final_chunk;
    unsigned char buffer[CHUNK_BUFFER_SIZE];
} MessageChunk;
#pragma pack(pop)
