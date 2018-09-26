/**
 * webserver.c -- A webserver written in C
 * 
 * Test with curl (if you don't have it, install it):
 * 
 *    curl -D - http://localhost:3490/
 *    curl -D - http://localhost:3490/d20
 *    curl -D - http://localhost:3490/date
 * 
 * You can also test the above URLs in your browser! They should work!
 * 
 * Posting Data:
 * 
 *    curl -D - -X POST -H 'Content-Type: text/plain' -d 'Hello, sample data!' http://localhost:3490/save
 * 
 * (Posting data is harder to test from a browser.)
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/wait.h>
#include <signal.h>
#include <time.h>
#include <sys/file.h>
#include <fcntl.h>
#include "net.h"
#include "file.h"
#include "mime.h"
#include "cache.h"

#define PORT "3490"  // the port users will be connecting to

#define SERVER_FILES "./serverfiles"
#define SERVER_ROOT "./serverroot"

/**
 * Handle SIGCHILD signal
 *
 * We get this signal when a child process dies. This function wait()s for
 * Zombie processes.
 *
 * This is only necessary if we've implemented a multiprocessed version with
 * fork().
 */
void sigchld_handler(int s) {
    (void)s; // quiet unused variable warning

    // waitpid() might overwrite errno, so we save and restore it:
    int saved_errno = errno;

    // Wait for all children that have died, discard the exit status
    while(waitpid(-1, NULL, WNOHANG) > 0);

    errno = saved_errno;
}

/**
 * Set up a signal handler that listens for child processes to die so
 * they can be reaped with wait()
 *
 * Whenever a child process dies, the parent process gets signal
 * SIGCHLD; the handler sigchld_handler() takes care of wait()ing.
 * 
 * This is only necessary if we've implemented a multiprocessed version with
 * fork().
 */
void start_reaper(void)
{
    struct sigaction sa;

    sa.sa_handler = sigchld_handler; // Reap all dead processes
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART; // Restart signal handler if interrupted
    if (sigaction(SIGCHLD, &sa, NULL) == -1) {
        perror("sigaction");
        exit(1);
    }
}

/**
 * Send an HTTP response
 *
 * header:       "HTTP/1.1 404 NOT FOUND" or "HTTP/1.1 200 OK", etc.
 * content_type: "text/plain", etc.
 * body:         the data to send.
 * 
 * Return the value from the send() function.
 */
int send_response(int fd, char *header, char *content_type, void *body, int content_length)
{
    const int max_response_size = 65536;
    char response[max_response_size];

    // Get current time for the HTTP header
    time_t t1 = time(NULL);
    struct tm *ltime = localtime(&t1);

    int response_length = sprintf(response,
        "%s\n"
        "Date: %s" // asctime adds its own newline
        "Connection: close\n"
        "Content-Length: %d\n"
        "Content-Type: %s\n"
        "\n" // End of HTTP header
        "%s\n",

        header,
        asctime(ltime),
        content_length,
        content_type,
        body);

    // Send it all!
    int rv = send(fd, response, response_length, 0);

    if (rv < 0) {
        perror("send");
    }

    return rv;
}


/**
 * Send a /d20 endpoint response
 */
void get_d20(int fd)
{
    srand(time(NULL) + getpid());

    char response_body[8];
    sprintf(response_body, "%d\n", (rand()%20)+1);

    send_response(fd, "HTTP/1.1 200 OK", "text/plain", response_body, strlen(response_body));
}

/**
 * Send a 404 response
 */
void resp_404(int fd)
{
    char filepath[4096];
    struct file_data *filedata; 
    char *mime_type;

    snprintf(filepath, sizeof filepath, "%s/404.html", SERVER_FILES);
    filedata = file_load(filepath);

    if (filedata == NULL) {
        // TODO: make this non-fatal
        fprintf(stderr, "cannot find system 404 file\n");
        exit(3);
    }

    mime_type = mime_type_get(filepath);

    send_response(fd, "HTTP/1.1 404 NOT FOUND", mime_type, filedata->data, filedata->size);

    file_free(filedata);
}

/**
 * Read and return a file
 */
void get_file(int fd, char *request_path)
{
    char filepath[4096];
    struct file_data *filedata; 
    char *mime_type;

    // Try to find the file
    snprintf(filepath, sizeof filepath, "%s%s", SERVER_ROOT, request_path);
    filedata = file_load(filepath);

    if (filedata == NULL) {
        // If we can't find it, try with an index.html on the end
        snprintf(filepath, sizeof filepath, "%s%s/index.html", SERVER_ROOT, request_path);
        filedata = file_load(filepath);

        if (filedata == NULL) {
            resp_404(fd);
            return;
        }
    }

    // At this point, we've loaded a file from somewhere or died trying

    mime_type = mime_type_get(filepath);

    send_response(fd, "HTTP/1.1 200 OK",  mime_type, filedata->data, filedata->size);

    file_free(filedata);
}

/**
 * Post /save endpoint data
 */
