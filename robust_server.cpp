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

#define BUFFER_SIZE 1024
#define TIMEOUT 1000   * 60            // Timeout in 60 sec
#define COUNT_TASKS 20



#define END_SERVER      (1 << 0)            // bit to close the serever
#define CLOSE_COMM      (1 << 1)            // bit to close comminucation with a socket
#define COMP_ARR        (1 << 2)            // bit to compress array

/*
    flags & MACRO (check bit on)

    flags |= MACRO (set bit)
    flags &= ~ MARCO (unset bit)
*/

const int port = 8080;
const char ip[] = "127.0.0.1";

typedef struct Task_t{
    Task_t(int client_fd, function<void(Task_t *)> job) : cfd(client_fd), func(job)
    {
    }

    function<void(Task_t *)> func;
    int cfd;
} Task_t;

class ThreadPool {
public:
// Constructor to creates a thread pool with given
// number of threads
ThreadPool(size_t num_threads = thread::hardware_concurrency())
{
    for(int i=0; i<COUNT_TASKS; i++){
        Task_t *t = new Task_t(-1, NULL);
        free_tasks.push(t);
    }

    // Creating worker threads
    for (size_t i = 0; i < num_threads; ++i) {
        threads.emplace_back([this] { // Creates thread in Threads_ with [this] as the thread and a Lambda function (it's main loop)
            Task_t *t = {};
            while (true) {
                {
                    // Locking the queue so that data
                    // can be shared safely
                    unique_lock<mutex> lock(queue_mutex);

                    // Waiting until there is a task to
                    // execute or the pool is stopped

                    //? Lambda func which tells the wait if it should open the the mutex lock or not?
                    cv.wait(lock, [this] {
                        return !busy_tasks.empty();
                    });
                    if (busy_tasks.empty()) {
                        return;
                    }
                    
                    // Get the next task from the queue
                    t = busy_tasks.front();
                    free_tasks.emplace(t);
                    busy_tasks.pop();
                }
                
                t->func(t);
            }
        });
    }
}
    ~ThreadPool()
    {
        cv.notify_all();
        for (auto& thread : threads) {
            thread.join();
        }
    }

    int addTask(int cfd, function<void(Task_t *)> func){
        if(free_tasks.empty()){
            return -1;
        }
        auto t = free_tasks.front();
        t->cfd = cfd;
        t->func = func;
        // mutex lock
        {
            unique_lock<mutex> lock(queue_mutex);
            busy_tasks.emplace(t);
        }
        free_tasks.pop();
        cv.notify_one();
        return 1;
    }

private:
    vector<thread> threads;
    queue<Task_t*> busy_tasks = {};
    queue<Task_t*> free_tasks;
    mutex queue_mutex;
    condition_variable cv;
};

void read_client(Task_t *t){
    char buffer[BUFFER_SIZE] = {0}; 
    size_t nbuffer = 0;
    int rc;
    struct pollfd pfd[1];                                       // Poll file descriptor
    pfd[0].events = POLLIN;
    pfd[0].fd = t->cfd;
    bool read_done = false;
    do{
        rc = poll(pfd, 1, TIMEOUT);                             // the check for slow/non-resposive clients
        if(rc < 0 /*|| pfd[0].revents != POLLIN*/){
            close(pfd[0].fd);
            cerr << "Closing socket due to error while using poll()\n";
            break;
        }
        else if(rc == 0){
            close(pfd[0].fd);
            cerr << "Closing socket due to it being non-resposive\n";
            break;
        }

        rc = read(pfd[0].fd, buffer + nbuffer, sizeof(buffer));
        if(rc <= 0){
            close(pfd[0].fd);
            cerr << "Closing socket due to being unable to read data\n";
            break;
        }

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
    if(!strcmp(buffer, "Time\n")){
        char output[128];
        const char* prefix = "Time: ";

        time_t now = std::time(nullptr);
        char timeStr[16];

        std::strftime(timeStr, sizeof(timeStr), "%Hh %Mmin %Ssec", std::localtime(&now));

        std::snprintf(output, sizeof(output), "%s%s", prefix, timeStr);
        send(pfd[0].fd, output, sizeof(output), 0);
    }
    else{
        send(pfd[0].fd, "ERROR: NOT VAILD COMMAND\n", 26, 0);
    }
    close(pfd[0].fd);
}



int main(void){
    int sfd = socket(AF_INET, SOCK_STREAM, 0);
    if(sfd <= 0){
        cerr << "Failed to create server socket\n";
        exit(1);
    }
    
    int opt = 1;
    if(setsockopt(sfd, SOL_SOCKET, SO_REUSEADDR, (char *)&opt, sizeof(opt)) < 0){
        cerr << "Failed to use setsockopt\n";
        exit(2);
    }

    if (ioctl(sfd, FIONBIO, (char *)&opt) < 0)
    {
        cerr << "Failed to use ioctl() to make socket non-blocking\n";
        exit(3);
    }

    struct sockaddr_in s_addr;
    memset(&s_addr, 0, sizeof(s_addr));
    s_addr.sin_family=AF_INET;
    s_addr.sin_port=htons(port);
    inet_pton(AF_INET, ip, &s_addr.sin_addr);

    if(bind(sfd, (struct sockaddr *)&s_addr, sizeof(s_addr)) < 0){
        cerr << "Failed to bind socket\n";
        exit(4);
    }

    if(listen(sfd, 10) < 0){
        cerr << "Failed to listen\n";
        exit(5);
    }
    cout << "Listening on port: " << port << "...\n";

    struct pollfd spoll[1];
    spoll[0].fd = sfd;
    spoll[0].events = POLLIN;
    int rc = 0;
    auto tp = new ThreadPool(4);
    /*** Start of main loop ***/
    while(true){
        // cout << "Wating on poll()...\n";
        rc = poll(spoll, 1, -1);
        if(rc < 0){
            cerr << "Failed to use poll, closing the server\n";
            break; 
        }
        int cfd = -1;
        // printf(" Listening socket is readable\n");
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
        sleep(5);
    }
    close(spoll[0].fd);
}