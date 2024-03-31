#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <arpa/inet.h>


//Define the port on make file
#ifndef PORT
//Using My (Krishna's) Student Number as the default port. (We can change this with the make file as we wish)
#define PORT 54640 
#endif

/*Struct to handle the messages that the game sends (such as the information board or if a mfer wants to say something)*/
struct message{
  char message_buffer[256];
  int room;
  char *after;
  int inbuf;

  char command_buffer[100];
  int command_room;
  char* command_after;
  int command_inbuf;  
};

/*struct to handle matches that a client is currently in*/
struct match{
    struct client *opponent;
    int past_fd;
    int in_match;
    int hp;
    int powermoves;
};

// make a client struct. 
struct client{ 
    int fd; //file descriptor for the client
    int has_something_to_say; //boolean to indicate a client a match wants to speak 
    char name[256];
    int turn;
    struct in_addr ipaddr;
    struct client *next;
    struct message message;
    struct match match;
};

int bind_and_listen(void){
    struct sockaddr_in server; 
    int listenfd;
    
    //sets up a socket (still doesn't have binding yet)
    if((listenfd = socket(AF_INET, SOCK_STREAM, 0)) < 0){
        perror("socket");
        exit(1);
    }

    // Make sure the the server the OS lets go of the port
    int yes = 1;
    if ((setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int))) == -1)
    {
        perror("setsockopt");
    }

    // Setting up server for binding
    memset(&server, '\0', sizeof(server));
    server.sin_family = AF_INET;
    server.sin_addr.s_addr = INADDR_ANY;
    server.sin_port = htons(PORT); //if there is an error its because this will be defined on compile time

    //Binding the socket to an address.
    if (bind(listenfd, (struct sockaddr *)&server, sizeof(server))){
        perror("bind");
        exit(1);
    }

    if (listen(listenfd,5)){
        perror("listen");
        exit(1);
    }

    return listenfd;
}

static struct client *addclient (struct client *top, int fd, struct in_addr addr){
    int error;
    struct client *tmp = top;
    // make space for new client
    struct client *p = malloc(sizeof(struct client));
    if (!p)
    {
        perror("malloc");
        exit(1);
    }

    printf("Adding client %s\n", inet_ntoa(addr));
    
    /*Initialize message struct */
    (p->message).room = sizeof((p->message).message_buffer);
    (p->message).inbuf  = 0;
    (p->message).after = (p->message).message_buffer;

    (p->message).command_room = sizeof((p->message).command_buffer);
    (p->message).command_inbuf = 0;
    (p->message).command_after = (p->message).command_buffer;

    
    /*Everything else for the client struct*/
    p->fd = fd;
    p->ipaddr = addr;
    p->next = top;
    p->has_something_to_say = 0;
    p->turn = 0;
    (p->name)[0] = '\0';

    /*Initialize match struct*/
    (p->match).opponent = NULL;
    (p->match).past_fd = p->fd;
    (p->match).in_match = 0;
    (p->match).hp = 0;
    (p->match).powermoves = 0;

    const char *welcome_message = "Welcome to the realm of magic and wonder! Pray tell, traveler, what name doth thou bear? For in this mystical land, every soul's name holds the key to unlocking the enchantments that await. 🌟🔮\r\n";


    error = write(fd, welcome_message,strlen(welcome_message)+1);
    if (error != strlen(welcome_message)+1){
        perror("write[inside addclient]");
        exit(EXIT_FAILURE);
    }

    if (top == NULL){ //if the top is empty
        top = p;
        return top;
    }

    while(tmp->next != NULL){ //add that boy to the back to of the list
        tmp = tmp->next;
    }
    tmp->next = p;
    return top;
}

static struct client *removeclient(struct client *top, int fd)
{
    struct client **p;

    for (p = &top; *p && (*p)->fd != fd; p = &(*p)->next)
        ;
    // Now, p points to (1) top, or (2) a pointer to another client
    // This avoids a special case for removing the head of the list
    if (*p)
    {
        struct client *t = (*p)->next;
        printf("Removing client %d %s\n", fd, inet_ntoa((*p)->ipaddr));
        free(*p);
        *p = t;
    }
    else
    {
        fprintf(stderr, "Trying to remove fd %d, but I don't know about it\n",
                fd);
    }
    return top;
}

int find_network_newline(char *buf, int inbuf){
    int i = 0;

    while ((buf[i] != '\0') && (i < inbuf))
    {
        if (buf[i] == '\r')
        {
            // network newline iff it is followed by '\n'
            if (buf[i + 1] == '\n')
            {
                // location of '\r'
                return i;
            }
        }
        i++;
    }
    return -1;
}