void post_save(int fd, char *body)
{
    char *status;

    // Open the file
    int file_fd = open("data.txt", O_CREAT|O_WRONLY, 0644);

    if (file_fd >= 0) {
        // Exclusive lock to keep processes from trying to write the file at the
        // same time. This is only necessary if we've implemented a
        // multiprocessed version with fork().
        flock(file_fd, LOCK_EX);

        // Write body
        write(file_fd, body, strlen(body));

        // Unlock
        flock(file_fd, LOCK_UN);

        // Close
        close(file_fd);

        status = "ok";
    } else {
        status = "fail";
    }

    // Now send an HTTP response

    char response_body[128];

    sprintf(response_body, "{\"status\": \"%s\"}\n", status);

    send_response(fd, "HTTP/1.1 200 OK", "application/json", response_body, strlen(response_body));
}

/**
 * Search for the end of the HTTP header
 * 
 * "Newlines" in HTTP can be \r\n (carriage return followed by newline) or \n
 * (newline) or \r (carriage return).
 */
char *find_start_of_body(char *header)
{
    char *p;

    p = strstr(header, "\n\n");

    if (p != NULL) return p;

    p = strstr(header, "\r\n\r\n");

    if (p != NULL) return p;

    p = strstr(header, "\r\r");

    return p;
}

/**
 * Handle HTTP request and send response
 */
void handle_http_request(int fd)
{
    const int request_buffer_size = 65536; // 64K
    char request[request_buffer_size];
    char *p;
    char request_type[8]; // GET or POST
    char request_path[1024]; // /info etc.
    char request_protocol[128]; // HTTP/1.1

    // Read request
    int bytes_recvd = recv(fd, request, request_buffer_size - 1, 0);

    if (bytes_recvd < 0) {
        perror("recv");
        return;
    }

     // NUL terminate request string
    request[bytes_recvd] = '\0';

    // Look for two newlines marking the end of the header
    p = find_start_of_body(request);

    if (p == NULL) {
        printf("Could not find end of header\n");
        exit(1);
    }

    // And here is the body
    char *body = p;

    /*
    * Now that we've assessed the request, we can take actions.
    */

    // Read the three components of the first request line
    sscanf(request, "%s %s %s", request_type, request_path,
        request_protocol);

    printf("REQUEST: %s %s %s\n", request_type, request_path, request_protocol);

    if (strcmp(request_type, "GET") == 0) {

        if (strcmp(request_path, "/d20") == 0) {
            // Handle any programmatic endpoints here
            get_d20(fd);
        } else {
            // Otherwise try to get a file
            get_file(fd, request_path);
        }
    }

    else if (strcmp(request_type, "POST") == 0) {
        // Endpoint "/save"
        if (strcmp(request_path, "/save") == 0) {
            post_save(fd, body);

        } else {
            resp_404(fd);
        }
    }

    else {
        fprintf(stderr, "unknown request type \"%s\"\n", request_type);
        return;
    }
}

/**
 * Main
 */
int main(void)
{
    int newfd;  // listen on sock_fd, new connection on newfd
    struct sockaddr_storage their_addr; // connector's address information
    char s[INET6_ADDRSTRLEN];

    // Start reaping child processes
    start_reaper();

    // Get a listening socket
    int listenfd = get_listener_socket(PORT);

    if (listenfd < 0) {
        fprintf(stderr, "webserver: fatal error getting listening socket\n");
        exit(1);
    }

    printf("webserver: waiting for connections on port %s...\n", PORT);

    // This is the main loop that accepts incoming connections and
    // fork()s a handler process to take care of it. The main parent
    // process then goes back to waiting for new connections.
    
    while(1) {
        socklen_t sin_size = sizeof their_addr;

        // Parent process will block on the accept() call until someone
        // makes a new connection:
        newfd = accept(listenfd, (struct sockaddr *)&their_addr, &sin_size);
        if (newfd == -1) {
            perror("accept");
            continue;
        }

        // Print out a message that we got the connection
        inet_ntop(their_addr.ss_family,
            get_in_addr((struct sockaddr *)&their_addr),
            s, sizeof s);
        printf("server: got connection from %s\n", s);
        
        // newfd is a new socket descriptor for the new connection.
        // listenfd is still listening for new connections.

        // For the non-fork() solution, remove this block of code and replace it with the one liner:
        //
        //     handle_http_request(newfd);
        //
        if (fork() == 0) {
            // We're the child process

            // We don't need the listening socket. The parent
            // process's listenfd is still open--we just close it in the
            // child.
            close(listenfd);

            // This does the heavy lifting, recv() the HTTP request and
            // send() the HTTP response.
            handle_http_request(newfd);

            // And this child is done! Bye bye!
            exit(0);
        }

        // Parent process out here, still

        // Parent doesn't need this. We need to close them as we get
        // them so we don't fill up the parent's file descriptor table.
        // The child's copy of newfd remains open.
        close(newfd);
    }

    // Unreachable code

    return 0;
}

