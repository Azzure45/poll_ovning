#include <iostream>
#include <string>
#include <memory>
#include <cstring>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/poll.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>
#include <functional>
#include <mutex>
#include <queue>
#include <thread>
#include <condition_variable>
#include <iomanip> 
#include <ctime>
#include <string>

using std::vector;
using std::queue;
using std::thread;
using std::function;
using std::unique_lock;
using std::mutex;
using std::condition_variable;
using std::string;
using std::cerr;
using std::cout;

using std::shared_ptr;
using std::make_shared;

#define BUFFER_SIZE 1024                                // Msg buffer
#define TIMEOUT 1000   * 60                             // Timeout in 60 sec
#define COUNT_TASKS 20                                  // Max amount of tasks (only for optimization)

const int port = 8080;                                  // Communation port
const char ip[] = "127.0.0.1";                          // Loopback ip address

/*
    Task_t - struct to be used for each task the thread are going to handle
*/
typedef struct Task_t{
    Task_t(int client_fd, function<void(Task_t *)> job) : cfd(client_fd), func(job)
    {}
    function<void(Task_t *)> func;
    int cfd;
} Task_t;

class ThreadPool {
public:
// Constructor
ThreadPool(size_t num_threads = thread::hardware_concurrency(), int max_task = COUNT_TASKS)
{
    // Creates the limited amount of tasks
    for(int i=0; i<max_task; i++){
        Task_t *t = new Task_t(-1, NULL);
        free_tasks.push(t);
    }

    // Creating worker threads
    for (size_t i = 0; i < num_threads; ++i) {
        // emplaces the thread ("this") with a lambda function
        threads.emplace_back([this] { 
            Task_t *t = {};
            while (true) {
                {
                    // Starts mutex lock while in this code block 
                    unique_lock<mutex> lock(queue_mutex);
                    // Stops thread to wait until lambda return value or cv notifice that queue has work
                    cv.wait(lock, [this] {
                        return !busy_tasks.empty();
                    });
                    // Extra failsafe check
                    if (busy_tasks.empty()) {
                        return;
                    }
                    // Get the next task from the queue
                    t = busy_tasks.front();
                    free_tasks.emplace(t);
                    busy_tasks.pop();
                } // End of mutex lock
                
                // Let worker thread to it's work
                t->func(t);
            }
        });
    }
}
// Deconstructor
~ThreadPool()
{
    cv.notify_all();
    // This not neccessery, since they will a be destroyed when the main thread dies, but good practice nontheless
    for (auto& thread : threads) {
        thread.join();
    }
}

// Adds task to busy queue
int addTask(int cfd, function<void(Task_t *)> func){
    if(free_tasks.empty()){ return -1; }
    // Gets a read/write refrence of the top task
    auto t = free_tasks.front();
    t->cfd = cfd;
    t->func = func;
    {// mutex lock begin
        unique_lock<mutex> lock(queue_mutex);
        busy_tasks.emplace(t);
    }// mutex lock stop
    free_tasks.pop();
    cv.notify_one();
    return 1;
}

private:
    vector<thread> threads;                                     // Vector of all worker threads                                     
    queue<Task_t*> busy_tasks = {};                             // Queue of a task that should be worked on
    queue<Task_t*> free_tasks;                                  // Queue of all idle task not set to be worked on
    mutex queue_mutex;                                          // Mutex variable
    condition_variable cv;                                      // Condition variable (variable)
};

