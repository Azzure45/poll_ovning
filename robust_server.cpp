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

using std::vector;
using std::queue;
using std::thread;
// using thread::hardware_concurrency;
using std::function;
using std::unique_lock;
using std::mutex;
using std::condition_variable;
using std::string;
using std::cerr;
using std::cout;
using std::unique_ptr;
using std::make_unique;

#define BUFFER_SIZE 1024
#define TIMEOUT 1 * 60 * 1000               // Timeout for three minutes


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

class ThreadPool {
public:
    ThreadPool(size_t num_threads = thread::hardware_concurrency()){ // Constructor
        // Creating worker threads
        for (size_t i = 0; i < num_threads; ++i) {
            // Fills the vector of threads with their behaviour func
            threads_.emplace_back([this] {
                while (true) {
                    // Defines standard thread behaviour func
                    function<void()> task; {
                        // Locking the queue
                        unique_lock<mutex> lock(queue_mutex_);
                        // Waiting until there is a task to execute
                        cv_.wait(lock, [this] { return !tasks_.empty() || stop_; });

                        // Exit the thread if queue empty
                        if (stop_ && tasks_.empty()) { return; }

                        // Get the next task from the queue
                        task = move(tasks_.front());
                        tasks_.pop();
                    }

                    task();
                }
            });
        }
    }

    // Destructor to stop the thread pool
    ~ThreadPool(){
        { //! varför är de här curly braces här?
            // Lock the queue to update the stop flag safely
            unique_lock<mutex> lock(queue_mutex_);
            stop_ = true;
        }

        // Notify all threads
        cv_.notify_all();

        // Joining all worker threads
        for (auto& thread : threads_) {
            thread.join();
        }
    }

    // To add tasks to th queue
    void enqueue(function<void()> task){
        {
            unique_lock<std::mutex> lock(queue_mutex_);
            tasks_.emplace(move(task));
        }
        cv_.notify_one();
    }

private:
    // Vector to store worker threads
    vector<thread> threads_;

    // Queue of tasks
    queue<function<void()> > tasks_;

    // Mutex to synchronize access to shared data
    mutex queue_mutex_;

    // Condition variable to signal changes in the state of
    // the tasks queue
    condition_variable cv_;

    // Flag to indicate whether the thread pool should stop
    // or not
    bool stop_ = false;
};

