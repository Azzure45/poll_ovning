#include <iostream>
#include <memory>
#include <cstring>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define BUFFER_SIZE 1024

const int port = 8080;
const char ip[] = "127.0.0.1";

using std::cerr;
using std::cout;
using std::cin;
using std::unique_ptr;
using std::make_unique;

typedef struct Client_t{
    int client_fd;                      // client file descriptor
    char buffer[BUFFER_SIZE] = {0};     // message buffer
    size_t buff_len;                    // buffer lenght

    Client_t(void) : 
    client_fd(-1),
    buff_len(sizeof(buffer)) // constructor
    {
    }

    void send_msg(const char * txt, int flag){ // handels sending messages to server
        memset(&buffer, sizeof(buffer), 0);
        strcpy(buffer, txt);
        if(flag != MSG_OOB){ send(client_fd, &buffer, buff_len, flag); }
        else{send(client_fd, &buffer, 1, flag);}
    }

    void read_msg(void){
        memset(&buffer, sizeof(buffer), 0);
        read(client_fd, &buffer, buff_len);
        cout << buffer << "\n";

    }
}Client_t;

typedef unique_ptr<Client_t> Client_p;

int main(int argc, char *argv[]){
    Client_p c = make_unique<Client_t>();
    c->client_fd = socket(AF_INET, SOCK_STREAM, 0);
    if(c->client_fd <= 0){
        cerr << "Failed to create socket\n";
        return 1;
    }

    struct sockaddr_in client_addr;
    memset(&client_addr, 0, sizeof(client_addr));
    client_addr.sin_family = AF_INET;
    client_addr.sin_port = htons(port);
    inet_pton(AF_INET, ip, &client_addr.sin_addr);

    cout << "Connecting to server..." << std::endl;
    if (connect(c->client_fd, (struct sockaddr*)&client_addr, sizeof(client_addr)) < 0) {
        std::cerr << "Connection failed" << std::endl;
        close(c->client_fd);
        return 1;
    }

    cout << "Connected successfully\n";
    while(true){
        if (argc >= 2)
        {
            c->send_msg(argv[1], 0);
            // c->read_msg();
            sleep(1);
        }
    }

    close(c->client_fd);
}
