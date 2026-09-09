#include <stdio.h>
#include "csapp.h"
#include <assert.h>
#include "cache.h"



/* Define debug asserts */
#undef ASSERT
#ifdef MYDEBUG
#define ASSERT(COND) assert(COND)
#else
#define ASSERT(COND) ((void) 0)
#endif

/* Global Variables */
cache_t global_cache;

/* Function Prototypes */
void run_proxy(char *port);
void process_request(int clientfd);
int parse_requestline(char *buf, char *request, char *server_host, char *server_port);
void parse_requesthdrs(char *buf, char *request);
void *thread(void *vargp);
void clienterror(int fd, char *cause, char *errnum,
        char *shortmsg, char *longmsg);

/* Test Prototypes */
int test();
void test_parse_requestline();
void test_parse_requesthdrs();

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr = "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n";


/* Processes the request line (method, uri, version). 
 * Side effects: 
 * - Adds request line to the request string
 * - Add host to server_host
 * - Add port to server_port
 * Returns -1 if invalid request line, 1 if uri is not cgi-bin, else 0. 
 *   */
int parse_requestline(char *buf, char *request, char *server_host, char *server_port)
{
    char method[MAXLINE], uri[MAXLINE], version[MAXLINE], path[MAXLINE];
    char *ptr;

    sscanf(buf, "%s %s %s", method, uri, version);
    printf("method: %s\n", method);
    if(strcasecmp(method, "GET") != 0){
        return -1;
    }

    /* Fill out server_host */
    if (strstr(uri, "http://") || strstr(uri, "https://"))
    { /* remove http:// */
        ptr = strchr(uri, '/') + 2;
        strcpy(server_host, ptr);
    } else {
        strcpy(server_host, uri);
    }

    ptr = strchr(server_host, '/');
    if (ptr != NULL) {
        strcpy(path, ptr);
        *ptr = '\0';
    } else {
        strcpy(path, "/");
    }

    /* Fill out server_port */
    ptr = strchr(server_host, ':');
    if (ptr != NULL){
        *ptr = '\0';
        strcpy(server_port, ptr + 1); /* Avoid the ":" */
    } else {
        server_port = "";
    }

    /* Place request line in request string */
    sprintf(request, "%s %s %s\r\n", method, path, version);
    return 0;
}

void test_parse_requestline()
{
    char request[MAXLINE], server_host[MAXLINE], server_port[MAXLINE];

    /* POST */
    ASSERT(parse_requestline(
                "POST /cgi-bin/adder?15000&213 HTTP/1.0\r\n", 
                request, server_host, server_port) == -1);

    /* cgi-bin */
    ASSERT(parse_requestline("GET http://www.hi.com/cgi-bin/adder?15000&213 HTTP/1.0\r\n",
               request, server_host, server_port) == 0);
    ASSERT(strcmp(server_host, "www.hi.com") == 0);
    ASSERT(strcmp(server_port, "") == 0);
    ASSERT(strcmp(request, "GET /cgi-bin/adder?15000&213 HTTP/1.0\r\n") == 0);

    /* Static */
    parse_requestline("GET hi.com/bin/adder?15000&213 HTTP/1.0\r\n",
               request, server_host, server_port);
    ASSERT(strcmp(server_host, "hi.com") == 0);
    ASSERT(strcmp(server_port, "") == 0);
    ASSERT(strcmp(request, "GET /bin/adder?15000&213 HTTP/1.0\r\n") == 0);

    /* Index file */
    parse_requestline("GET http://www.hi.com HTTP/1.0\r\n",
               request, server_host, server_port);
    ASSERT(strcmp(server_host, "www.hi.com") == 0);
    ASSERT(strcmp(server_port, "") == 0);
    ASSERT(strcmp(request, "GET / HTTP/1.0\r\n") == 0);

    /* port specified */
    ASSERT(parse_requestline("GET https://www.hi.com:8080/cgi-bin/adder?15000&213 HTTP/1.0\r\n",
               request, server_host, server_port) == 0);
    ASSERT(strcmp(server_host, "www.hi.com") == 0);
    ASSERT(strcmp(server_port, "8080") == 0);
    ASSERT(strcmp(request, "GET /cgi-bin/adder?15000&213 HTTP/1.0\r\n") == 0);
}