typedef struct Server_t{
    int server_fd;                          // server file descriptor
    int client_n;                           // another name is 'nfds'
    char buffer[BUFFER_SIZE] = {0};         // message buffer
    size_t buff_len;                        // buffer lenght
    struct pollfd clients[200];
    int_fast8_t flags;

    Server_t(void) :
    server_fd(-1),
    client_n(1),
    flags(0),
    buff_len(sizeof(buffer))                // constructor
    {
        memset(clients, 0 , sizeof(clients));
    }

    void send_msg(const char * txt, int flag){ // handels sending messages to client
        memset(&buffer, sizeof(buffer), 0);
        strcpy(buffer, txt);
        // send(client_fd, &buffer, buff_len, flag);
    }

    // Do more with this
    int set_flag(){
        return 0;
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
        cerr << "Failed to use ioctl() to make socket non-blocking\n";
        exit(3);
    }

    struct sockaddr_in s_addr;
    memset(&s_addr, 0, sizeof(s_addr));
    s_addr.sin_family=AF_INET;
    s_addr.sin_port=htons(port);
    inet_pton(AF_INET, ip, &s_addr.sin_addr);

    if(bind(s->server_fd, (struct sockaddr *)&s_addr, sizeof(s_addr)) < 0){
        cerr << "Failed to bind socket\n";
        exit(4);
    }

    if(listen(s->server_fd, 1) < 0){
        cerr << "Failed to listen\n";
        exit(5);
    }
    cout << "Listening on port: " << port << "...\n";

    s->clients[0].fd = s->server_fd;
    s->clients[0].events = POLLIN;
  
    int rc = 0, new_client = -1;
    do{
        cout << "Wating on poll() ...\n";
        rc = poll(s->clients, s->client_n, TIMEOUT);
        if(rc < 0){
            cerr << "Failed to use poll, closing the server\n";
            break; 
        }
        else if(rc == 0){
            cerr << "timeout has expired, closing the server\n";
            break;
        }

        for(int i = 0; i < s->client_n; i++){
            if(s->clients[i].revents == 0){
                continue;
            }

            // Error check to make sure that revents is POLLIN
            if(s->clients[i].revents != POLLIN) //! men varför?
            {
                printf("  Error! revents = %d\n", s->clients[i].revents);
                s->flags |= END_SERVER;
                break;

            }
            if (s->clients[i].fd == s->server_fd)
            {
                /*******************************************************/
                /* Listening descriptor is readable.                   */
                /*******************************************************/
                printf("  Listening socket is readable\n");

                /*******************************************************/
                /* Accept all incoming connections that are            */
                /* queued up on the listening socket before we         */
                /* loop back and call poll again.                      */
                /*******************************************************/
                do
                {
                /*****************************************************/
                /* Accept each incoming connection. If               */
                /* accept fails with EWOULDBLOCK, then we            */
                /* have accepted all of them. Any other              */
                /* failure on accept will cause us to end the        */
                /* server.                                           */
                /*****************************************************/
                new_client = accept(s->server_fd, NULL, NULL);
                if (new_client < 0)
                {
                    if (errno != EWOULDBLOCK)
                    {
                    perror("  accept() failed");
                    s->flags |= END_SERVER;
                    }
                    break;
                }

                /*****************************************************/
                /* Add the new incoming connection to the            */
                /* pollfd structure                                  */
                /*****************************************************/
                printf("  New incoming connection - %d\n", new_client);
                s->clients[s->client_n].fd = new_client;
                s->clients[s->client_n].events = POLLIN;
                s->client_n++;

                /*****************************************************/
                /* Loop back up and accept another incoming          */
                /* connection                                        */
                /*****************************************************/
                } while (new_client != -1);
            }

            /*********************************************************/
            /* This is not the listening socket, therefore an        */
            /* existing connection must be readable                  */
            /*********************************************************/

            else
            {
                printf("  Descriptor %d is readable\n", s->clients[i].fd);
                s->flags |= CLOSE_COMM;
                cout << "before: " << (int)s->flags << "\n";
                s->flags &= ~CLOSE_COMM;
                cout << "after: " << (int)s->flags << "\n";
                sleep(1);
                /*******************************************************/
                /* Receive all incoming data on this socket            */
                /* before we loop back and call poll again.            */
                /*******************************************************/

                // do
                {
                /*****************************************************/
                /* Receive data on this connection until the         */
                /* recv fails with EWOULDBLOCK. If any other         */
                /* failure occurs, we will close the                 */
                /* connection.                                       */
                /*****************************************************/
                rc = recv(s->clients[i].fd, s->buffer, s->buff_len, 0);
                if (rc < 0)
                {
                    if (errno != EWOULDBLOCK){
                        perror("  recv() failed");
                        s->flags |= CLOSE_COMM;
                    }
                    break;
                }

                /*****************************************************/
                /* Check to see if the connection has been           */
                /* closed by the client                              */
                /*****************************************************/
                if (rc == 0){
                    printf("  Connection closed\n");
                    s->flags |= CLOSE_COMM;
                    break;
                }

                /*****************************************************/
                /* Data was received                                 */
                /*****************************************************/
                int len = rc;
                // !printf("  %d bytes received\n", len);

                /*****************************************************/
                /* Echo the data back to the client                  */
                /*****************************************************/
                cout << i << ": " << s->clients[i].fd << " / buffer: " << s->buffer << '\n';

                } //while(true);

                /*******************************************************/
                /* If the close_conn flag was turned on, we need       */
                /* to clean up this active connection. This            */
                /* clean up process includes removing the              */
                /* descriptor.                                         */
                /*******************************************************/
                if ((s->flags & CLOSE_COMM)){
                    cout << "removing client\n";
                    close(s->clients[i].fd);
                    s->clients[i].fd = -1;
                    s->flags |= COMP_ARR;
                }


            }  /* End of existing connection is readable             */
        } /* End of loop through pollable descriptors              */

            /***********************************************************/
            /* If the compress_array flag was turned on, we need       */
            /* to squeeze together the array and decrement the number  */
            /* of file descriptors. We do not need to move back the    */
            /* events and revents fields because the events will always*/
            /* be POLLIN in this case, and revents is output.          */
            /***********************************************************/

            //! would be KINDA funny to use merge sort here
            if (s->flags & COMP_ARR){
                s->flags &= ~COMP_ARR;
                for (int i = 0; i < s->client_n; i++){
                    if (s->clients[i].fd == -1){
                        for(int j = i; j < s->client_n-1; j++){
                            s->clients[j].fd = s->clients[j+1].fd;
                        }
                    i--;
                    s->client_n--;
                    }
                }
            }
        
    }while (!(s->flags & END_SERVER)); /* End of serving running.    */

  /*************************************************************/
  /* Clean up all of the sockets that are open                 */
  /*************************************************************/
  for (int i = 0; i < s->client_n; i++)
  {
    if(s->clients[i].fd >= 0){ close(s->clients[i].fd); }
  }
}

// do
//   {
//     /***********************************************************/
//     /* Call poll() and wait 3 minutes for it to complete.      */
//     /***********************************************************/
//     printf("Waiting on poll()...\n");
//     rc = poll(fds, nfds, timeout);

//     /***********************************************************/
//     /* Check to see if the poll call failed.                   */
//     /***********************************************************/
//     if (rc < 0)
//     {
//       perror("  poll() failed");
//       break;
//     }

//     /***********************************************************/
//     /* Check to see if the 3 minute time out expired.          */
//     /***********************************************************/
//     if (rc == 0)
//     {
//       printf("  poll() timed out.  End program.\n");
//       break;
//     }


//     /***********************************************************/
//     /* One or more descriptors are readable.  Need to          */
//     /* determine which ones they are.                          */
//     /***********************************************************/
//     current_size = nfds;
//     for (i = 0; i < current_size; i++)
//     {
//       /*********************************************************/
//       /* Loop through to find the descriptors that returned    */
//       /* POLLIN and determine whether it's the listening       */
//       /* or the active connection.                             */
//       /*********************************************************/
//       if(fds[i].revents == 0)
//         continue;

