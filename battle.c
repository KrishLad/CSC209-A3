#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <assert.h>
#include <sys/socket.h>
#include <netinet/in.h>    /* Internet domain header */
#include <arpa/inet.h>     /* only needed on mac */

#include "helpers.h"
#include "client.h"

int sigint_received = 0;

void sigint_handler(int code) {
    sigint_received = 1;
}

/*
 * Close all sockets, free memory, and exit with specified exit status.
 */
void clean_exit(struct listen_sock s, struct client_sock *clients, int exit_status) {
    struct client_sock *tmp;
    while (clients) {
        tmp = clients;
        close(tmp->sock_fd);
        clients = clients->next;
        free(tmp->username);
        free(tmp);
    }
    close(s.sock_fd);
    free(s.addr);
    exit(exit_status);
}

int main() {

    // This line causes stdout not to be buffered.
    // Don't change this! Necessary for autotesting.
    setbuf(stdout, NULL);

    /*
     * Turn off SIGPIPE: write() to a socket that is closed on the other
     * end will return -1 with errno set to EPIPE, instead of generating
     * a SIGPIPE signal that terminates the process.
     */
    if (signal(SIGPIPE, SIG_IGN) == SIG_ERR) {
        perror("signal");
        exit(1);
    }

    // Linked list of clients
    struct client_sock *clients = NULL;

    struct listen_sock s;
    setup_server_socket(&s);

    // Set up SIGINT handler
    struct sigaction sa_sigint;
    memset (&sa_sigint, 0, sizeof (sa_sigint));
    sa_sigint.sa_handler = sigint_handler;
    sa_sigint.sa_flags = 0;
    sigemptyset(&sa_sigint.sa_mask);
    sigaction(SIGINT, &sa_sigint, NULL);
 
    int exit_status = 0;
    int max_fd = s.sock_fd;

    //setup file descriptor sets
    fd_set all_fds, listen_fds;
    FD_ZERO(&all_fds);
    FD_SET(s.sock_fd, &all_fds);

    //setup players
    struct client_sock *p1 = NULL;
    struct client_sock *p2 = NULL;

    do {
        listen_fds = all_fds;
        int nready = select(max_fd + 1, &listen_fds, NULL, NULL, NULL);
        if (sigint_received) break;
        if (nready == -1) {
            if (errno == EINTR) continue;
            perror("server: select");
            exit_status = 1;
            break;
        }

        /*
         * If a new client is connecting, create new
         * client_sock struct and add to clients linked list.
         */
        if (FD_ISSET(s.sock_fd, &listen_fds)) {
            int client_fd = accept_connection(s.sock_fd, &clients);
            if (client_fd < 0) {
                printf("Failed to accept incoming connection.\n");
                continue;
            }
            if (client_fd > max_fd) {
                max_fd = client_fd;
            }
            FD_SET(client_fd, &all_fds);
            printf("Accepted connection\n");

            char *question = "What is your name? ";
            write(client_fd, question, strlen(question));

        }

        if (sigint_received) break;

        struct client_sock *curr = clients;

        while (curr) {

            if (!FD_ISSET(curr->sock_fd, &listen_fds)) {
                curr = curr->next;
                continue;
            }

            int client_closed = 2;
            while (client_closed == 2) {
                client_closed = read_from_client(curr);
            }

            // If error encountered when receiving data
            if (client_closed == -1) {
                client_closed = 1; // Disconnect the client
            }

            if (client_closed == 0  && curr->username == NULL) {

                char *newline_pos = strchr(curr->buf, '\n');
                if (newline_pos != NULL) {
                    // Found a newline, check if it's already part of a network newline
                    if (newline_pos > curr->buf && *(newline_pos - 1) != '\r') {
                        *newline_pos = '\r'; // Replace '\n' with '\r'
                        // Ensure there's enough space to shift and insert '\n'
                        memmove(newline_pos + 2, newline_pos + 1, strlen(newline_pos + 1) + 1);
                        curr->inbuf += 1;
                        *(newline_pos + 1) = '\n'; // Insert '\n' after '\r'
                    }
                }

                // Here you should already have the complete username in curr->buf
                if (!set_username(curr)) {
                    printf("Username set successfully: %s\n", curr->username);
                    char message[40];
                    sprintf(message, "Welcome %s! Awaiting opponent...\n", curr->username);
                    write(curr->sock_fd, message, sizeof(message));
                    curr->state = 1;
                } else {
                    printf("Failed to set username.\n");
                }

            }

            if (client_closed == 1) { // Client disconnected
                // Note: Never reduces max_fd when client disconnects
                FD_CLR(curr->sock_fd, &all_fds);
                close(curr->sock_fd);

                // //alert all other clients that a player has left
                // struct client_sock *rec2 = clients;
                // while (rec2) {
                //     if (rec2 != curr) {
                //         char left_msg[BUF_SIZE];
                //         sprintf(left_msg, "%s left the arena.\n", curr->username);
                //         write_buf_to_client(rec2, left_msg, strlen(left_msg));
                //     }
                //     rec2 = rec2->next;
                // }

                remove_client(&curr, &clients);
            }
            else {
                
                curr = curr->next;
            }
        }

        /*
        * GAME LOGIC
        */

        // printf("ALL PLAYERS\n");
        // struct client_sock *printer = clients;
        // while (printer) {
        //     printf("%s %d\n", printer->username, printer->state);
        //     printer = printer->next;
        // }

        find_players(clients, &p1, &p2);

        //play the game
        int game = 0;
        if (p1 != NULL && p2 != NULL) {
            game = play_game(clients, p1, p2, &all_fds);
        }

        //update states: these two just played together so they can't play again.
        if (game == 1) {
            //set every player except the two who just played state's to be 1
            struct client_sock *i = clients;
            while (i != NULL) {
                i->state = 1;
                i = i->next;
            }
            p1->state = 2;
            p2->state = 2;
            
            p1 = NULL;
            p2 = NULL;
        } else {
            p1 = NULL;
            p2 = NULL;
        }

    } while (!sigint_received);

    clean_exit(s, clients, exit_status);

}
