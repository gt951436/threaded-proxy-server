#define _WIN32_WINNT 0x0600

#include <stdio.h>
#include "proxy_parse.h"
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <fcntl.h>
#include <time.h>
#include <errno.h>
#include <pthread.h>
#include <semaphore.h>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#ifdef _MSC_VER
# pragma comment(lib, "ws2_32.lib")
#endif

#ifndef SHUT_RDWR
#define SHUT_RDWR SD_BOTH
#endif
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#endif

int checkHTTPversion(char *req);
int handleRequest(int clientSocketId, struct ParsedRequest *request, char *tempReq);
int sendErrorMessage(int socket, int statusCode);
int connectRemoteServer(char *host_addr, int portNumber);

#define MAX_CLIENTS 10                  // maximum client req served at a time
#define MAX_BYTES 4096                  // max. bytes read from client req
#define MAX_ELEMENT_SIZE 10 * (1 << 10) // max. size of cache element
#define MAX_SIZE 200 * (1 << 20)        // max. size of cache in bytes

struct cacheElement
{
    char *data;                // data stores http request's response
    int len;                   // sizeof(data)
    char *url;                 // the request url
    time_t lruTimeTrack;       // the latest time the element is  accesed
    struct cacheElement *next; // pointer to next element
};

struct cacheElement *find(char *url);
int add_cacheElement(char *data, int size, char *url);
void remove_cacheElement();

int portNumber = 8080;           // Default Port
int proxySocketId;               // socket descriptor of proxy server
pthread_t threadId[MAX_CLIENTS]; // array to store the thread ids of clients
sem_t semaphore;                 // if client requests exceeds the max_clients this seamaphore puts the
pthread_mutex_t lock;

struct cacheElement *cacheHead = NULL; // head of the cache LL
int cacheSize = 0;                     // current cache size

void *thread_fn(void *socketNew)
{
    sem_wait(&semaphore); // wait for semaphore to be available
    int p;
    sem_getvalue(&semaphore, &p);
    printf("Semaphore value: %d\n", p);

    int *t = (int *)socketNew;
    int socket = *t;
    printf("Thread created for socket: %d\n", socket);

    char *buffer = (char *)calloc(MAX_BYTES, sizeof(char));
    if (!buffer)
    {
        perror("Memory allocation failed");
        sem_post(&semaphore);
        return NULL;
    }

    int bytesRecv = recv(socket, buffer, MAX_BYTES, 0);
    int totalRecv = bytesRecv;
    while (bytesRecv > 0)
    {
        if (strstr(buffer, "\r\n\r\n") != NULL)
        {
            break;
        }
        bytesRecv = recv(socket, buffer + totalRecv, MAX_BYTES - totalRecv, 0);
        if (bytesRecv > 0)
        {
            totalRecv += bytesRecv;
        }
    }
    if (totalRecv <= 0)
    {
        perror("Error receiving data from client");
        free(buffer);
        closesocket(socket);
        sem_post(&semaphore);
        return NULL;
    }
    buffer[totalRecv] = '\0';
    char *tempReq = strdup(buffer);
    if (!tempReq)
    {
        perror("Memory allocation failed");
        free(buffer);
        closesocket(socket);
        sem_post(&semaphore);
        return NULL;
    }
    struct cacheElement *cached = find(tempReq);
    if (cached)
    {
        printf("Cache hit for URL: %s\n", cached->url);
        int sent = 0;
        while (sent < cached->len)
        {
            int toSend = (cached->len - sent) > MAX_BYTES ? MAX_BYTES : (cached->len - sent);
            int bytesSent = send(socket, cached->data + sent, toSend, 0);
            if (bytesSent <= 0)
            {
                perror("Error sending cached data");
                break;
            }
            sent += bytesSent;
        }
        printf("Data retrieved from cache\n");
    }
    else if (totalRecv > 0)
    {
        struct ParsedRequest *request = ParsedRequest_create();
        if (!request)
        {
            perror("Failed to create request");
            free(buffer);
            free(tempReq);
            closesocket(socket);
            sem_post(&semaphore);
            return NULL;
        }
        if (ParsedRequest_parse(request, buffer, totalRecv) < 0)
        {
            printf("Error parsing request\n");
            sendErrorMessage(socket, 400);
        }
        else
        {
            if (strcmp(request->method, "GET") == 0)
            {
                if (request->host && request->path && checkHTTPversion(request->version) == 1)
                {
                    handleRequest(socket, request, tempReq);
                }
                else
                {
                    sendErrorMessage(socket, 400);
                }
            }
            else
            {
                printf("Unsupported method: %s\n", request->method);
                sendErrorMessage(socket, 501);
            }
        }
        ParsedRequest_destroy(request);
    }
    shutdown(socket, SHUT_RDWR);
    closesocket(socket);
    free(buffer);
    free(tempReq);

    sem_post(&semaphore);
    sem_getvalue(&semaphore, &p);
    printf("Semaphore value after release: %d\n", p);

    return NULL;
}

