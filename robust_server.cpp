#include <iostream>
#include <string>
#include <memory>
#include <cstring>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

using std::string;
using std::cerr;
using std::cout;
using std::unique_ptr;
using std::make_unique;

#define BUFFER_SIZE 1024
#define TIMEOUT = (3 * 60 * 1000)           // timeout for three minutes

const int port = 8080;
const char ip[] = "127.0.0.1";

typedef struct Server_t{
    int server_fd;                          // server file descriptor
    char buffer[BUFFER_SIZE] = {0};         // message buffer
    size_t buff_len;                        // buffer lenght
    struct pollfd clients[200];

    Server_t(void) :
    server_fd(-1),
    buff_len(sizeof(buffer)) // constructor
    {
    }

    void send_msg(const char * txt, int flag){ // handels sending messages to client
        memset(&buffer, sizeof(buffer), 0);
        strcpy(buffer, txt);
        // send(client_fd, &buffer, buff_len, flag);
    }
}Server_t;

typedef unique_ptr<Server_t> Server_p;

int main(void){
    Server_p s = make_unique<Server_t>();
    s->server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if(s->server_fd <= 0){
        cerr << "Failed to create server socket\n";
        exit(1);
    }
    
    int opt = 1;
    if(setsockopt(s->server_fd, SOL_SOCKET, SO_REUSEADDR, (char *)&opt, sizeof(opt)) < 0){
        cerr << "Failed to use setsockopt\n";
        exit(2);
    }

    if (ioctl(s->server_fd, FIONBIO, (char *)&opt) < 0)
    {
        perror("ioctl() failed");
        close(listen_sd);
        exit(-1);
    }

    struct sockaddr_in s_addr;
    memset(&s_addr, 0, sizeof(s_addr));
    s_addr.sin_family=AF_INET;
    s_addr.sin_port=htons(port);
    inet_pton(AF_INET, ip, &s_addr.sin_addr);

    if(bind(s->server_fd, (struct sockaddr *)&s_addr, sizeof(s_addr)) < 0){
        cerr << "Failed to bind socket\n";
        exit(3);
    }

    if(listen(s->server_fd, 1) < 0){
        cerr << "Failed to listen\n";
        exit(4);
    }
    cout << "Listening on port: " << port << "...\n";
    
    memset(s->clients, 0 , sizeof(fds));

    s->clients[0].fd = listen_sd;
    s->clients[0].events = POLLIN;
  

    do{
        
        
    }while(true);




    struct sockaddr_in new_client;
    socklen_t client_len = sizeof(new_client);
    int read_bytes = 0;
    while(true){
        if(s->client_fd == -1){    
            s->client_fd = accept(s->server_fd, (struct sockaddr *)&new_client, &client_len);
            if(s->client_fd < 0){
                cerr << "Failed to find client\n";
            }
            cout << "connected to client fd: " << s->client_fd << "\n";
        }
    }
    close(s->server_fd);
}