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

// make a client struct. 
struct client{ 
    int fd; //file descriptor for the client
    char name[256];
    struct in_addr ipaddr;
    struct client *next;
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

    p->fd = fd;
    p->ipaddr = addr;
    p->next = top;
    (p->name)[0] = '\0';
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

int main(void) {
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
        // for (i = 0; i <= maxfd; i++)
        // {
        //     if (FD_ISSET(i, &rset))
        //     {
        //         for (p = head; p != NULL; p = p->next)
        //         {
        //             if (p->fd == i)
        //             {
        //                 int result = handleclient(p, head);
        //                 if (result == -1)
        //                 {
        //                     int tmp_fd = p->fd;
        //                     head = removeclient(head, p->fd);
        //                     FD_CLR(tmp_fd, &allset);
        //                     close(tmp_fd);
        //                 }
        //                 break;
        //             }
        //         }
        //     }
        // }
    }
    return 0;
}