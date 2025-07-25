<h1>Threaded HTTP Proxy Server with Caching</h1>

## flow diagram

![Proxy Server Diagram](https://github.com/gt951436/threaded-proxy-server/blob/main/images/UML.JPG)

A multi-threaded HTTP proxy server with LRU caching implemented in C. This proxy handles concurrent client requests, forwards GET requests to remote servers, caches responses, and serves cached content for subsequent identical requests.

# Note:

- This code is only tested on Windows OS as of now,however code is implemented to run on both Linux and Windows Machine.
- Do not forget to configure your browser for your system to act as proxy server.
- Please disable your browser cache before running the proxy server.

## Features

- 🚀 **Multi-threaded architecture** handles up to 10(can be as changed as per requirements) concurrent clients
- 💾 **LRU caching** with configurable size (default: 200MB)
- ⚡ **HTTP/1.0 & HTTP/1.1** protocol support
- 📦 **GET request handling** with proper HTTP parsing
- 🔒 **Thread synchronization** using semaphores and mutexes
- 📊 **Detailed logging** of proxy operations
- 🌐 **Cross-platform** : works on Windows and Linux (not tested on Linux system as of now)

## How did we implement Multi-threading?

- Used Semaphore instead of Condition Variables and pthread_join() and pthread_exit() function.
- pthread_join() requires us to pass the thread id of the the thread to wait for.
- Semaphore’s sem_wait() and sem_post() doesn’t need any parameter. So it is a better choice.

## Motivation for Project

- To Understand →
  - The working of requests from our local computer to the server.
  - The handling of multiple client requests from various clients.
  - Locking procedure for concurrency.
  - The concept of cache and its different functions that might be used by browsers.
- Proxy Server do →
  - It speeds up the process and reduces the traffic on the server side.
  - It can be used to restrict user from accessing specific websites.
  - A good proxy will change the IP such that the server wouldn’t know about the client who sent the request.
  - Changes can be made in Proxy to encrypt the requests, to stop anyone accessing the request illegally from your client.

## OS concepts used: ​

- Threading
- Locks
- Semaphore
- Cache (LRU algorithm (using linked list))

## Future possible extensions: ​

- Code can be implemented using multiprocessing that can speed up the process with parallelism.
- We can decide which type of websites should be allowed and which type should not be allowed.
- Code can be extended to handle requests like POST.
- Add cache persistence to disk
- Support HTTP/2 protocol
- Add admin interface for monitoring
- Implement cache validation with ETags

## Requirements

- **Compiler**: GCC (MinGW64 on Windows)
- **Libraries**:
  - pthreads (included with GCC)
  - Winsock2 (Windows only, linked via `-lws2_32`)
  - Ws2tcpip
- **Tools**: make (optional)

## Building the Proxy Server

## Windows

```bash
gcc -g -Wall -c proxyServerWithCache.c -o proxyServerWithCache.o
gcc -g -Wall -c proxy_parse.c -o proxy_parse.o
gcc proxyServerWithCache.o proxy_parse.o -o proxy.exe -lpthread -lws2_32
```

### Running the proxy

```bash
.\proxy.exe <port>
```

`Open : http://localhost:port/https://www.cs.princeton.edu/`

## Author

```
    Garv Tyagi - 2201003CS
```