//       /*********************************************************/
//       /* If revents is not POLLIN, it's an unexpected result,  */
//       /* log and end the server.                               */
//       /*********************************************************/
//       if(fds[i].revents != POLLIN)
//       {
//         printf("  Error! revents = %d\n", fds[i].revents);
//         end_server = TRUE;
//         break;

//       }
//       if (fds[i].fd == listen_sd)
//       {
//         /*******************************************************/
//         /* Listening descriptor is readable.                   */
//         /*******************************************************/
//         printf("  Listening socket is readable\n");

//         /*******************************************************/
//         /* Accept all incoming connections that are            */
//         /* queued up on the listening socket before we         */
//         /* loop back and call poll again.                      */
//         /*******************************************************/
//         do
//         {
//           /*****************************************************/
//           /* Accept each incoming connection. If               */
//           /* accept fails with EWOULDBLOCK, then we            */
//           /* have accepted all of them. Any other              */
//           /* failure on accept will cause us to end the        */
//           /* server.                                           */
//           /*****************************************************/
//           new_sd = accept(listen_sd, NULL, NULL);
//           if (new_sd < 0)
//           {
//             if (errno != EWOULDBLOCK)
//             {
//               perror("  accept() failed");
//               end_server = TRUE;
//             }
//             break;
//           }

//           /*****************************************************/
//           /* Add the new incoming connection to the            */
//           /* pollfd structure                                  */
//           /*****************************************************/
//           printf("  New incoming connection - %d\n", new_sd);
//           fds[nfds].fd = new_sd;
//           fds[nfds].events = POLLIN;
//           nfds++;

//           /*****************************************************/
//           /* Loop back up and accept another incoming          */
//           /* connection                                        */
//           /*****************************************************/
//         } while (new_sd != -1);
//       }

//       /*********************************************************/
//       /* This is not the listening socket, therefore an        */
//       /* existing connection must be readable                  */
//       /*********************************************************/

//       else
//       {
//         printf("  Descriptor %d is readable\n", fds[i].fd);
//         close_conn = FALSE;
//         /*******************************************************/
//         /* Receive all incoming data on this socket            */
//         /* before we loop back and call poll again.            */
//         /*******************************************************/

//         do
//         {
//           /*****************************************************/
//           /* Receive data on this connection until the         */
//           /* recv fails with EWOULDBLOCK. If any other         */
//           /* failure occurs, we will close the                 */
//           /* connection.                                       */
//           /*****************************************************/
//           rc = recv(fds[i].fd, buffer, sizeof(buffer), 0);
//           if (rc < 0)
//           {
//             if (errno != EWOULDBLOCK)
//             {
//               perror("  recv() failed");
//               close_conn = TRUE;
//             }
//             break;
//           }

//           /*****************************************************/
//           /* Check to see if the connection has been           */
//           /* closed by the client                              */
//           /*****************************************************/
//           if (rc == 0)
//           {
//             printf("  Connection closed\n");
//             close_conn = TRUE;
//             break;
//           }

//           /*****************************************************/
//           /* Data was received                                 */
//           /*****************************************************/
//           len = rc;
//           printf("  %d bytes received\n", len);

//           /*****************************************************/
//           /* Echo the data back to the client                  */
//           /*****************************************************/
//           rc = send(fds[i].fd, buffer, len, 0);
//           if (rc < 0)
//           {
//             perror("  send() failed");
//             close_conn = TRUE;
//             break;
//           }

//         } while(TRUE);

//         /*******************************************************/
//         /* If the close_conn flag was turned on, we need       */
//         /* to clean up this active connection. This            */
//         /* clean up process includes removing the              */
//         /* descriptor.                                         */
//         /*******************************************************/
//         if (close_conn)
//         {
//           close(fds[i].fd);
//           fds[i].fd = -1;
//           compress_array = TRUE;
//         }


//       }  /* End of existing connection is readable             */
//     } /* End of loop through pollable descriptors              */

//     /***********************************************************/
//     /* If the compress_array flag was turned on, we need       */
//     /* to squeeze together the array and decrement the number  */
//     /* of file descriptors. We do not need to move back the    */
//     /* events and revents fields because the events will always*/
//     /* be POLLIN in this case, and revents is output.          */
//     /***********************************************************/
//     if (compress_array)
//     {
//       compress_array = FALSE;
//       for (i = 0; i < nfds; i++)
//       {
//         if (fds[i].fd == -1)
//         {
//           for(j = i; j < nfds-1; j++)
//           {
//             fds[j].fd = fds[j+1].fd;
//           }
//           i--;
//           nfds--;
//         }
//       }
//     }

//   } while (end_server == FALSE); /* End of serving running.    */

//   /*************************************************************/
//   /* Clean up all of the sockets that are open                 */
//   /*************************************************************/
//   for (i = 0; i < nfds; i++)
//   {
//     if(fds[i].fd >= 0)
//       close(fds[i].fd);
//   }