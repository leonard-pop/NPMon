#include <windows.h>
#include <fltUser.h>
#include <stdio.h>
#include <signal.h>
#include <stdexcept>
#include <vector>
#include <map>
#include <thread>

#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx10.h"
#include <d3d10_1.h>
#include <d3d10.h>
#include <tchar.h>

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
std::vector<Operation> g_operations;

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

void cleanup() {
    for(Operation operation: g_operations) {
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

void handleCommunication() {
    Message message;
    wchar_t port_name[] = COMM_PORT_NAME;
    HANDLE client_port;
    HRESULT result;
    std::map< unsigned int, std::vector<MessageChunk> > chunk_map;

    printf("[*] Connecting to communication port\n");
    result = FilterConnectCommunicationPort(port_name, 0, NULL, 0,
            NULL, &client_port);

    if(result != S_OK) {
        printf("[X] Error connecting: %lx\n", result);
        return;
    }

    printf("[*] Success\n");

    while(running) {
        result = FilterGetMessage(client_port,
                &message.header,
                sizeof(Message),
                NULL);

        if(result != S_OK) {
            printf("[!!!] Error getting message: %lx\n", result);
            running = 0;
        } else {
            chunk_map[message.chunk.message_id].push_back(message.chunk);
            printf("[*] Got chunk\n");

            if(message.chunk.final_chunk) {
                printf("[*] Parsing chunks\n");
                g_operations.push_back(parseMessageChunks(chunk_map[message.chunk.message_id]));
                chunk_map.erase(message.chunk.message_id);

                printOperation(g_operations[g_operations.size() - 1]);
            }
        }
    }

    printf("[*] Closig communicaion port\n");
    CloseHandle(client_port);
}

// Data
static ID3D10Device*            g_pd3dDevice = NULL;
static IDXGISwapChain*          g_pSwapChain = NULL;
static ID3D10RenderTargetView*  g_mainRenderTargetView = NULL;

// Forward declarations of helper functions
bool CreateDeviceD3D(HWND hWnd);
void CleanupDeviceD3D();
void CreateRenderTarget();
void CleanupRenderTarget();
LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

void renderGUI() {
    // Create application window
    ImGui_ImplWin32_EnableDpiAwareness();
    WNDCLASSEX wc = { sizeof(WNDCLASSEX), CS_CLASSDC, WndProc, 0L, 0L, GetModuleHandle(NULL), NULL, NULL, NULL, NULL, _T("ImGui Example"), NULL };
    ::RegisterClassEx(&wc);
    HWND hwnd = ::CreateWindow(wc.lpszClassName, _T("Dear ImGui DirectX10 Example"), WS_OVERLAPPEDWINDOW, 100, 100, 1280, 800, NULL, NULL, wc.hInstance, NULL);

    // Initialize Direct3D
    if (!CreateDeviceD3D(hwnd))
    {
        CleanupDeviceD3D();
        ::UnregisterClass(wc.lpszClassName, wc.hInstance);
        fprintf(stderr, "Error initializing Direct3D\n");
        return;
    }

    // Show the window
    ::ShowWindow(hwnd, SW_SHOWDEFAULT);
    ::UpdateWindow(hwnd);

    // Setup Dear ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls

    // Setup Dear ImGui style
    ImGui::StyleColorsDark();
    //ImGui::StyleColorsClassic();

    // Setup Platform/Renderer backends
    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX10_Init(g_pd3dDevice);

    // Our state
    bool show_another_window = false;
    ImVec4 clear_color = ImVec4(0.45f, 0.55f, 0.60f, 1.00f);

    // Main loop
    bool done = false;
    while (!done)
    {
        MSG msg;
        while (::PeekMessage(&msg, NULL, 0U, 0U, PM_REMOVE))
        {
            ::TranslateMessage(&msg);
            ::DispatchMessage(&msg);
            if (msg.message == WM_QUIT)
                done = true;
        }
        if (done)
            break;

        ImGui_ImplDX10_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        {
            static float f = 0.0f;
            static int counter = 0;

            ImGui::Begin("Hello, world!");                          // Create a window called "Hello, world!" and append into it.

            ImGui::Text("This is some useful text.");               // Display some text (you can use a format strings too)
            ImGui::Checkbox("Another Window", &show_another_window);

            ImGui::SliderFloat("float", &f, 0.0f, 1.0f);            // Edit 1 float using a slider from 0.0f to 1.0f
            ImGui::ColorEdit3("clear color", (float*)&clear_color); // Edit 3 floats representing a color

            if (ImGui::Button("Button"))                            // Buttons return true when clicked (most widgets return true when edited/activated)
                counter++;
            ImGui::SameLine();
            ImGui::Text("counter = %d", counter);

            ImGui::Text("Application average %.3f ms/frame (%.1f FPS)", 1000.0f / ImGui::GetIO().Framerate, ImGui::GetIO().Framerate);
            ImGui::End();
        }

        // 3. Show another simple window.
        if (show_another_window)
        {
            ImGui::Begin("Another Window", &show_another_window);   // Pass a pointer to our bool variable (the window will have a closing button that will clear the bool when clicked)
            ImGui::Text("Hello from another window!");
            if (ImGui::Button("Close Me"))
                show_another_window = false;
            ImGui::End();
        }

        // Rendering
        ImGui::Render();
        const float clear_color_with_alpha[4] = { clear_color.x * clear_color.w, clear_color.y * clear_color.w, clear_color.z * clear_color.w, clear_color.w };
        g_pd3dDevice->OMSetRenderTargets(1, &g_mainRenderTargetView, NULL);
        g_pd3dDevice->ClearRenderTargetView(g_mainRenderTargetView, clear_color_with_alpha);
        ImGui_ImplDX10_RenderDrawData(ImGui::GetDrawData());

        g_pSwapChain->Present(1, 0); // Present with vsync
        //g_pSwapChain->Present(0, 0); // Present without vsync
    }

    ImGui_ImplDX10_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();

    CleanupDeviceD3D();
    ::DestroyWindow(hwnd);
    ::UnregisterClass(wc.lpszClassName, wc.hInstance);
}

// Helper functions

bool CreateDeviceD3D(HWND hWnd)
{
    // Setup swap chain
    DXGI_SWAP_CHAIN_DESC sd;
    ZeroMemory(&sd, sizeof(sd));
    sd.BufferCount = 2;
    sd.BufferDesc.Width = 0;
    sd.BufferDesc.Height = 0;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hWnd;
    sd.SampleDesc.Count = 1;
    sd.SampleDesc.Quality = 0;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    UINT createDeviceFlags = 0;
    //createDeviceFlags |= D3D10_CREATE_DEVICE_DEBUG;
    HRESULT result = D3D10CreateDeviceAndSwapChain(NULL, D3D10_DRIVER_TYPE_WARP, NULL, createDeviceFlags, D3D10_SDK_VERSION, &sd, &g_pSwapChain, &g_pd3dDevice);
    if(result != S_OK) {
        fprintf(stderr, "Error: %x\n", result);
        return false;
    }

    CreateRenderTarget();
    return true;
}

void CleanupDeviceD3D()
{
    CleanupRenderTarget();
    if (g_pSwapChain) { g_pSwapChain->Release(); g_pSwapChain = NULL; }
    if (g_pd3dDevice) { g_pd3dDevice->Release(); g_pd3dDevice = NULL; }
}

void CreateRenderTarget()
{
    ID3D10Texture2D* pBackBuffer;
    g_pSwapChain->GetBuffer(0, IID_PPV_ARGS(&pBackBuffer));
    g_pd3dDevice->CreateRenderTargetView(pBackBuffer, NULL, &g_mainRenderTargetView);
    pBackBuffer->Release();
}

void CleanupRenderTarget()
{
    if (g_mainRenderTargetView) { g_mainRenderTargetView->Release(); g_mainRenderTargetView = NULL; }
}

// Forward declare message handler from imgui_impl_win32.cpp
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

// Win32 message handler
LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
        return true;

    switch (msg)
    {
    case WM_SIZE:
        if (g_pd3dDevice != NULL && wParam != SIZE_MINIMIZED)
        {
            CleanupRenderTarget();
            g_pSwapChain->ResizeBuffers(0, (UINT)LOWORD(lParam), (UINT)HIWORD(lParam), DXGI_FORMAT_UNKNOWN, 0);
            CreateRenderTarget();
        }
        return 0;
    case WM_SYSCOMMAND:
        if ((wParam & 0xfff0) == SC_KEYMENU) // Disable ALT application menu
            return 0;
        break;
    case WM_DESTROY:
        ::PostQuitMessage(0);
        return 0;
    }
    return ::DefWindowProc(hWnd, msg, wParam, lParam);
}

int main(void) {
    std::thread communication_thread;
    signal(SIGINT, stop);

    communication_thread = std::thread(handleCommunication);

    renderGUI();

    running = 0;
    communication_thread.join();

    printf("[*] Cleanup\n");
    cleanup();

    printf("[*] Exitting\n");
    return 0;
}
