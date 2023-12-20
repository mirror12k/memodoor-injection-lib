#include <ctype.h>
#include <stdio.h>
#include <pthread.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdbool.h>
#include <sys/mman.h>

#include <fcntl.h>
#include <setjmp.h>
#include <signal.h>
#include <dlfcn.h>
#include <dirent.h>

#define DEFAULT_PORT 8999

typedef void (*RouteHandler)(int, const char *, const char *);

// Route struct
typedef struct {
    char *path;
    RouteHandler handler;
    bool hasParam;
    char *method;
} Route;

// Route handlers
void handleRoot(int socket, const char *param, const char *body) {
    char *response = "HTTP/1.1 200 OK\n\nWelcome to the root!";
    send(socket, response, strlen(response), 0);
}

void handleDlsym(int socket, const char *symbol, const char *body) {
    void *handle = dlopen(NULL, RTLD_NOW);  // Use the main program's symbol table
    char response[2048];
    if (handle) {
        void *sym = dlsym(handle, symbol);
        if (sym) {
            snprintf(response, sizeof(response), "HTTP/1.1 200 OK\nContent-Type: text/plain\n\n%p", sym);
        } else {
            snprintf(response, sizeof(response), "HTTP/1.1 404 Not Found\nContent-Type: text/plain\n\nSymbol not found.\n");
        }
        dlclose(handle);
    } else {
        snprintf(response, sizeof(response), "HTTP/1.1 500 Internal Server Error\nContent-Type: text/plain\n\nFailed to open symbol table.\n");
    }
    send(socket, response, strlen(response), 0);
}


void handleDlopen(int socket, const char *libraryPath, const char *body) {
    void *handle = dlopen(libraryPath, RTLD_LAZY);
    char response[1024];
    if (handle) {
        snprintf(response, sizeof(response), "HTTP/1.1 200 OK\nContent-Type: text/plain\n\n%p", handle);
        dlclose(handle);
    } else {
        snprintf(response, sizeof(response), "HTTP/1.1 500 Internal Server Error\nContent-Type: text/plain\n\nFailed to open library.\n");
    }
    send(socket, response, strlen(response), 0);
}


extern char **environ;

void handleEnv(int socket, const char *param, const char *body) {
    char buffer[1024 * 128];
    int len = snprintf(buffer, sizeof(buffer), "HTTP/1.1 200 OK\nContent-Type: text/plain\n\n");
    for (char **env = environ; *env && len < sizeof(buffer); ++env) {
        len += snprintf(buffer + len, sizeof(buffer) - len, "%s\n", *env);
    }
    send(socket, buffer, len, 0);
}

void handleThreads(int socket, const char *param, const char *body) {
    DIR *dir;
    struct dirent *entry;
    char response[8192];
    int len = snprintf(response, sizeof(response), "HTTP/1.1 200 OK\nContent-Type: text/plain\n\n");

    dir = opendir("/proc/self/task");
    if (dir != NULL) {
        while ((entry = readdir(dir)) != NULL) {
            if (entry->d_name[0] != '.') { // Skip '.' and '..' directories
                len += snprintf(response + len, sizeof(response) - len, "Thread ID: %s\n", entry->d_name);
                if (len >= sizeof(response) - 128) { // Prevent buffer overflow
                    // Send what we have and reset the buffer
                    send(socket, response, len, 0);
                    len = 0;
                }
            }
        }
        closedir(dir);
    } else {
        len += snprintf(response + len, sizeof(response) - len, "Failed to open thread directory.\n");
    }

    // Send any remaining data
    send(socket, response, len, 0);
}

