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

    while (i != NULL) {

        if ((i->state == 1 || i->state == 2) && *p1 == NULL) {
            *p1 = i;
        }
        else if ((i->state == 1 || i->state == 2) && *p2 == NULL && *p1 != i) {
            if ( ((*p1)->state == 2 && i->state == 1) || ((*p1)->state == 1)) {
                *p2 = i;
            }
        }

        i = i->next;
    }
}

int play_game(struct client_sock *top, struct client_sock *p1, struct client_sock *p2, fd_set *all_fds) {

    //so that the buffer is empty whenever we start a new game
    memset(p1->buf, 0, BUF_SIZE);
    memset(p2->buf, 0, BUF_SIZE);
    p1->inbuf = 0;
    p2->inbuf = 0;

    //want the hitpoints of each player to be at least 20
    int power1 = 20 + (rand() % 5);
    int power2 = 20 + (rand() % 5);
    //want each player to have at least 1 power move
    int powermoves1 = 1 + (rand() % 4);
    int powermoves2 = 1 + (rand() % 4);
    // healing moves for both players. min 1, max 3
    int healsp1 = 1 + (rand() % 3);
    int healsp2 = 1 + (rand() % 3);

    struct client_sock *player = p1;
    struct client_sock *waiter = p2;
    char *message = "(a) Regular move\n(p) Power move\n(s) Say something\n(h) Heal yourself\n";
    int client_closed = 0;
    int waiter_closed = 0;
    char *move;

    //send welcome messages to players
    char welcome_player1[BUF_SIZE];
    sprintf(welcome_player1, "Welcome! You are playing %s.\nYour hitpoints: %d\nYour powermoves: %d\n", waiter->username, power1, powermoves1);
    write_buf_to_client(player, welcome_player1, strlen(welcome_player1));

    char welcome_player2[BUF_SIZE];
    sprintf(welcome_player2, "Welcome! You are playing %s.\n", player->username);
    write_buf_to_client(waiter, welcome_player2, strlen(welcome_player2));
    
    int max_health_p1 = power1;
    int max_health_p2 = power2; 
    //turn logic
    while (power1 > 0 && power2 > 0) {

        //reduces the number of if - else statements we have to use
        int *player_power, *waiter_power;
        int *waiter_moves, *player_moves;
        int *player_heals, *waiter_heals;
        int *player_max;
        if (player == p1) {
            player_power = &power1;
            waiter_power = &power2;
            waiter_moves = &powermoves2;
            player_moves = &powermoves1;
            player_heals = &healsp1;
            waiter_heals = &healsp2;
            player_max = &max_health_p1;
        } else {
            player_power = &power2;
            waiter_power = &power1;
            waiter_moves = &powermoves1;
            player_moves = &powermoves2;
            player_heals = &healsp2;
            waiter_heals = &healsp2;
            player_max = &max_health_p2;
        }

        //send information to the waiter
        char waiter_msg[BUF_SIZE];
        sprintf(waiter_msg, "\nYour hitpoints: %d\nYour powermoves: %d\nYour healing moves: %d\n\n%s's hitpoints: %d\n",*waiter_power, *waiter_moves,*waiter_heals,player->username, *player_power);
        write_buf_to_client(waiter, waiter_msg, strlen(waiter_msg));

        //send prompt to the player
        write_buf_to_client(player, message, strlen(message));

        //check if available for reading
        client_closed = read_from_client(player);
        printf("Read from client returned %d\n",client_closed);

        waiter_closed = read_from_client(waiter);
        if (waiter_closed == 1){
            FD_CLR(waiter->sock_fd, all_fds);
            close(waiter->sock_fd);


            char disconnect_msg[BUF_SIZE];
            sprintf(disconnect_msg, "--%s dropped. You win!", waiter->username);
            write_buf_to_client(player, disconnect_msg, strlen(disconnect_msg));

            remove_client(&waiter, &top);

            return 0;
        }
        //check message 
        //handle case where we can't read
        if (client_closed == -1 || client_closed == 1) {

            FD_CLR(player->sock_fd, all_fds);
            close(player->sock_fd);
            
            char close_msg[BUF_SIZE];
            sprintf(close_msg, "--%s dropped. You win!", player->username);
            write_buf_to_client(waiter, close_msg, strlen(close_msg));

            remove_client(&player, &top);

            return 0;

        } else if (client_closed == 2 || client_closed == 0) {

            //we add a network newline
            if (strlen(player->buf) == 1) {
                player->buf[1] = '\r';
                player->buf[2] = '\n';
                player->inbuf += 2;
            }
            else {
                memset(player->buf, 0, BUF_SIZE);
                player->inbuf = 0;
                continue;
            }
            
            //read the message
            int err = get_message(&move, player->buf, &(player->inbuf));

            if (err == 1) { //can't read the message
                char *msg_error = "Could not get message.\n";
                write_buf_to_client(player, msg_error, strlen(msg_error));

                break;
            } else { 
                if (strcmp(move, "a") == 0) {

                    int deduc = rand() % 6;
                    *waiter_power -= deduc;
                    char hit_msg[BUF_SIZE];
                    sprintf(hit_msg, "You hit %s for %d points.\n",waiter->username, deduc);
                    write_buf_to_client(player, hit_msg, strlen(hit_msg));

                } else if(strcmp(move, "p") == 0) {

                    int hit_or_not = rand() % 3;
                    int deduc = 10 + (rand() % 10);

                    if ((*player_moves) <= 0) {
                        if ((*player_heals)<= 0){
                            message = "(a) Regular move\n(s) Say something\n";
                            continue;
                        }
                        else{
                            message = "(a) Regular move\n(s) Say something\n(h) Heal yourself\n";
                            continue;
                        }   
                    } else {
                        if (hit_or_not == 1) { //powermove hits
                            *waiter_power -= deduc;
                            char hit_msg2[BUF_SIZE];
                            sprintf(hit_msg2, "You hit %s for %d points.\n",waiter->username, deduc);
                            write_buf_to_client(player, hit_msg2, strlen(hit_msg2));
                        } else {
                            char *missed_msg = "You missed.\n";
                            write_buf_to_client(player, missed_msg, strlen(missed_msg));
                        }
                        
                        *player_moves -= 1;
                    }

                } else if(strcmp(move, "s") == 0) {

                    char *prompt = "Type message: ";
                    write_buf_to_client(player, prompt, strlen(prompt));

                    int msg_okay = 2;
                    while (msg_okay == 2) {
                        msg_okay = read_from_client(player);
                    }

                    char *newline_pos = strchr(player->buf, '\n');
                    if (newline_pos != NULL) {
                        // Found a newline, check if it's already part of a network newline
                        if (newline_pos > player->buf && *(newline_pos - 1) != '\r') {
                            *newline_pos = '\r'; // Replace '\n' with '\r'
                            // Ensure there's enough space to shift and insert '\n'
                            memmove(newline_pos + 2, newline_pos + 1, strlen(newline_pos + 1) + 1);
                            player->inbuf += 1;
                            *(newline_pos + 1) = '\n'; // Insert '\n' after '\r'
                        }
                    }

                    char *msg_to_send;
                    get_message(&msg_to_send, player->buf, &(player->inbuf));
                    write_to_socket(waiter->sock_fd, msg_to_send, strlen(msg_to_send));


                }
                else if(strcmp(move , "h") == 0){ // if the player selects a healing move.
                    int value =1+ (rand() % 10); // I just choose the first number off the top of my head.
                    if((*player_power) == (*player_max)){ // Will not allow them to heal at full health 
                        char *out = "You are at full health, you cannot use a heal\n"; 
                        write_buf_to_client(player,out,strlen(out));
                        continue;
                    } 
                    else if ((*player_heals)  <= 0){
                       if ((*player_moves) <= 0){ 
                           message = "(a) Regular move\n(s) Say something\n";
                           continue;
                       } else{
                           message = "(a) Regular move\n(p) Power move \n(s) Say something\n";
                           continue;
                       }
                    }
                    else{
                        int check = (*player_power) + value; 
                        while(check > (*player_max)){
                            value = 1 + (rand() % 10); //reroll the number.
                            check = (*player_power) + value; 
                        }
                        *player_power += value;
                        char healing_msg[16]; 
                        sprintf(healing_msg, "You healed %d HP", value);
                        write_buf_to_client(player, healing_msg, strlen(healing_msg));
                        *player_heals -= 1;
                    }
                }
                else if (strcmp(move, "a") != 0 && strcmp(move, "p") != 0 && strcmp(move, "s") != 0 && strcmp(move, "h") != 0)
                {

                    char *inp_error = "\nNot a valid move.\n";
                    write_buf_to_client(player, inp_error, strlen(inp_error));
                }
            }
        }

        if ((*player_heals > 0) && (*player_power <=0)){
            message = "(a) Regular move\n(s) Say something\n(h) Heal yourself\n"; 
        }
        else if ((*player_heals <= 0) && (*player_power > 0)){
            message = "(a) Regular move\n(p) Power move \n(s) Say something\n"; 

        }
        else if ((*player_heals <= 0) && (*player_power <= 0)){
            message = "(a) Regular move\n(p) Power move \n(s) Say something\n";
        }

        //check who is winning / losing
        char *winner = "You won!\n";
        char *loser = "You lost.\n";
        if (power1 <= 0) {
            write_buf_to_client(p1, loser, strlen(loser));
            write_buf_to_client(p2, winner, strlen(winner));

            return 1;
        } else if (power2 <= 0) {
            write_buf_to_client(p1, winner, strlen(winner));
            write_buf_to_client(p2, loser, strlen(loser));

            return 1;
        }
        
        if ( (strcmp(move, "a") == 0 || strcmp(move, "p") == 0 || strcmp(move, "h") == 0) && player == p1) {
            waiter = p1;
            player = p2;
        } else if ( (strcmp(move, "a") == 0 || strcmp(move, "p") == 0 || strcmp(move, "h") == 0) && player == p2 ){
            waiter = p2;
            player = p1;
        }
    }

    return 0; 

}