/* Parse the request headers.
 * Certain headers are ignored.
 * Otherwise the header is added to the request string */
void parse_requesthdrs(char *buf, char *request)
{
    if ((strstr(buf, "Connection: ") == NULL) 
        && (strstr(buf, "Proxy-Connection: ") == NULL) 
        && (strstr(buf, "User-Agent: ") == NULL) 
        && (strstr(buf, "Host: ") == NULL) 
        ) {
        strcat(request, buf);
    }
}

void test_parse_requesthdrs()
{
    char request[MAXLINE], ua_non_const[MAXLINE];
    strcpy(ua_non_const, user_agent_hdr);
    /* initialize request */
    strcpy(request, "a");

    /* Certain headers are ignored */
    parse_requesthdrs("Connection: close\r\n", request);
    ASSERT(strcmp(request, "a") == 0);
    parse_requesthdrs("Proxy-Connection: close\r\n", request);
    ASSERT(strcmp(request, "a") == 0);
    parse_requesthdrs(ua_non_const, request);
    ASSERT(strcmp(request, "a") == 0);
    parse_requesthdrs("Host: www.cmu.edu\r\n", request);
    ASSERT(strcmp(request, "a") == 0);

    /* Adding other headers */
    parse_requesthdrs("Authorization: XXX\r\n", request);
    ASSERT(strcmp(request, "aAuthorization: XXX\r\n") == 0);
    parse_requesthdrs("Content-Length: 12\r\n", request);
    ASSERT(strcmp(request, 
                "aAuthorization: XXX\r\nContent-Length: 12\r\n") == 0);
}

/* Processes the request.
 *  Request has two parts 
 *   - request line (method, uri, version)
 *   - List of headers */
void process_request(int clientfd)
{
    int serverfd, tmpsize;
    size_t filesize, res_filesize;
    char request[MAXLINE], requestline[MAXLINE], buf[MAXLINE],
         server_host[MAXLINE], server_port[MAXLINE],
         cache_store[MAX_OBJECT_SIZE];
    void **res;
    rio_t client_rio, server_rio;


    Rio_readinitb(&client_rio, clientfd);
    Rio_readlineb(&client_rio, buf, MAXLINE); /* Read request line */

    printf("Received: %s\n", buf);
    strcpy(requestline, buf);
    /* Check cache */
    res = Malloc(sizeof(void*));
    if(cache_find(&global_cache, requestline, res, &res_filesize) >= 0)
        
    {
        Rio_writen(clientfd, *res, res_filesize);
    } else {

        if (parse_requestline(buf, request, server_host, server_port) == -1) {
            /* request line invalid */
            clienterror(clientfd, buf, "501", "Not implemented",
                    "This proxy does not implement this method");
            return;
         }

        strcat(request, "Host: ");
        strcat(request, server_host);
        strcat(request, "\r\n");
        strcat(request, user_agent_hdr);
        strcat(request, "Connection: close\r\n");
        strcat(request, "Proxy-Connection: close\r\n");
     
        Rio_readlineb(&client_rio, buf, MAXLINE);
        while(strcmp(buf, "\r\n")){
            parse_requesthdrs(buf, request);
            Rio_readlineb(&client_rio, buf, MAXLINE);
        }
        strcat(request, "\r\n");

        printf("Connecting to (%s, %s)\n", server_host, server_port);
        if((serverfd = Open_clientfd(server_host, 
                strcmp(server_port,"") == 0 ? NULL : server_port)) < 0)
        {
            clienterror(clientfd, server_host, "400", "Bad Request",
                    "The requested server was not found");
            return;
        }

        printf("Connected to (%s, %s)\n", server_host, server_port);

        printf("Sending request: %s\n", request);
        Rio_writen(serverfd, request, strlen(request));


        /* Send Header from server to client*/
        Rio_readinitb(&server_rio, serverfd);
        Rio_readlineb(&server_rio, buf, MAXLINE);
        strcat(cache_store, buf);
        while (strcmp(buf, "\r\n")){
            printf("send: %s\n", buf);
            Rio_writen(clientfd, buf, strlen(buf));
            if (strstr(buf, "Content-length: ")){
                filesize = atoi(strchr(buf,':') + 2);
            }
            Rio_readlineb(&server_rio, buf, MAXLINE);
            strcat(cache_store, buf);
        }
        Rio_writen(clientfd, "\r\n", strlen("\r\n"));


        if (filesize + strlen(cache_store) <= MAX_OBJECT_SIZE)
        {
            while (filesize > 0){
                if (filesize > MAXLINE) {
                    filesize -= Rio_readnb(&server_rio, buf, MAXLINE);
                    strcat(cache_store, buf);
                    Rio_writen(clientfd, buf, MAXLINE);
                } else {
                    tmpsize = Rio_readnb(&server_rio, buf, filesize);
                    strcat(cache_store, buf);
                    Rio_writen(clientfd, buf, filesize);
                    filesize -= tmpsize;
                }
            }
            cache_insert(&global_cache, requestline, cache_store, strlen(cache_store));

        } else {
            while (filesize > 0){
                if (filesize > MAXLINE) {
                    filesize -= Rio_readnb(&server_rio, buf, MAXLINE);
                    Rio_writen(clientfd, buf, MAXLINE);
                } else {
                    tmpsize = Rio_readnb(&server_rio, buf, filesize);
                    Rio_writen(clientfd, buf, filesize);
                    filesize -= tmpsize;
                }
            }
        }
        Close(serverfd);
    }
    Free(res);
    return;
}

