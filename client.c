#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <errno.h>
#include <signal.h>
#include <assert.h>

#include "client.h"
#include "helpers.h"

int write_buf_to_client(struct client_sock *c, char *buf, int len) {
    if (len > 0 && buf[len-1] == '\0') { // Check if the last character is null terminator
        buf[len-1] = '\r'; // Replace null terminator with carriage return

        // Ensure there's space for one more character
        if (len < BUF_SIZE) {
            len += 1;
        }

        buf[len] = '\n';
    }
    return write_to_socket(c->sock_fd, buf, len);
}

struct client_sock *addclient(struct client_sock **top, int fd) {
    // Allocate memory for the new client
    struct client_sock *new_client = malloc(sizeof(struct client_sock));
    if (new_client == NULL) {
        // Memory allocation failed
        perror("Failed to allocate memory for new client");
        exit(EXIT_FAILURE); // Or handle error as appropriate
    }

    // Initialize the new client's fields
    new_client->sock_fd = fd;       // Set file descriptor
    new_client->state = 0;          // Initial state, adjust as necessary
    new_client->username = NULL;    // Username not set yet
    memset(new_client->buf, 0, BUF_SIZE); // Clear the buffer
    new_client->inbuf = 0;          // No data in buffer yet
    new_client->next = NULL;        // Next client not known yet

    // Insert the new client at the start of the linked list
    if (*top == NULL) {
        // List is empty, new client is now the head
        *top = new_client;
    } else {
        // List is not empty, insert new client at the end
        struct client_sock *i = *top;
        while (i != NULL && i->next != NULL) {
            i = i->next;
        }
        i->next = new_client;
    }

    return new_client; // Return the address of the new client
}

int accept_connection(int fd, struct client_sock **clients) {

    // setting up connection
    struct sockaddr_in peer;
    unsigned int peer_len = sizeof(peer);
    peer.sin_family = AF_INET;

    //counting the num of clients. if too many, error. 
    int num_clients = 0;
    struct client_sock *curr = *clients;
    while (curr != NULL && num_clients < MAX_CONNECTIONS && curr->next != NULL) {
        curr = curr->next;
        num_clients++;
    }

    if (num_clients < MAX_CONNECTIONS) { 
        //accept connection
        int client_fd = accept(fd, (struct sockaddr *)&peer, &peer_len);

        if (client_fd < 0) {
            perror("server: accept");
            return -1;
        }

        //create client
        addclient(clients, client_fd);
        
        return client_fd;

    } else {
        return -1;
    }

}

int remove_client(struct client_sock **curr, struct client_sock **clients) {
    if (curr == NULL || *curr == NULL || clients == NULL || *clients == NULL) {
        // Invalid pointers or empty list, cannot proceed
        return 1;
    }

    struct client_sock *temp = *clients;
    struct client_sock *prev = NULL;

    // If the client to remove is the head of the list
    if (temp == *curr) {
        *clients = temp->next; // Head now points to the next client
        free(temp->username);  // Free dynamically allocated username
        free(temp);            // Free the client struct itself
        *curr = *clients;      // Update curr to point to the new head (or NULL if list is now empty)
        return 0;              // Success
    }

    // Find the client to remove, keeping track of the previous client
    while (temp != NULL && temp != *curr) {
        prev = temp;
        temp = temp->next;
    }

    // If the client was not found in the list
    if (temp == NULL) {
        return 1; // Client not found
    }

    // Client found, remove it from the list
    if (prev != NULL) {
        prev->next = temp->next; // Bypass the client to be removed
    }

    // Free the removed client's resources
    free(temp->username);
    free(temp);

    // If there's a next client, update curr to point to it
    if (prev != NULL && prev->next != NULL) {
        *curr = prev->next;
    } else {
        // Otherwise, we removed the last client, so update curr to NULL
        *curr = NULL;
    }

    return 0; // Success
}

/*
 * Read incoming bytes from client.
 *
 * Return -1 if read error or maximum message size is exceeded.
 * Return 0 upon receipt of CRLF-terminated message.
 * Return 1 if client socket has been closed.
 * Return 2 upon receipt of partial (non-CRLF-terminated) message.
 */
int read_from_client(struct client_sock *curr) {
    return read_from_socket(curr->sock_fd, curr->buf, &(curr->inbuf));
}

/* Set a client's user name.
 * Returns 0 on success.
 * Returns 1 on either get_message() failure or
 * if user name contains invalid character(s).
 */
int set_username(struct client_sock *curr) {

    int r = get_message(&(curr->username), curr->buf, &(curr->inbuf));
    if (r == 1) {
        return 1;
    } 

    for(int i = 0; i < strlen(curr->username); i++) {
        if (strcmp(curr->username, " ")==0) { //username contains invalid characters
            return 1;
        }
    }
    return 0;

}

void find_players(struct client_sock *top, struct client_sock **p1, struct client_sock **p2) {

    struct client_sock *i = top;
    //finding player 1, if it exists
    while (i != NULL) {
        printf("Name: %s State: %d\n", i->username, i->state);

        if ((i->state == 1 || i->state == 2) && *p1 == NULL) {
            *p1 = i;
            printf("Player 1: %s\n", (*p1)->username);
        }
        else if ((i->state == 1 || i->state == 2) && *p2 == NULL && *p1 != i) {
            *p2 = i;
            printf("Player 1: %s\n", (*p1)->username);
        }
        i = i->next;
    }

    printf("Finding players.\n");
    if (*p1 == NULL) {
        printf("Player 1: None\n");
    } else {
        printf("Player 1: %s\n", (*p1)->username);
    }
    if (*p2 == NULL) {
        printf("Player 2: None\n");
    } else {
        printf("Player 2: %s\n", (*p2)->username);
    }

}