/*
    Name might not be the clearest, but this reads the msg from the client and then if it reads "Time\n"
    then it returns the local time back
*/
void read_client(Task_t *t){
    char buffer[BUFFER_SIZE] = {0};                             // char buffer or C style string
    size_t nbuffer = 0;                                         // Size of buffer
    int rc;                                                     // Recive variable
    struct pollfd pfd[1];                                       // Poll file descriptor
    // seting up pollfd
    pfd[0].events = POLLIN;                                 
    pfd[0].fd = t->cfd;
    // End condtion of do-while loop
    bool read_done = false;
    do{
        rc = poll(pfd, 1, TIMEOUT);                             // Poll() is here to check for slow/non-resposive clients
        if(rc < 0){
            close(pfd[0].fd);
            cerr << "Closing socket due to error while using poll()\n";
            break;
        }
        else if(rc == 0){
            close(pfd[0].fd);
            cerr << "Closing socket due to it being non-resposive\n";
            break;
        }
        // Poll managed to see life from the client, now it is time to read() the msg
        rc = read(pfd[0].fd, buffer + nbuffer, sizeof(buffer));
        if(rc <= 0){
            close(pfd[0].fd);
            cerr << "Closing socket due to being unable to read data\n";
            break;
        }
        
        // To find the end of the string
        for(int i=0; i<rc; i++){
            if(buffer[i] == '\n' || buffer[i] == (char)0){
                nbuffer = i;
                break;
            }
        }
        if(buffer[nbuffer] == '\n'){
            read_done = true;
        }
        else if(nbuffer >= BUFFER_SIZE){
            send(pfd[0].fd, "ERROR: MSG TO LONG\n", 20, 0);
            memset(buffer, 0, sizeof(buffer));
            close(pfd[0].fd);
            return;
        }

    }while(!read_done);
    // Checks to see if the command the client sent is right
    if(!strcmp(buffer, "Time\n")){
        char output[128];
        const char* prefix = "Time: ";
        // Getting the local time
        time_t now = std::time(nullptr);
        char timeStr[16];
        // Formating the time to a char array
        std::strftime(timeStr, sizeof(timeStr), "%Hh %Mmin %Ssec", std::localtime(&now));
        // Appending the time to the const "Time: " char array
        std::snprintf(output, sizeof(output), "%s%s", prefix, timeStr);
        // Sending msg back
        send(pfd[0].fd, output, sizeof(output), 0);
    }
    else{
        send(pfd[0].fd, "ERROR: NOT VAILD COMMAND\n", 26, 0);
    }
    close(pfd[0].fd);
}



int main(void){
    // Creating server socket
    int sfd = socket(AF_INET, SOCK_STREAM, 0);
    if(sfd <= 0){
        cerr << "Failed to create server socket\n";
        exit(1);
    }
    // Setting socket to resuable and more
    int opt = 1;
    if(setsockopt(sfd, SOL_SOCKET, SO_REUSEADDR, (char *)&opt, sizeof(opt)) < 0){
        cerr << "Failed to use setsockopt\n";
        exit(2);
    }
    // Set this and later sockers to non-blocking
    if (ioctl(sfd, FIONBIO, (char *)&opt) < 0)
    {
        cerr << "Failed to use ioctl() to make socket non-blocking\n";
        exit(3);
    }
    // Seting up bind of socket
    struct sockaddr_in s_addr;
    memset(&s_addr, 0, sizeof(s_addr));
    s_addr.sin_family=AF_INET;
    s_addr.sin_port=htons(port);
    inet_pton(AF_INET, ip, &s_addr.sin_addr);
    // Binding socket
    if(bind(sfd, (struct sockaddr *)&s_addr, sizeof(s_addr)) < 0){
        cerr << "Failed to bind socket\n";
        exit(4);
    }
    // Set socket to listen
    if(listen(sfd, 10) < 0){
        cerr << "Failed to listen\n";
        exit(5);
    }
    cout << "Listening on port: " << port << "...\n";
    // Setting up pollfd for the server file descriptor
    struct pollfd spoll[1];
    spoll[0].fd = sfd;
    spoll[0].events = POLLIN;
    int rc = 0;
    // Creates the thread pool, in this case with 4 worker threads
    auto tp = new ThreadPool(4);
    // Start of main thread loop
    while(true){
        // Listning for clients that want to connect
        rc = poll(spoll, 1, -1);
        if(rc < 0){
            cerr << "Failed to use poll, closing the server\n";
            break; 
        }
        int cfd = -1;                                           // client file descriptor
        // Loop to accept all clients that want to connect
        do{
            cfd = accept(spoll[0].fd, NULL, NULL);
            if (cfd < 0)
            {
                if (errno != EWOULDBLOCK){ cerr << "accept() failed\n"; }
                break;
            }
            printf("New incoming connection - %d\n", cfd);
            if(tp->addTask(cfd, read_client) < 0){
                cerr << "Too many clients\n";
                break;
            }
        } while (cfd != -1);
        // Since the workload of the threads is so light I have to artifically make the server slower to test multiable clients
        // But feel free to remove this line of code
        sleep(5);
    }
    // Not used but got a practice to follow
    close(spoll[0].fd);
}