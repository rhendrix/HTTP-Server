# Simple HTTP Server

This project is a simple http server for linux written in C using POSIX sockets and threads. 

## Building 

If make and gcc are already installed, the server can be built by simply calling `make`. Otherwise first, install make and gcc.

## Running

The server is called without any arguments in the directory where the server resources (html, css, js, videos, images, etc) are located. In the case of the test directory this would be:

```C
sudo ../server
```

Note that the server will likely need to be called with sudo, because of the low port nunber (80).
