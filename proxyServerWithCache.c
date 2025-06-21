#include "proxy_parse.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <fcntl.h>
#include <time.h>
#include <errno.h>
#include <pthread.h>
#include <semaphore.h>
#include <time.h>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#ifndef SHUT_RDWR
#define SHUT_RDWR SD_BOTH
#endif
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#endif

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
                                 // waiting threads to sleep and wakes them when traffic on queue decreases
// sem_t cache_lock;
pthread_mutex_t lock;

struct cacheElement *cacheHead; // head of the cache LL
int cacheSize;                  // current cache size

void *thread_fn(void *socketNew)
{
    sem_wait(&semaphore); // wait for semaphore to be available
    int p;
    sem_getvalue(&semaphore, p);
    printf("Semaphore value: %d\n", p);
    int *t = (int *)socketNew;
    int socket = *t;
    printf("Thread created for socket: %d\n", socket);

    int bytesSendClient, len;
    char *buffer = (char *)calloc(MAX_BYTES, sizeof(char));
    bzero(buffer, MAX_BYTES);

    bytesSendClient = recv(socket, buffer, MAX_BYTES, 0);
    // if (bytesSendClient < 0)
    // {
    //     perror("Error recieving data from client\n");
    // }
    // else
    // {
    //     printf("Recieved %d bytes from client\n", bytesSendClient);
    // }
    while (bytesSendClient > 0)
    {
        len = strlen(buffer);
        if (strstr(buffer, "\r\n\r\n") == NULL)
        {
            bytesSendClient = recv(socket, buffer + len, MAX_BYTES - len, 0);
        }
        else
        {
            break; // end of HTTP request is reached
        }
    }
    char *tempReq = (char *)malloc(strlen(buffer) * sizeof(char) + 1);
    for (int i = 0; i < strlen(buffer); ++i)
    {
        tempReq[i] = buffer[i];
    }

    struct cacheElement *temp = find(tempReq);
    if (temp != NULL)
    {
        int size = temp->len / sizeof(char);
        int pos = 0;
        char response[MAX_BYTES];
        while (pos < size)
        {
            bzero(response, MAX_BYTES);
            for (int i = 0; i < MAX_BYTES && (pos < size); ++i)
            {
                response[i] = temp->data[pos];
                pos += 1;
            }
            send(socket, response, MAX_BYTES, 0);
        }
        printf("Data retrieved from cache\n");
        printf("%s\n\n", response);
    }
    else if (bytesSendClient > 0) // cache miss --> no element in cache for request
    {
        len = strlen(buffer);
        struct ParsedRequest *request = ParsedRequest_create();

        if (ParsedRequest_parse(request, buffer, len) < 0)
        {
            printf("Error parsing request\n");
        }
        else
        {
            bzero(buffer, MAX_BYTES);
            if (!strcmp(request->method, "GET"))
            {
                if (request->host && request->path && checkHTTPversion(request->version) == 1)
                {
                    bytesSendClient = handleRequest(socket, request, tempReq);
                    if (bytesSendClient == -1)
                    {
                        sendErrorMessage(socket, 500);
                        // Internal Proxy Server Error
                    }
                }
                else
                {
                    sendErrorMessage(socket, 500);
                    // Internal Main Server Error
                }
            }
            else
            {
                printf("This is not a GET request,As of now we support GET requests only.\n");
            }
        }
        ParsedRequest_destroy(request);
    }
    else if (bytesSendClient == 0)
    {
        printf("Client disconnected\n");
    }
    shutdown(socket, SHUT_RDWR);
    close(socket);
    free(buffer);
    sem_post(&semaphore); // release semaphore
    sem_getvalue(&semaphore, p);
    printf("Semaphore value after release: %d\n", p);
    free(tempReq);
    // printf("Thread for socket %d finished execution\n", socket);
    // pthread_exit(NULL);
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
        snprintf(str, sizeof(str), "HTTP/1.1 400 Bad Request\r\nContent-Length: 95\r\nConnection: keep-alive\r\nContent-Type: text/html\r\nDate: %s\r\nServer: VaibhavN/14785\r\n\r\n<HTML><HEAD><TITLE>400 Bad Request</TITLE></HEAD>\n<BODY><H1>400 Bad Rqeuest</H1>\n</BODY></HTML>", currentTime);
        printf("400 Bad Request\n");
        send(socket, str, strlen(str), 0);
        break;
    case 403:
        snprintf(str, sizeof(str), "HTTP/1.1 403 Forbidden\r\nContent-Length: 112\r\nContent-Type: text/html\r\nConnection: keep-alive\r\nDate: %s\r\nServer: VaibhavN/14785\r\n\r\n<HTML><HEAD><TITLE>403 Forbidden</TITLE></HEAD>\n<BODY><H1>403 Forbidden</H1><br>Permission Denied\n</BODY></HTML>", currentTime);
        printf("403 Forbidden\n");
        send(socket, str, strlen(str), 0);
        break;
    case 404:
        snsnprintf(str, sizeof(str), "HTTP/1.1 404 Not Found\r\nContent-Length: 91\r\nContent-Type: text/html\r\nConnection: keep-alive\r\nDate: %s\r\nServer: VaibhavN/14785\r\n\r\n<HTML><HEAD><TITLE>404 Not Found</TITLE></HEAD>\n<BODY><H1>404 Not Found</H1>\n</BODY></HTML>", currentTime);
        printf("404 Not Found\n");
        send(socket, str, strlen(str), 0);
        break;
    case 500:
        snprintf(str, sizeof(str), "HTTP/1.1 500 Internal Server Error\r\nContent-Length: 115\r\nConnection: keep-alive\r\nContent-Type: text/html\r\nDate: %s\r\nServer: VaibhavN/14785\r\n\r\n<HTML><HEAD><TITLE>500 Internal Server Error</TITLE></HEAD>\n<BODY><H1>500 Internal Server Error</H1>\n</BODY></HTML>", currentTime);
        printf("500 Internal Server Error\n");
        send(socket, str, strlen(str), 0);
        break;
    case 501:
        snprintf(str, sizeof(str), "HTTP/1.1 501 Not Implemented\r\nContent-Length: 103\r\nConnection: keep-alive\r\nContent-Type: text/html\r\nDate: %s\r\nServer: VaibhavN/14785\r\n\r\n<HTML><HEAD><TITLE>404 Not Implemented</TITLE></HEAD>\n<BODY><H1>501 Not Implemented</H1>\n</BODY></HTML>", currentTime);
        printf("501 Not Implemented\n");
        send(socket, str, strlen(str), 0);
        break;
    case 505:
        snprintf(str, sizeof(str), "HTTP/1.1 505 HTTP Version Not Supported\r\nContent-Length: 125\r\nConnection: keep-alive\r\nContent-Type: text/html\r\nDate: %s\r\nServer: VaibhavN/14785\r\n\r\n<HTML><HEAD><TITLE>505 HTTP Version Not Supported</TITLE></HEAD>\n<BODY><H1>505 HTTP Version Not Supported</H1>\n</BODY></HTML>", currentTime);
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
    int version = -1;
    if (strncmp(req, "HTTP/1.1", 8) == 0)
    {
        version = 1;
    }
    else if (strncmp(req, "HTTP/1.0", 8) == 0)
    {
        version = 1;
    }
    else
    {
        version = -1;
    }
    return version;
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
    }

    int serverPort = 80; // default port for HTTP main server
    if (request->port != NULL)
    {
        serverPort = atoi(request->port);
    }
    int remoteSocketId = connectRemoteServer(request->host, serverPort);
    if (remoteSocketId < 0)
    {
        return -1;
    }
    // send request(bytes) to remote server
    int bytesSend = send(remoteSocketId, buf, strlen(buf), 0);
    bzero(buf, MAX_BYTES);

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
        bzero(buf, MAX_BYTES);
        bytesSend = recv(remoteSocketId, buf, MAX_BYTES - 1, 0);
    }
    tempBuffer[idx] = '\0'; // buffer end
    free(buf);
    add_cacheElement(tempBuffer, strlen(tempBuffer), tempReq);
    free(tempBuffer);
    close(remoteSocketId);
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
        fprintf(stderr, "Error: No such host exists\n");
        return -1;
    }
    struct sockaddr_in server_addr;
    bzero((char *)&server_addr, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(portNumber);

    bcopy((char *)&host->h_addr, (char *)&server_addr.sin_addr.s_addr, host->h_length);
    if (connect(remoteSocket, (struct sockaddr *)&server_addr, (size_t)sizeof(server_addr)) < 0)
    {
        fprintf(stderr, "Error connecting to remote server %s:%d\n", host_addr, portNumber);
        return -1;
    }
    return remoteSocket;
}

