#include <windows.h>
#include <fltUser.h>
#include <stdio.h>
#include <signal.h>
#include "commondefs.h"

#pragma pack(push, 1)
typedef struct {
    FILTER_MESSAGE_HEADER header;
    ULONG buffer_length;
} LENGTH_MESSAGE;

typedef struct {
    FILTER_MESSAGE_HEADER header;
    char buffer[2048];
} BUFFER_MESSAGE;
#pragma pack(pop)

short running = 1;

void stop(int signal) {
    running = 0;
}

int main(void) {
    LENGTH_MESSAGE length_message;
    BUFFER_MESSAGE buffer_message;
    wchar_t port_name[] = COMM_PORT_NAME;
    HANDLE client_port;
    HRESULT result;
    ULONG buffer_length;

    signal(SIGINT, stop);

    result = FilterConnectCommunicationPort(port_name, 0, NULL, 0,
            NULL, &client_port);

    if(result != S_OK) {
        printf("Error connecting: %lx\n", result);
        return 1;
    }

    while(running) {
        result = FilterGetMessage(client_port,
                &length_message.header,
                sizeof(LENGTH_MESSAGE),
                NULL);

        if(result != S_OK) {
            printf("###Error getting length message: %lx\n", result);
            return 1;
        }

        printf("ReplyLength: %d, MessageId: %llu, buffer_length: %lu\n",
            length_message.header.ReplyLength,
            length_message.header.MessageId,
            length_message.buffer_length);

        buffer_length = length_message.buffer_length;

        if(buffer_length && buffer_length <= 2048) {
            result = FilterGetMessage(client_port, &buffer_message.header,
                    sizeof(BUFFER_MESSAGE),
                    NULL);

            if(result != S_OK) {
                printf("###Error getting buffer (length %d) message: %lx\n", buffer_length, result);
                return 1;
            }

            printf("Buffer: ");
            for(unsigned int i = 0; i < buffer_length; i++) {
                printf("%02x", buffer_message.buffer[i]);
            }
            printf("\n");
        }
    }

    CloseHandle(client_port);

    return 0;
}