int do_message(struct client *p){
    int where; //stores the index of the network newline; -1 if DNE
    /*
        The purpose of the first iteration variable is if read returns 0 upon the 
        first iteration of the while loop, then we know that the client has exited
        But if it returns 0 afterwards, then we know that there was nothing else left to read,
        not that the client has exited. 
    */
    int first_interation = 0; 
    int nbytes;
    int error;
    
    int return_val = 0;
    while(1){
        nbytes = read(p->fd, (p->message).after, (p->message).room);
        if(nbytes == -1){
            perror("read");
            return -1;
        }
        if (nbytes == 0){ //all bytes read so break
            break;
        }

        first_interation = 1;

        (p->message.inbuf) = (p->message).inbuf + nbytes; // we still have things to read so we shift the buffer over by the amount of bytes we read

        where = find_network_newline((p->message).message_buffer, (p->message).inbuf);

        if(where >= 0){
            (p->message).message_buffer[where] = '\n';
            (p->message).message_buffer[where + 1] = '\0';

            //If they name is not set then set it now
            if ((p->name)[0] == '\0')
            {
                (p->message).message_buffer[where] = '\0';
                strncpy(p->name, (p->message).message_buffer, sizeof(p->message).message_buffer);
                return_val = 1;
            }
            else
            { // Otherwise, we know we are parsing a "(s)ay something" 
            char outbuf[512];
            sprintf(outbuf,"%s Your OPP starts yapping: \r\n", p->name);
            error = write(((p->match).opponent)->fd, outbuf, strlen(outbuf)+1); //send the opponent a mesage
            if(error == -1){
                perror("write");
                return -1;
            }
            //Write the message to the OPPs file descriptor
            strncpy(outbuf, (p->message).message_buffer, sizeof(p->message).message_buffer);
            error = write(((p->match).opponent)->fd, outbuf, strlen(outbuf) + 1);
            if(error == -1){
                perror("write");
                return -1;
            }
            return_val = 2;
        }
        (p->message).inbuf -= where + 2;
        memmove((p->message).message_buffer, (p->message).message_buffer + where + 2, sizeof(p->message).message_buffer);
        }
        (p->message).room = sizeof((p->message).message_buffer) - (p->message).inbuf;
        (p->message).after = (p->message).message_buffer + (p->message).inbuf;
        // We processed their name or message and therefore we don't have to read anymore.
        if (return_val == 1 || return_val == 2)
        {
            break;
        }

        // The user entered more than 256 characters for a message
        // They will be deleted from the client list because this is a violation
        if ((p->message).room == 0 && where < 0)
        {
            return -1;
        }

        break;
    }

    if(first_interation == 0){
        return -1;
    }

    return return_val;
    
}

static void broadcast(struct client *top, char *s, int size)
{
    int error;
    struct client *p;
    for (p = top; p; p = p->next)
    {
        error = write(p->fd, s, size);
        if (error == -1)
        {
            perror("write");
        }
    }
    /* should probably check write() return value and perhaps remove client */
}

int look_for_opponent(struct client *top, struct client *p){
        //TODO
    }

int handleclient(struct client *p, struct client *top){
    int error;
    char outbuf[512];
    if (p->has_something_to_say == 1){
        int result = do_message(p);
        if(result  == 2){
            p->has_something_to_say = 0; //has nothing to say anymore
        }
        return result;
    }

    if((p->match).in_match == 1 && p->turn == 1){
        //do something
    }

    if(p->turn == 0 && p->name[0] != '\0'){
        //do some more things
    }

    int message_result = do_message(p);
    if(message_result == -1)
    { // Error or socket closed, then LTG yourself
    printf("Disconnect from %s\n", inet_ntoa(p->ipaddr));
    return message_result;
    }

    if(message_result == 1){
        char *message = "You are waiting for an opp to slide\r\n";
        error = write(p->fd, message, strlen(message)+1);
        if(error == -1){
            perror("write");
            return -1;
        }

        sprintf(outbuf, "\n%s got a thang on em\r\n", p->name);
        
        broadcast(top, outbuf,strlen(outbuf)+1);
    }
}

    int main(void)
{
    int clientfd, maxfd, nready;
    struct client *p; //used to access the list
    struct client *head = NULL;
    socklen_t len;
    struct sockaddr_in q;
    fd_set allset;
    fd_set rset;
    int i;
    // Get the server
    int listenfd = bind_and_listen(); 

    // init allset and add the server to the set of the file descriptors passed into select

    FD_ZERO(&allset);
    FD_SET(listenfd, &allset);
    // maxfd is  how far into the set to search
    maxfd = listenfd;

    while(1){
        //make a copy of the set before we pass into select
        rset = allset; 

        nready = select(maxfd + 1, &rset, NULL, NULL, NULL);

        if (nready == 0){ // nothing happened 
            continue;
        }

        if (nready == -1){ //error happened and terminate 
            perror("select");
            continue;
        }
        
        // 
        if(FD_ISSET(listenfd, &rset)){ //FD_ISSET returns 1 on new connection so this checks for a new connection 

        len = sizeof(q); 
        if ((clientfd = accept(listenfd, (struct sockaddr*) &q, &len)) < 0){ //accepts the connection from the client
            perror("accept");
            exit(1);
        }
        FD_SET(clientfd, &allset); //add the jit that wanted to connect to the set of all FDs

        if(clientfd > maxfd){
            maxfd = clientfd; //updates maxfile descriptor
        }
        printf("A jit joined from %s\n", inet_ntoa(q.sin_addr));

        head = addclient(head, clientfd, q.sin_addr); //add the mfer to the client list
        }

        //update everyone else on the situation by looping through the set
        for (i = 0; i <= maxfd; i++)
        {
            if (FD_ISSET(i, &rset))
            {
                for (p = head; p != NULL; p = p->next)
                {
                    if (p->fd == i)
                    {
                        int result = handleclient(p, head);
                        if (result == -1)
                        {
                            int tmp_fd = p->fd;
                            head = removeclient(head, p->fd);
                            FD_CLR(tmp_fd, &allset);
                            close(tmp_fd);
                        }
                        break;
                    }
                }
            }
        }
    }
    return 0;
}