void handleProcess(int socket, const char *param, const char *body) {
    char response[8192];
    int len = 0;
    DIR *dir;
    struct dirent *entry;
    int threadCount = 0;

    // Get process ID
    pid_t pid = getpid();

    // Count number of threads by reading /proc/self/task
    dir = opendir("/proc/self/task");
    if (dir != NULL) {
        while ((entry = readdir(dir)) != NULL) {
            if (entry->d_name[0] != '.') {
                threadCount++;
            }
        }
        closedir(dir);
    }

    // Get execution command from /proc/self/cmdline
    FILE *cmdline = fopen("/proc/self/cmdline", "r");
    char command[256];
    if (cmdline != NULL) {
        size_t n = fread(command, 1, sizeof(command) - 1, cmdline);
        fclose(cmdline);
        command[n] = '\0'; // Ensure null termination
        // Replace null character between arguments with spaces
        for (int i = 0; i < n; i++) {
            if (command[i] == '\0') {
                command[i] = ' ';
            }
        }
    } else {
        snprintf(command, sizeof(command), "Unavailable");
    }

    // Construct the response
    len += snprintf(response + len, sizeof(response) - len, "HTTP/1.1 200 OK\nContent-Type: text/plain\n\n");
    len += snprintf(response + len, sizeof(response) - len, "Process ID: %d\n", pid);
    len += snprintf(response + len, sizeof(response) - len, "Number of Threads: %d\n", threadCount);
    len += snprintf(response + len, sizeof(response) - len, "Execution Command: %s\n", command);

    // Send the response
    send(socket, response, len, 0);
}







static sigjmp_buf jumpBuffer;

void segfault_sigaction(int signal, siginfo_t *si, void *arg) {
    siglongjmp(jumpBuffer, 1);
}

// Function to convert hex string to byte array
void hexStringToByteArray(const char *hexString, unsigned char *byteArray, int byteArrayLength) {
    for (int i = 0; i < byteArrayLength; ++i) {
        sscanf(hexString + 2 * i, "%2hhx", &byteArray[i]);
    }
}

void handleSearchMemory(int socket, const char *hexString, const char *body) {
    // Convert hex string to byte array
    int hexStringLength = strlen(hexString);
    int byteArrayLength = hexStringLength / 2;
    unsigned char *searchBytes = malloc(byteArrayLength);
    if (!searchBytes) {
        char *errorResponse = "HTTP/1.1 500 Internal Server Error\n\nFailed to allocate memory.";
        send(socket, errorResponse, strlen(errorResponse), 0);
        return;
    }
    hexStringToByteArray(hexString, searchBytes, byteArrayLength);

    struct sigaction sa;
    memset_s(&sa, 0, sizeof(sigaction));
    sigemptyset(&sa.sa_mask);
    sa.sa_sigaction = segfault_sigaction;
    sa.sa_flags = SA_SIGINFO;
    sigaction(SIGBUS, &sa, NULL);
    sigaction(SIGSEGV, &sa, NULL);

    // Send the initial HTTP header
    const char *header = "HTTP/1.1 200 OK\nContent-Type: text/plain\n\n";
    send(socket, header, strlen(header), 0);

    FILE *maps = fopen("/proc/self/maps", "r");
    if (!maps) {
        char *errorResponse = "HTTP/1.1 500 Internal Server Error\n\nFailed to open memory maps.";
        send(socket, errorResponse, strlen(errorResponse), 0);
        free(searchBytes);
        return;
    }

    char line[256];
    unsigned long start, end;
    char permissions[5];
    int count = 0;

    while (fgets(line, sizeof(line), maps)) {
        if (sscanf(line, "%lx-%lx %4s", &start, &end, permissions) == 3) {
            if (permissions[0] == 'r') {
                for (unsigned char *currentAddress = (unsigned char *)start; currentAddress < (unsigned char *)end - byteArrayLength; ++currentAddress) {
                    if (sigsetjmp(jumpBuffer, 1) == 0) {
                        if (memcmp(currentAddress, searchBytes, byteArrayLength) == 0) {
                            count++;
                            char addressInfo[256];
                            int infoLength = snprintf(addressInfo, sizeof(addressInfo), "%p\n", (void *)currentAddress);
                            send(socket, addressInfo, infoLength, 0);
                        }
                    }
                }
            }
        }
    }

    fclose(maps);
    free(searchBytes);

    // Send the count of found instances
    char countMessage[256];
    int messageLength = snprintf(countMessage, sizeof(countMessage), "Instances found: %d\n", count);
    send(socket, countMessage, messageLength, 0);
}



