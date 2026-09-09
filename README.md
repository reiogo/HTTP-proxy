# HTTP-proxy
A project to learn more about concurrency and networking in C.

### AI usage:
No AI was used for making this project in anyway whatsoever.

### What it is and why I made it.
This is a toy proxy. It is the result of a project from the book CSAPP (https://csapp.cs.cmu.edu/3e/labs.html).
This proxy receives HTTP requests from a client and relays the request to the intended server. 
When the server responds, the proxy forwards the data back to the client. (This can be useful for security or caching reasons.)
I made it in order to better understand concurrency and networking in C. I used a robust IO library, posix threads, and semaphores to make the project.

### Explanations of each file.
The cache logic is in the cache.c file (and its corresponding cache.h header file), my proxy is in the proxy.c file (and its corresponding proxy.h header file). The csapp.h file is the header file from the csapp library (I didn't publish the .c file here because its not mine to share, and in its place I have the .o file which is only machine code). I also have a makefile, but I don't think it makes sense to try to run the file, I have tested extensively in a separate repository with the rest of the csapp labs.