int sendErrorMessage(int socket, int statusCode)
{
    char str[1024];
    char currentTime[50];
    time_t now = time(0);

    struct tm data = *gmtime(&now);
    strftime(currentTime, sizeof(currentTime), "%a, %d %b %Y %H:%M:%S %Z", &data);

    switch (statusCode)
    {
    case 400:
        snprintf(str, sizeof(str), "HTTP/1.1 400 Bad Request\r\nContent-Length: 95\r\nConnection: keep-alive\r\nContent-Type: text/html\r\nDate: %s\r\nServer: ProxyServer\r\n\r\n<HTML><HEAD><TITLE>400 Bad Request</TITLE></HEAD>\n<BODY><H1>400 Bad Rqeuest</H1>\n</BODY></HTML>", currentTime);
        printf("400 Bad Request\n");
        send(socket, str, strlen(str), 0);
        break;
    case 403:
        snprintf(str, sizeof(str), "HTTP/1.1 403 Forbidden\r\nContent-Length: 112\r\nContent-Type: text/html\r\nConnection: keep-alive\r\nDate: %s\r\nServer: ProxyServer\r\n\r\n<HTML><HEAD><TITLE>403 Forbidden</TITLE></HEAD>\n<BODY><H1>403 Forbidden</H1><br>Permission Denied\n</BODY></HTML>", currentTime);
        printf("403 Forbidden\n");
        send(socket, str, strlen(str), 0);
        break;
    case 404:
        snprintf(str, sizeof(str), "HTTP/1.1 404 Not Found\r\nContent-Length: 91\r\nContent-Type: text/html\r\nConnection: keep-alive\r\nDate: %s\r\nServer: ProxyServer\r\n\r\n<HTML><HEAD><TITLE>404 Not Found</TITLE></HEAD>\n<BODY><H1>404 Not Found</H1>\n</BODY></HTML>", currentTime);
        printf("404 Not Found\n");
        send(socket, str, strlen(str), 0);
        break;
    case 500:
        snprintf(str, sizeof(str), "HTTP/1.1 500 Internal Server Error\r\nContent-Length: 115\r\nConnection: keep-alive\r\nContent-Type: text/html\r\nDate: %s\r\nServer: ProxyServer\r\n\r\n<HTML><HEAD><TITLE>500 Internal Server Error</TITLE></HEAD>\n<BODY><H1>500 Internal Server Error</H1>\n</BODY></HTML>", currentTime);
        printf("500 Internal Server Error\n");
        send(socket, str, strlen(str), 0);
        break;
    case 501:
        snprintf(str, sizeof(str), "HTTP/1.1 501 Not Implemented\r\nContent-Length: 103\r\nConnection: keep-alive\r\nContent-Type: text/html\r\nDate: %s\r\nServer: ProxyServer\r\n\r\n<HTML><HEAD><TITLE>404 Not Implemented</TITLE></HEAD>\n<BODY><H1>501 Not Implemented</H1>\n</BODY></HTML>", currentTime);
        printf("501 Not Implemented\n");
        send(socket, str, strlen(str), 0);
        break;
    case 505:
        snprintf(str, sizeof(str), "HTTP/1.1 505 HTTP Version Not Supported\r\nContent-Length: 125\r\nConnection: keep-alive\r\nContent-Type: text/html\r\nDate: %s\r\nServer: ProxyServer\r\n\r\n<HTML><HEAD><TITLE>505 HTTP Version Not Supported</TITLE></HEAD>\n<BODY><H1>505 HTTP Version Not Supported</H1>\n</BODY></HTML>", currentTime);
        printf("505 HTTP Version Not Supported\n");
        send(socket, str, strlen(str), 0);
        break;
    default:
        return -1; // unsupported status code
    }
    return 1;
}