typedef void (*FunctionPointer)();

void handleMmap(int socket, const char *param, const char *body) {
    unsigned long address;
    size_t size;
    int prot = 0;
    char permissions[4];
    int n = sscanf(param, "%lx:%zx:%3s", &address, &size, permissions);

    if (n != 3) {
        char *errorResponse = "HTTP/1.1 400 Bad Request\n\nInvalid parameters.";
        send(socket, errorResponse, strlen(errorResponse), 0);
        return;
    }

    // Set protection flags based on the permissions string
    if (strchr(permissions, 'r')) prot |= PROT_READ;
    if (strchr(permissions, 'w')) prot |= PROT_WRITE;
    if (strchr(permissions, 'x')) prot |= PROT_EXEC;

    // Perform the memory mapping
    void *mappedAddress = mmap((void *)address, size, prot, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (mappedAddress == MAP_FAILED) {
        char *errorResponse = "HTTP/1.1 500 Internal Server Error\n\nmmap failed.";
        send(socket, errorResponse, strlen(errorResponse), 0);
        return;
    }

    // Send a success response
    char successResponse[1024];
    snprintf(successResponse, sizeof(successResponse), "HTTP/1.1 200 OK\n\n%p", mappedAddress);
    send(socket, successResponse, strlen(successResponse), 0);
}

void handleExec(int socket, const char *param, const char *body) {
    unsigned long address;
    int n = sscanf(param, "%lx", &address);
    if (n != 1) {
        char *errorResponse = "HTTP/1.1 400 Bad Request\n\nInvalid address.";
        send(socket, errorResponse, strlen(errorResponse), 0);
        return;
    }

    // Convert address to function pointer
    FunctionPointer func = (FunctionPointer)address;

    // Execute the function
    func();

    // Send a success response
    char *successResponse = "HTTP/1.1 200 OK\n\nFunction executed successfully.";
    send(socket, successResponse, strlen(successResponse), 0);
}


void handleMemoryPut(int socket, const char *param, const char *body) {
    unsigned long address;
    int length;
    int n = sscanf(param, "%lx:%x", &address, &length);
    if (n != 2) {
        char *errorResponse = "HTTP/1.1 400 Bad Request\n\nInvalid parameters.";
        send(socket, errorResponse, strlen(errorResponse), 0);
        return;
    }

    // Convert address to pointer
    unsigned char *memoryPtr = (unsigned char *)address;
    // Write the body to memory
    memcpy(memoryPtr, body, length);

    // Send a success response
    char *successResponse = "HTTP/1.1 200 OK\n\nData written successfully.";
    send(socket, successResponse, strlen(successResponse), 0);
}


void handleMemory(int socket, const char *param, const char *body) {
    unsigned long address;
    int chunk_size;
    int n = sscanf(param, "%lx:%x", &address, &chunk_size);

    // Check if the parameters were successfully parsed
    if (n != 2) {
        char *errorResponse = "HTTP/1.1 400 Bad Request\n\nInvalid parameters.";
        send(socket, errorResponse, strlen(errorResponse), 0);
        return;
    }

    // Convert address to pointer
    unsigned char *memoryPtr = (unsigned char *)address;

    // Construct HTTP response header
    char header[1024];
    snprintf(header, sizeof(header), "HTTP/1.1 200 OK\nContent-Length: %d\n\n", chunk_size);

    // Send the header
    send(socket, header, strlen(header), 0);

    // Send the memory chunk
    send(socket, memoryPtr, chunk_size, 0);
}

void handleMaps(int socket, const char *param, const char *body) {
    // Buffer for reading file content
    char buffer[8192];

    // Open the /proc/self/maps file
    FILE *fp = fopen("/proc/self/maps", "r");
    if (fp == NULL) {
        char *errorResponse = "HTTP/1.1 500 Internal Server Error\n\nUnable to open maps file.";
        send(socket, errorResponse, strlen(errorResponse), 0);
        return;
    }

    // Start sending the HTTP response
    char *header = "HTTP/1.1 200 OK\nContent-Type: text/plain\n\n";
    send(socket, header, strlen(header), 0);

    // Read and send the file content
    size_t bytesRead;
    while ((bytesRead = fread(buffer, 1, sizeof(buffer), fp)) > 0) {
        send(socket, buffer, bytesRead, 0);
    }

    // Close the file
    fclose(fp);
}

#include <Python.h>

void handleExecutePython(int socket, const char *param, const char *body) {
    // Initialize Python interpreter
    Py_Initialize();

    // Create a Python object that will capture stdout
    PyObject *sys = PyImport_ImportModule("sys");
    PyObject *io = PyImport_ImportModule("io");
    PyObject *stdoutCapture = PyObject_CallMethod(io, "StringIO", NULL);
    PyModule_AddObject(sys, "stdout", stdoutCapture);
    Py_INCREF(stdoutCapture); // Increment reference since PyModule_AddObject steals a reference

    // Execute the Python code
    PyRun_SimpleString(body);

    // Retrieve the output from the StringIO object
    PyObject *output = PyObject_CallMethod(stdoutCapture, "getvalue", NULL);
    const char *stdoutString = PyUnicode_AsUTF8(output);

    // Prepare and send the HTTP response
    char *responseHeader = "HTTP/1.1 200 OK\nContent-Type: text/plain\n\n";
    send(socket, responseHeader, strlen(responseHeader), 0);
    send(socket, stdoutString, strlen(stdoutString), 0);

    // Clean up
    Py_DECREF(output);
    Py_DECREF(stdoutCapture);
    Py_DECREF(io);
    Py_DECREF(sys);

    // Finalize the Python interpreter
    Py_Finalize();
}


void handleNotFound(int socket, const char *param, const char *body) {
    char *response = "HTTP/1.1 404 Not Found\n\nNot Found";
    send(socket, response, strlen(response), 0);
}

// Dispatch table for routes
Route routes[] = {
    {"/", handleRoot, false, "get"},
    {"/maps", handleMaps, false, "get"},
    {"/env", handleEnv, false, "get"},
    {"/threads", handleThreads, false, "get"},
    {"/process", handleProcess, false, "get"},
    {"/memory/{}", handleMemory, true, "get"},
    {"/memory/{}", handleMemoryPut, true, "put"},
    {"/exec/{}", handleExec, true, "get"},
    {"/mmap/{}", handleMmap, true, "get"},
    {"/search/{}", handleSearchMemory, true, "get"},
    {"/dlsym/{}", handleDlsym, true, "get"},
    {"/dlopen/{}", handleDlopen, true, "get"},
    {"/python", handleExecutePython, true, "put"},
    {NULL, handleNotFound, false, "get"}, // Default route
};


// Function to match and extract dynamic route parameter
bool matchDynamicRoute(const char *routePath, const char *requestPath, char *param) {
    const char *routePtr = routePath;
    const char *requestPtr = requestPath;

    while (*routePtr && *requestPtr) {
        if (*routePtr == '{' && *++routePtr == '}') {  // Dynamic segment found
            while (*requestPtr && *requestPtr != '/') {
                *param++ = *requestPtr++;
            }
            *param = '\0';  // Null terminate the parameter
            return true;
        }
        if (*routePtr++ != *requestPtr++) {
            return false;
        }
    }
    return *routePtr == '\0' && *requestPtr == '\0';
}

void http_handler(int socket) {
    char buffer[8192] = {0};
    int totalBytesRead = 0, bytesRead;
    char *headerEnd;
    int contentLength = 0;

    // Continuously read and buffer the request
    while ((bytesRead = read(socket, buffer + totalBytesRead, sizeof(buffer) - totalBytesRead)) > 0) {
        totalBytesRead += bytesRead;
        buffer[totalBytesRead] = '\0';

        // Check if the end of the header is in the buffer
        if ((headerEnd = strstr(buffer, "\r\n\r\n")) != NULL) {
            headerEnd += 4; // Move past the end of the header

            // Convert the header section to lower case for case-insensitive search
            for (int i = 0; i < (headerEnd - buffer); i++) {
                buffer[i] = tolower(buffer[i]);
            }

            // Find Content-Length header
            char *contentLengthHeader = strstr(buffer, "content-length:");
            if (contentLengthHeader) {
                sscanf(contentLengthHeader, "content-length: %d", &contentLength);
            }

            break;
        }
    }

    // Extract method and path
    char method[10], path[1024];
    sscanf(buffer, "%s %s", method, path);

    // Calculate the size of the already read body part
    int bodySize = totalBytesRead - (headerEnd - buffer);

    // Allocate memory for the body and copy the already read part
    char *body = malloc(contentLength + 1);
    if (body == NULL) {
        // Handle memory allocation failure
        char *errorResponse = "HTTP/1.1 500 Internal Server Error\n\nFailed to allocate memory.";
        send(socket, errorResponse, strlen(errorResponse), 0);
        close(socket);
        return;
    }
    memcpy(body, headerEnd, bodySize);

    // If the body part is less than contentLength, read the remaining part
    while (bodySize < contentLength) {
        bytesRead = read(socket, body + bodySize, contentLength - bodySize);
        if (bytesRead <= 0) {
            // Handle read error or no data
            free(body);
            char *errorResponse = "HTTP/1.1 500 Internal Server Error\n\nFailed to read complete request.";
            send(socket, errorResponse, strlen(errorResponse), 0);
            close(socket);
            return;
        }
        bodySize += bytesRead;
    }
    body[contentLength] = '\0'; // Null-terminate the body

    // Find and call the appropriate route handler
    int i;
    for (i = 0; routes[i].path != NULL; i++) {
        char param[1024] = {0};
        bool isMatch = (routes[i].hasParam && matchDynamicRoute(routes[i].path, path, param)) ||
                       strcmp(path, routes[i].path) == 0;
        bool isMethodMatch = strcmp(method, routes[i].method) == 0;

        if (isMatch && isMethodMatch) {
            printf("%s [%s]\n", method, path);
            routes[i].handler(socket, param, body);
            break;
        }
    }


    if (routes[i].path == NULL)
        routes[i].handler(socket, "", body);

    free(body);
    close(socket);
}



void* socketThread(void *arg) {
    int server_fd, new_socket;
    struct sockaddr_in address;
    int addrlen = sizeof(address);
    char buffer[1024] = {0};
    char *hello = "hello world\n";

    // Creating socket file descriptor
    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        perror("socket failed");
        pthread_exit(NULL);
    }

    printf("socket created\n");

    const char *env_port = getenv("MEMODOOR_PORT");
    int port = env_port ? atoi(env_port) : DEFAULT_PORT;

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(port);

    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("bind failed");
        pthread_exit(NULL);
    }

    if (listen(server_fd, 3) < 0) {
        perror("listen");
        pthread_exit(NULL);
    }

    while(1) {
        // printf("waiting for connections...\n");
        if ((new_socket = accept(server_fd, (struct sockaddr *)&address, (socklen_t*)&addrlen))<0) {
            perror("accept");
            continue;
        }
        // printf("got socket!\n");

        // // Send hello world message
        // send(new_socket, hello, strlen(hello), 0);
        // close(new_socket);
        http_handler(new_socket);
    }

    close(server_fd);
    pthread_exit(NULL);
}

__attribute__((constructor))
void initializer() {
    printf("Hello World - Library Loaded\n");
    pthread_t tid;
    if(pthread_create(&tid, NULL, socketThread, NULL) != 0) {
        printf("Failed to create thread\n");
    }
}