void *thread(void *vargp)
{
    int connfd = *((int *) vargp);
    Pthread_detach(pthread_self());
    Free(vargp);
    process_request(connfd);
    Close(connfd);
    return NULL;

}

/* Run the proxy based on a port */
void run_proxy(char *proxyport)
{
    int listenfd, *connfdp;
    char client_hostname[MAXLINE], client_port[MAXLINE];
    socklen_t clientlen;
    struct sockaddr_storage clientaddr;
    pthread_t tid;

    /* Start listening on the given port */
    listenfd = Open_listenfd(proxyport);

    while(1) {
        clientlen = sizeof(clientaddr);
        connfdp = Malloc(sizeof(int));
        *connfdp = Accept(listenfd, (SA *)&clientaddr, &clientlen); 
        Getnameinfo((SA *) &clientaddr, clientlen, client_hostname, MAXLINE,
                client_port, MAXLINE, 0);
        printf("Accepted connection from (%s, %s)\n", client_hostname, client_port);
        Pthread_create(&tid, NULL, thread, connfdp);
    }
}

/*  Sends back an error in html */
void clienterror(int fd, char *cause, char *errnum,
        char *shortmsg, char *longmsg)
{
    char buf[MAXLINE], body[MAXBUF];
    sprintf(body, "<html><title>Proxy Error</title>");
    sprintf(body, "%s<body bgcolor=""ffffff"">\r\n", body);
    sprintf(body, "%s%s: %s\r\n", body, errnum, shortmsg);
    sprintf(body, "%s<p>%s: %s\r\n", body, longmsg, cause);
    sprintf(body, "%s<hr><em>Proxy Lab</em>\r\n", body);

    sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "Content type: text/html\r\n");
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "Content-length: %d\r\n\r\n", (int) strlen(body));
    Rio_writen(fd, buf, strlen(buf));
    Rio_writen(fd, body, strlen(body));
}


int test(){
    test_parse_requestline();
    test_parse_requesthdrs();
    test_cache();
    return 1;
}

int main(int argc, char **argv)
{
    int flag;
    /* Testing */
    flag = 0;
    /* flag = test(); */
    printf("Testing is %s\n", flag ? "on" : "off");

    if (argc != 2){
        fprintf(stderr, "Usage: %s <port number>\n", argv[0]);
        exit(1);
    }

    /* Ignore SIGPIPE signals */
    Signal(SIGPIPE, SIG_IGN);


    cache_init(&global_cache, CACHE_LIST_SIZE);
    run_proxy(argv[1]);
    cache_clear(&global_cache);

}