int checkHTTPversion(char *req)
{
    if (!req)
    {
        return -1;
    }
    if (strncmp(req, "HTTP/1.1", 8) == 0)
    {
        return 1;
    }
    if (strncmp(req, "HTTP/1.0", 8) == 0)
    {
        return 1;
    }
    return -1;
}

int handleRequest(int clientSocketId, struct ParsedRequest *request, char *tempReq)
{
    char *buf = (char *)malloc(sizeof(char) * MAX_BYTES);
    strcpy(buf, "GET ");
    strcat(buf, request->path);
    strcat(buf, " ");
    strcat(buf, request->version);
    strcat(buf, "\r\n");
    size_t len = strlen(buf);

    if (ParsedHeader_set(request, "Connection", "close") < 0)
    {
        printf("Set Connection header key is not working\n");
    }

    if (ParsedHeader_get(request, "Host") == NULL)
    {
        if (ParsedHeader_set(request, "Host", request->host) < 0)
        {
            printf("Set host header key is not working\n");
        }
    }
    if (ParsedRequest_unparse_headers(request, buf + len, (size_t)MAX_BYTES - len) < 0)
    {
        printf("Unparse headers failed\n");
        free(buf);
        return -1;
    }

    int serverPort = request->port ? atoi(request->port) : 80; // default port for HTTP main server

    int remoteSocketId = connectRemoteServer(request->host, serverPort);
    if (remoteSocketId < 0)
    {
        free(buf);
        return -1;
    }
    // send request(bytes) to remote server
    int bytesSend = send(remoteSocketId, buf, strlen(buf), 0);
    memset(buf, 0, MAX_BYTES);
    if (bytesSend <= 0)
    {
        perror("Failed to send request to server");
        closesocket(remoteSocketId);
        return -1;
    }

    bytesSend = recv(remoteSocketId, buf, MAX_BYTES - 1, 0); //  a b c f \0
    char *tempBuffer = (char *)malloc(sizeof(char) * MAX_BYTES);
    int tempBufferSize = MAX_BYTES;
    int idx = 0; // temp buffer index

    while (bytesSend > 0)
    {
        bytesSend = send(clientSocketId, buf, bytesSend, 0);
        for (int i = 0; i < bytesSend / sizeof(char); ++i)
        {
            tempBuffer[idx] = buf[i];
            idx += 1;
        }
        tempBufferSize += MAX_BYTES;
        tempBuffer = (char *)realloc(tempBuffer, tempBufferSize);
        if (bytesSend < 0)
        {
            perror("Error sending data to client\n");
            break;
        }
        memset(buf, 0, MAX_BYTES);
        bytesSend = recv(remoteSocketId, buf, MAX_BYTES - 1, 0);
    }
    tempBuffer[idx] = '\0'; // buffer end
    free(buf);
    add_cacheElement(tempBuffer, strlen(tempBuffer), tempReq);
    free(tempBuffer);
    closesocket(remoteSocketId);
    return 0;
}