int main(int argc, char *argv)
{
    int clientSocketId;
    int client_len;
    struct sockaddr_in server_addr, client_addr;
    sem_init(&semaphore, 0, MAX_CLIENTS); // min = 0,max = MAX_CLIENTS
    pthread_mutex_init(&lock, NULL);

    // port number given as CLI argument
    if (argv == 2)
    {
        portNumber = atoi(argv[1]); // .proxy 8090 --> proxy portNumber = 8090
    }
    else
    {
        printf("Too few arguments. Usage: ./proxyServerWithCache <port>\n");
        exit(1);
    }

    printf("Starting Proxy Server at port : %d\n", portNumber);

    // main(global) proxy socket
    proxySocketId = socket(AF_INET, SOCK_STREAM, 0);
    if (proxySocketId < 0)
    {
        perror("Socket creation Failed\n");
        exit(1);
    }
    // reuse socket for new requests and thread spawning
    int reuse = 1;
    if (setsocketopt(proxySocketId, SOL_SOCKET, SO_REUSEADDR, (const char *)&reuse, sizeof(reuse)) < 0)
    {
        perror("setsockopt failed\n");
    }

    // cleaning buffer
    bzero((char *)&server_addr, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(portNumber);
    server_addr.sin_addr.s_addr = INADDR_ANY; // bind to all interfaces

    if (bind(proxySocketId, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
    {
        perror("Port not available for binding\n");
        exit(1);
    }
    printf("Proxy Server bound to port %d\n", portNumber);
    int listenStatus = listen(proxySocketId, MAX_CLIENTS);
    if (listenStatus < 0)
    {
        perror("Error in listening on socket\n");
        exit(1);
    }
    printf("Proxy Server is listening for connections...\n");

    int i = 0;
    int connectedSocketId[MAX_CLIENTS]; // connected client socket ids

    while (1)
    {
        bzero((char *)&client_addr, sizeof(client_addr));
        client_len = sizeof(client_addr);
        clientSocketId = accept(proxySocketId, (struct sockaddr *)&client_addr, (socklen_t *)&client_len);
        if (clientSocketId < 0)
        {
            perror("Error accepting client connection\n");
            exit(1);
            // continue; // continue accepting next client
        }
        else
        {
            connectedSocketId[i] = clientSocketId;
            printf("Client connected with socket id: %d\n", clientSocketId);
        }
        struct sockaddr_in *client = (struct sockaddr_in *)&client_addr;
        struct in_addr ipAddr = client->sin_addr;
        char str[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &ipAddr, str, INET_ADDRSTRLEN);
        printf("Client is connected with port: %d and it's IP Adress: %s\n", ntohs(client_addr.sin_port), str);

        pthread_create(&threadId[i], NULL, &thread_fn, (void *)&connectedSocketId[i]);
        i += 1;
    }
    close(proxySocketId);
    return 0;
}

struct cacheElement *find(char *url)
{
    struct cacheElement *site = NULL; // website pointer
    int tempLockVal = pthread_mutex_lock(&lock);
    printf("Remove cache lock acquired: %d\n", tempLockVal);
    if (cacheHead != NULL)
    {
        site = cacheHead;
        while (site != NULL)
        {
            if (!strcmp(site->url, url))
            {
                printf("LRU time track before: %ld\n", site->lruTimeTrack);
                printf("\nCache hit for URL(url found)\n");
                site->lruTimeTrack = time(NULL); // update most recent time accessed
                printf("LRU time track after: %ld\n", site->lruTimeTrack);
                break;
            }
            site = site->next;
        }
    }
    else
    {
        printf("Cache is empty, no URL found\n");
    }
    tempLockVal = pthread_mutex_unlock(&lock); // release lock
    printf("Remove cache lock released\n");
    return site;
}

int add_cacheElement(char *data, int size, char *url)
{
    int tempLockVal = pthread_mutex_lock(&lock);
    printf("Add cache lock acquired: %d\n", tempLockVal);

    int elementSize = size + 1 + strlen(url) + sizeof(struct cacheElement);
    if (elementSize > MAX_ELEMENT_SIZE)
    {
        tempLockVal = pthread_mutex_unlock(&lock);
        printf("Add cache lock released\n");
        return 0; // cannot add element to cache, size exceeds limit
    }
    else
    {
        while (cacheSize + elementSize > MAX_SIZE)
        {
            remove_cacheElement(); // remove LRU element
        }
        struct cacheElement *newElement = (struct cacheElement *)malloc(sizeof(struct cacheElement));
        newElement->data = (char *)malloc(size + 1);
        strcpy(newElement->data, data);
        newElement->url = (char *)malloc((strlen(url) * sizeof(char)) + 1);
        strcpy(newElement->url, url);
        newElement->lruTimeTrack = time(NULL);
        newElement->next = cacheHead;
        newElement->len = size;
        cacheHead = newElement;
        cacheSize += elementSize;
        tempLockVal = pthread_mutex_unloack(&lock); // release lock
        printf("Add cache lock released\n");
        return 1; // element added successfully
    }
    return 0;
}

void remove_cacheElement()
{
    struct cacheElement *p;
    struct cacheElement *q;
    struct cacheElement *temp;

    int tempLockVal = pthread_mutex_lock(&lock);
    printf("Lock acquired\n");
    if (cacheHead != NULL)
    {
        for (q = cacheHead, p = cacheHead, temp = cacheHead; q->next != NULL; q = q->next)
        {
            if (((q->next)->lruTimeTrack) < (temp->lruTimeTrack))
            {
                temp = q->next;
                p = q;
            }
        }
        if (temp == cacheHead)
        {
            cacheHead = cacheHead->next; // remove head element
        }
        else
        {
            p->next = temp->next;
        }
        //  if cache = no empty, then search for the node with least lruTimeTrack and delete it.
        cacheSize -= (temp->len + sizeof(struct cacheElement) + strlen(temp->url) + 1);
        free(temp->data);
        free(temp->url);
        free(temp);
    }
    tempLockVal = pthread_mutex_unlock(&lock);
    printf("cache Lock released\n");
}