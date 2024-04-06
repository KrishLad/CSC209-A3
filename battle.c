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

            find_players(clients, &p1, &p2);
        }

        if (sigint_received) break;

        /*
         * Accept incoming messages from clients,
         * and send to all other connected clients.
         */
        struct client_sock *curr = clients;

        while (curr) {
            if (!FD_ISSET(curr->sock_fd, &listen_fds)) {
                curr = curr->next;
                continue;
            }
            int client_closed = read_from_client(curr);

            // If error encountered when receiving data
            if (client_closed == -1) {
                client_closed = 1; // Disconnect the client
            }

            // If received at least one complete message
            // and client is newly connected: Get username
            if (client_closed == 0 && curr->username == NULL) {
                if (set_username(curr)) {
                    printf("Error processing user name from client %d.\n", curr->sock_fd);
                    client_closed = 1; // Disconnect the client
                }
                else {
                    printf("Client %d user name is %s.\n", curr->sock_fd, curr->username);
                    curr->state = 1; //player is ready to play

                    find_players(clients, &p1, &p2);
                }
            }

            //FROM HERE DOWN SHOULD BE IN GAME LOGIC! MEANING BEFORE THIS WE NEED TO FIND GAME STATE AND START GAME. IN WHILE LOOP?

            // char *message = "(a) Regular move\n(p) Power move\n(s) Say something\n";
            // // Loop through buffer to get complete message(s)
            // char msg_buf[BUF_SIZE];
            // memset(msg_buf, 0, BUF_SIZE); 
            // strncat(msg_buf, message, MAX_USER_MSG);

            // char *msg;
            // while (client_closed == 0 && !get_message(&msg, curr->buf, &(curr->inbuf))) {
            //     printf("Echoing message from %s: %s\n", curr->username, msg);
            //     char write_buf[BUF_SIZE];
            //     memset(write_buf, 0, BUF_SIZE);
            //     // write_buf[0] = '\0';
            //     strncat(write_buf, curr->username, MAX_NAME);
            //     strncat(write_buf, ": ", MAX_NAME);
            //     strncat(write_buf, msg, MAX_USER_MSG);
            //     strncat(write_buf, "\n", MAX_USER_MSG);
            //     free(msg);
            //     int data_len = strlen(write_buf);

            //     struct client_sock *dest_c = clients;
            //     while (dest_c) {
            //         if (dest_c != curr) {
            //             int ret = write_buf_to_client(dest_c, write_buf, data_len);
            //             if (ret == 0) {
            //                 printf("Sent message from %s (%d) to %s (%d).\n",
            //                     curr->username, curr->sock_fd,
            //                     dest_c->username, dest_c->sock_fd);
            //             }
            //             else {
            //                 printf("Failed to send message to user %s (%d).\n", dest_c->username, dest_c->sock_fd);
            //                 if (ret == 2) {
            //                     printf("User %s (%d) disconnected.\n", dest_c->username, dest_c->sock_fd);
            //                     close(dest_c->sock_fd);
            //                     FD_CLR(dest_c->sock_fd, &all_fds);
            //                     assert(remove_client(&dest_c, &clients) == 0); // If this fails we have a bug
            //                     continue;
            //                 }
            //             }
            //         }
            //         dest_c = dest_c->next;
            //     }
            // }

            if (client_closed == 1) { // Client disconnected
                // Note: Never reduces max_fd when client disconnects
                FD_CLR(curr->sock_fd, &all_fds);
                close(curr->sock_fd);
                printf("Client %d disconnected\n", curr->sock_fd);
                assert(remove_client(&curr, &clients) == 0); // If this fails we have a bug
            }
            else {
                curr = curr->next;
            }
        }

        //play the game!
        // if (p1 != NULL && p2 != NULL) {
        //     int power1 = 20;
        //     int power2 = 20;
        //     struct client_sock *curr_player = p1;
        //     int client_closed;
        //     char *message = "(a) Regular move\n(p) Power move\n(s) Say something\n";
        //     char *tryagain = "Try again.\n(a) Regular move\n(p) Power move\n(s) Say something\n";
        //     char terminated[BUF_SIZE];
        //     while (power1 > 0 || power2 > 0) {
                
        //         //send the message to the player
        //         write(curr_player->sock_fd, message, strlen(message));

        //         //while we run into an error getting the message, re prompt
        //         client_closed = read_from_client(curr_player);
        //         while (client_closed == -1 || client_closed == 2) {
        //             write(curr_player->sock_fd, tryagain, strlen(tryagain));
        //         }
        //         if (client_closed == 1) { //the socket has been closed. 
        //             //let the other player know
        //             memset(terminated, 0, BUF_SIZE);
        //             sprintf(terminated, "Player %d: %s has disconnected.", curr_player->sock_fd, curr->username);
        //             if (curr_player == p1) {
        //                 write(p2->sock_fd, terminated, strlen(terminated));
        //             } else {
        //                 write(p1->sock_fd, terminated, strlen(terminated));
        //             }
        //         } else { //message is successful and CLRF terminated
        //             char *move;
        //             int err = get_message(&move, curr_player->buf, &(curr_player->inbuf));
        //             if (err == 0) {
        //                 printf("MOVE SELECTED: %s",move);
        //             }
        //         }

        //     power1 -= 5;
        //     power2 -= 5;
        //     }
        // }


    } while (!sigint_received);

    clean_exit(s, clients, exit_status);

}