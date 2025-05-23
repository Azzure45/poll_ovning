This code is only the server part of the TCP connection.

So feel free to use your own client code, but for it to work as intended, please follow the instruction below:

- Update the CmakeList file so my server code and your client code is compiled right
- Use port 8080
- Use ip address 127.0.0.1
- Send specifically "Time\n"
- Then use read() or recv() to get the local time from the server
- Then close the socket (the server won't try and reconnect, it will just close the socket after sending the message)


Things of note:
- The server is artificially slowed down with a sleep() of 5 seconds, feel free to delete that line if you have better client test capabilities.
- this code was written on a windows machine, but it used CMake and WSL to use linux based libraries for the socket syntax/logic, if any error occurs during compilation please keep this in mind when trying to fix it.


If worst comes to worst, here is my git repo with my client code:
https://github.com/Azzure45/poll_ovning.git

if you wanna use this code use two split terminals (or more) do this in each terminal:
- change directory to the build/ directory

Then execute the server code (./server) and the client code (./client)

DO NOTE that the client takes multible command line args (this was for testing purposes), but I recommend using (./client t &) to run the client in the background (so you can make multiable clients run at once in one terminal) 