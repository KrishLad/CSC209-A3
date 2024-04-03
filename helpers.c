#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <errno.h>
#include <arpa/inet.h>     /* inet_ntoa */
#include <netdb.h>         /* gethostname */
#include <netinet/in.h>    /* struct sockaddr_in */

#include "helpers.h"

void setup_server_socket(struct listen_sock *s) {
    if(!(s->addr = malloc(sizeof(struct sockaddr_in)))) {
        perror("malloc");
        exit(1);
    }
    // Allow sockets across machines.
    s->addr->sin_family = AF_INET;
    // The port the process will listen on.
    s->addr->sin_port = htons(SERVER_PORT);
    // Clear this field; sin_zero is used for padding for the struct.
    memset(&(s->addr->sin_zero), 0, 8);
    // Listen on all network interfaces.
    s->addr->sin_addr.s_addr = INADDR_ANY;

    s->sock_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (s->sock_fd < 0) {
        perror("server socket");
        exit(1);
    }

    // Make sure we can reuse the port immediately after the
    // server terminates. Avoids the "address in use" error
    int on = 1;
    int status = setsockopt(s->sock_fd, SOL_SOCKET, SO_REUSEADDR,
        (const char *) &on, sizeof(on));
    if (status < 0) {
        perror("setsockopt");
        exit(1);
    }

    // Bind the selected port to the socket.
    if (bind(s->sock_fd, (struct sockaddr *)s->addr, sizeof(*(s->addr))) < 0) {
        perror("server: bind");
        close(s->sock_fd);
        exit(1);
    }

    // Announce willingness to accept connections on this socket.
    if (listen(s->sock_fd, MAX_BACKLOG) < 0) {
        perror("server: listen");
        close(s->sock_fd);
        exit(1);
    }
}

int find_network_newline(const char *buf, int inbuf) {
    for (int i = 0; i < inbuf - 1; i++) {
        if (buf[i] == '\r' && buf[i+1] == '\n') {
            return i + 2;
        }
    }
    return -1; 
}

int read_from_socket(int sock_fd, char *buf, int *inbuf) {
    /*
    start by reading the next bit into the buffer
    second param: use pointer arithmetic for starting point
    third param: only put amount that can fit into the buffer
    */
    int next_bytes = read(sock_fd, buf + *inbuf, BUF_SIZE - *inbuf - 1);
    if (next_bytes < 0) { // error reading from socket
        perror("read");
        return -1;
    } else if (next_bytes == 0) { //socket is closed
        return 1;
    }
    *inbuf += next_bytes; //update value of inbuf

    // Ensure null-termination for string operations; reserve 1 byte in buffer
    buf[*inbuf] = '\0';

    // Check if we have a full message (look for "\r\n")
    if (find_network_newline(buf, *inbuf) > 0) {
        return 0; // Full message ready
    } else {
        return 2; // Partial message; need more data
    }
}

int get_message(char **dst, char *src, int *inbuf) {
    int location = find_network_newline(src, *inbuf);
    if (location == -1) {
        return 1;
    }
    //if location > 0, it is actually the length of the message to print
    *dst = malloc(location-1);
    strncpy(*dst, src, location - 2);
    (*dst)[location-2]='\0';
    
    memmove(src, src + location, *inbuf - location);
    *inbuf -= location; // Adjust the buffer size

    return 0;
}

int write_to_socket(int sock_fd, char *buf, int len) {
    int total_written = 0; // Total bytes written so far
    while (total_written < len) {
        int written = write(sock_fd, buf + total_written, len - total_written);
        if (written == -1) {
            perror("write to socket"); // Print error message
            if (errno == EPIPE) {
                // The socket is closed by the peer before all data could be sent
                return 2; // Indicate disconnect
            }
            return 1; // Indicate error
        }
        total_written += written;
    }
    return 0; // Success
}