int connectRemoteServer(char *host_addr, int portNumber)
{
    int remoteSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (remoteSocket < 0)
    {
        printf("Error creating socket for remote server\n");
        return -1;
    }
    struct hostent *host = gethostbyname(host_addr);
    if (host == NULL)
    {
        fprintf(stderr, "Error: No such host as %s exists\n", host_addr);
        closesocket(remoteSocket);
        return -1;
    }
    struct sockaddr_in server_addr;
    memset((char *)&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(portNumber);

    memcpy((char *)&server_addr.sin_addr.s_addr, (char *)&host->h_addr, host->h_length);
    if (connect(remoteSocket, (struct sockaddr *)&server_addr, (size_t)sizeof(server_addr)) < 0)
    {
        fprintf(stderr, "Error connecting to remote server %s:%d\n", host_addr, portNumber);
        closesocket(remoteSocket);
        return -1;
    }
    return remoteSocket;
}

int main(int argc, char **argv)
{
#ifdef _WIN32
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
    {
        perror("WSAStartup failed");
        exit(1);
    }
#endif

    int clientSocketId;
    int client_len;
    struct sockaddr_in server_addr, client_addr;

    // port number given as CLI argument
    if (argc != 2)
    {
        printf("Too few arguments. Usage: %s <port>\n", argv[0]);
        exit(1);
    }
    portNumber = atoi(argv[1]); // .proxy 8090 --> proxy portNumber = 8090

    sem_init(&semaphore, 0, MAX_CLIENTS); // min = 0,max = MAX_CLIENTS
    pthread_mutex_init(&lock, NULL);

    printf("Starting Proxy Server at port : %d\n", portNumber);

    // main(global) proxy socket
    proxySocketId = socket(AF_INET, SOCK_STREAM, 0);
    if (proxySocketId < 0)
    {
        perror("Socket creation Failed\n");
        exit(1);
    }
    // enable socket reuse for new requests and thread spawning
    int reuse = 1;
    if (setsockopt(proxySocketId, SOL_SOCKET, SO_REUSEADDR, (const char *)&reuse, sizeof(reuse)) < 0)
    {
        perror("setsockopt failed\n");
        closesocket(proxySocketId);
        exit(1);
    }

    // cleaning buffer
    memset((char *)&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(portNumber);
    server_addr.sin_addr.s_addr = INADDR_ANY; // bind to all interfaces

    if (bind(proxySocketId, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
    {
        perror("Port not available for binding\n");
        closesocket(proxySocketId);
        exit(1);
    }
    printf("Proxy Server bound to port %d\n", portNumber);
    if (listen(proxySocketId, MAX_CLIENTS) < 0)
    {
        perror("Error in listening on socket\n");
        closesocket(proxySocketId);
        exit(1);
    }
    printf("Proxy Server is listening for connections on port: %d\n", portNumber);

    int threadCount = 0;
    int clientSockets[MAX_CLIENTS]; // connected client socket ids

    while (1)
    {
        // memset((char *)&client_addr, 0, sizeof(client_addr));
        client_len = sizeof(client_addr);
        clientSocketId = accept(proxySocketId, (struct sockaddr *)&client_addr, (int *)&client_len);
        if (clientSocketId < 0)
        {
            perror("Error accepting client connection\n");
            // exit(1);
            continue; // continue accepting next client
        }

        // connectedSocketId[i] = clientSocketId;
        // printf("Client connected with socket id: %d\n", clientSocketId);

        char clientIP[INET_ADDRSTRLEN];
#ifdef _WIN32
        InetNtopA(AF_INET, &client_addr.sin_addr, clientIP, INET_ADDRSTRLEN);
#else
        inet_ntop(AF_INET, &client_addr.sin_addr, clientIP, INET_ADDRSTRLEN);

#endif
        printf("Client is connected with port: %d and it's IP Adress: %s\n", ntohs(client_addr.sin_port), clientIP);
        if (threadCount >= MAX_CLIENTS)
        {
            fprintf(stderr, "Too many clients, rejecting connection\n");
            close(clientSocketId);
            continue;
        }
        clientSockets[threadCount] = clientSocketId; // store the socket id
        if (pthread_create(&threadId[threadCount], NULL, thread_fn, &clientSockets[threadCount]) != 0)
        {
            perror("Thread creation failed");
            closesocket(clientSocketId);
            continue;
        }
        threadCount = (threadCount + 1) % MAX_CLIENTS;
    }
    closesocket(proxySocketId);
#ifdef _WIN32
    WSACleanup();
#endif
    return 0;
}

struct cacheElement *find(char *url)
{
    pthread_mutex_lock(&lock);
    struct cacheElement *current = cacheHead;
    struct cacheElement *found = NULL;

    if (current != NULL)
    {
        while (current)
        {
            if (strcmp(current->url, url) == 0)
            {
                found = current;
                current->lruTimeTrack = time(NULL);
                break;
            }
            current = current->next;
        }
    }
    else
    {
        printf("Cache is empty, no URL found\n");
    }
    pthread_mutex_unlock(&lock); // release lock
    printf("Remove cache lock released\n");
    return found;
}

int add_cacheElement(char *data, int size, char *url)
{
    pthread_mutex_lock(&lock);
    printf("Add cache lock acquired\n");

    int elementSize = size + 1 + strlen(url) + sizeof(struct cacheElement);
    if (elementSize > MAX_ELEMENT_SIZE)
    {
        pthread_mutex_unlock(&lock);
        printf("Add cache lock released\n");
        return 0; // cannot add element to cache, size exceeds limit
    }

    while (cacheSize + elementSize > MAX_SIZE && cacheHead)
    {
        remove_cacheElement(); // remove LRU element
    }
    struct cacheElement *newElement = malloc(sizeof(struct cacheElement));
    if (!newElement)
    {
        pthread_mutex_unlock(&lock);
        return 0;
    }
    newElement->data = (char *)malloc(size + 1);
    if (!newElement->data)
    {
        free(newElement);
        pthread_mutex_unlock(&lock);
        return 0;
    }
    memcpy(newElement->data, data, size);

    newElement->url = strdup(url);
    if (!newElement->url)
    {
        free(newElement->data);
        free(newElement);
        pthread_mutex_unlock(&lock);
        return 0;
    }

    // memcpy(newElement->url, url, size);

    newElement->lruTimeTrack = time(NULL);
    newElement->next = cacheHead;
    newElement->len = size;
    cacheHead = newElement;
    cacheSize += elementSize;

    pthread_mutex_unlock(&lock); // release lock
    printf("Add cache lock released\n");
    return 1; // element added successfully
}

void remove_cacheElement()
{
    pthread_mutex_lock(&lock);

    if (!cacheHead)
    {
        pthread_mutex_unlock(&lock);
        return;
    }

    struct cacheElement *prev = NULL;
    struct cacheElement *current = cacheHead;
    struct cacheElement *lruPrev = NULL;
    struct cacheElement *lru = cacheHead;
    time_t oldest = cacheHead->lruTimeTrack;

    while (current)
    {
        if (current->lruTimeTrack < oldest)
        {
            oldest = current->lruTimeTrack;
            lru = current;
            lruPrev = prev;
        }
        prev = current;
        current = current->next;
    }

    if (lruPrev)
    {
        lruPrev->next = lru->next;
    }
    else
    {
        cacheHead = lru->next;
    }

    int elementSize = lru->len + strlen(lru->url) + sizeof(struct cacheElement);
    cacheSize -= elementSize;

    free(lru->data);
    free(lru->url);
    free(lru);

    pthread_mutex_unlock(&lock);
    printf("cache Lock released\n");
}