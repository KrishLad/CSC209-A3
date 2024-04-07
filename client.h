#ifndef CLIENT_H
#define CLIENT_H

#ifndef MAX_CONNECTIONS
    #define MAX_CONNECTIONS 12
#endif

#ifndef MAX_NAME
    #define MAX_NAME 20
#endif

#ifndef MAX_USER_MSG
    #define MAX_USER_MSG 128
#endif

#ifndef BUF_SIZE
    #define BUF_SIZE MAX_USER_MSG+1
#endif

struct client_sock {
    int sock_fd;
    int state;
    char *username;
    char buf[BUF_SIZE];
    int inbuf;
    struct client_sock *next;
};

/*
 * Add client to list. Return address to new client.
 */
struct client_sock *addclient(struct client_sock **top, int fd);

/*
* Accept connection of new client
*/
int accept_connection(int fd, struct client_sock **clients);

/*
 * Remove client from list. Return 0 on success, 1 on failure.
 * Update curr pointer to the new node at the index of the removed node.
 * Update clients pointer if head node was removed.
 */
int remove_client(struct client_sock **curr, struct client_sock **top);

/*
 * Send a string to a client.
 *
 * Input buffer must contain a NULL-terminated string. The NULL
 * terminator is replaced with a network-newline (CRLF) before
 * being sent to the client.
 *
 * On success, return 0.
 * On error, return 1.
 * On client disconnect, return 2.
 */
int write_buf_to_client(struct client_sock *c, char *buf, int len);

/*
 * Read incoming bytes from client.
 *
 * Return -1 if read error or maximum message size is exceeded.
 * Return 0 upon receipt of CRLF-terminated message.
 * Return 1 if client socket has been closed.
 * Return 2 upon receipt of partial (non-CRLF-terminated) message.
 */
int read_from_client(struct client_sock *curr);

/* Set a client's user name.
 * Returns 0 on success.
 * Returns 1 on either get_message() failure or
 * if user name contains invalid character(s).
 */
int set_username(struct client_sock *curr);

/* Find the current 2 players that should be playing the game
*/
void find_players(struct client_sock *top, struct client_sock **p1, struct client_sock **p2);

/* Play the game
* Return 0 if the game does not work and 1 if the game works
*/
int play_game(struct client_sock *top, struct client_sock *p1, struct client_sock *p2, fd_set all_fds);

#endif