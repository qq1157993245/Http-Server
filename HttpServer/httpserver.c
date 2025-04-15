#include "helper_funcs.h"
#include "connection.h"
#include "debug.h"
#include "protocol.h"
#include "response.h"
#include "request.h"
#include "queue.h"
#include "rwlock.h"
#include "file_lock.h" //A file uses linked list to check if read/write to the same or different files.

#include <arpa/inet.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <regex.h>
#include <stdbool.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <pthread.h>

#include <sys/stat.h>

queue_t *queue = NULL;
file_lock *fl = NULL;
pthread_mutex_t mutex;
rwlock_t *rwl_audit_log = NULL;
pthread_t *threads = NULL;
int num_threads = 4;

void handle_connection(int);
void handle_get(conn_t *);
void handle_put(conn_t *);
void handle_unsupported(conn_t *);

void *worker_thread(void *arg) {
    (void) arg;
    while (1) {
        int *connfd = NULL;
        queue_pop(queue, (void **) &connfd);
        if (connfd) {
            handle_connection(*connfd);
            close(*connfd);
            free(connfd);
        }
    }
}

void free_memory(int signal) {
    (void) signal;

    delete_file_lock(&fl);

    pthread_mutex_destroy(&mutex);

    rwlock_delete(&rwl_audit_log);

    queue_delete(&queue);

    for (int i = 0; i < num_threads; i++) {
        pthread_join(threads[i], NULL);
    }
    free(threads);

    exit(EXIT_SUCCESS);
}

void exit_handler(void) {
    struct sigaction sa;
    sa.sa_handler = free_memory;
    sa.sa_flags = 0;
    sigemptyset(&sa.sa_mask);

    if (sigaction(SIGINT, &sa, NULL) == -1) {
        perror("sigaction");
        exit(EXIT_FAILURE);
    }
}

void concat_audit_log(const char *method, char *fileName, uint16_t status_code, char *id) {
    char *status_code_str = malloc(4); //including '\0'
    sprintf(status_code_str, "%d", status_code);

    char *audit_log = malloc(strlen(method) + 1 + strlen(fileName) + 1 + 1 + 3 + 1 + 1 + 1);
    strcpy(audit_log, method);
    strcat(audit_log, ",/");
    strcat(audit_log, fileName);
    strcat(audit_log, ",");
    strcat(audit_log, status_code_str);
    strcat(audit_log, ",");
    strcat(audit_log, id);

    writer_lock(rwl_audit_log);
    fprintf(stderr, "%s\n", audit_log);
    writer_unlock(rwl_audit_log);

    free(audit_log);
    free(status_code_str);
    audit_log = NULL;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        warnx("wrong arguments: %s port_num", argv[0]);
        fprintf(stderr, "usage: %s <port>\n", argv[0]);
        return EXIT_FAILURE;
    }

    /* Use getopt() to parse the command-line arguments */

    int opt;
    char *port_str;
    while ((opt = getopt(argc, argv, "t:")) != -1) {
        switch (opt) {
        case 't': {
            char *endptr = NULL;
            num_threads = strtol(optarg, &endptr, 10);
            if (endptr && *endptr != '\0') {
                fprintf(stderr, "Invalid Threads\n");
                return EXIT_FAILURE;
            }
            break;
        }
        case '?': fprintf(stderr, "Unknown Option\n"); return EXIT_FAILURE;
        default: break;
        }
    }

    if (argc == 2) {
        port_str = argv[1];
    } else if (argc > 2) {
        port_str = argv[3];
    }

    char *endptr = NULL;
    size_t port = (size_t) strtoull(port_str, &endptr, 10);
    if (endptr && *endptr != '\0') {
        fprintf(stderr, "Invalid Port\n");
        return EXIT_FAILURE;
    }

    if (port < 1 || port > 65535) {
        fprintf(stderr, "Invalid Port\n");
        return EXIT_FAILURE;
    }

    signal(SIGPIPE, SIG_IGN);
    Listener_Socket sock;
    if (listener_init(&sock, port) < 0) {
        fprintf(stderr, "Invalid Port\n");
        return EXIT_FAILURE;
    }

    queue = queue_new(num_threads);
    fl = create_file_lock();
    pthread_mutex_init(&mutex, NULL);
    rwl_audit_log = rwlock_new(WRITERS, 0);
    threads = malloc(num_threads * sizeof(pthread_t));

    for (int i = 0; i < num_threads; i++) {
        pthread_create(&threads[i], NULL, worker_thread, NULL);
    }

    exit_handler();

    while (1) {
        int *connfd = malloc(sizeof(int));
        *connfd = listener_accept(&sock);
        queue_push(queue, connfd);
    }

    return EXIT_SUCCESS;
}

void handle_connection(int connfd) {
    conn_t *conn = conn_new(connfd);

    const Response_t *res = conn_parse(conn);

    if (res != NULL) {

        conn_send_response(conn, res);
    } else {
        //debug("%s", conn_str(conn));
        const Request_t *req = conn_get_request(conn);
        if (req == &REQUEST_GET) {
            handle_get(conn);
        } else if (req == &REQUEST_PUT) {
            handle_put(conn);
        } else {
            handle_unsupported(conn);
        }
    }

    conn_delete(&conn);
}

void handle_get(conn_t *conn) {
    const Request_t *req = conn_get_request(conn);
    const char *method = request_get_str(req);

    char *uri = conn_get_uri(conn);
    //debug("Handling GET request for %s", uri);

    char *id_str = conn_get_header(conn, "Request-Id");
    if (!id_str) {
        id_str = "0";
    }

    //Use rwlock to create a critical region
    rwlock_t *rwl = NULL;
    pthread_mutex_lock(&mutex);
    if (!look_for_item(fl, uri, &rwl)) {
        rwl = rwlock_new(READERS, 0);
        insert_to_file_lock(fl, uri, rwl);
    }
    pthread_mutex_unlock(&mutex);
    reader_lock(rwl);

    // Open the file.
    int fd = open(uri, O_RDONLY);
    const Response_t *res = NULL;
    if (fd < 0) {
        // File open error
        if (errno == EACCES) {
            res = &RESPONSE_FORBIDDEN;
        } else if (errno == ENOENT) {
            res = &RESPONSE_NOT_FOUND;
        } else {
            res = &RESPONSE_INTERNAL_SERVER_ERROR;
        }
        uint16_t status_code = response_get_code(res);
        concat_audit_log(method, uri, status_code, id_str);
        conn_send_response(conn, res);

        reader_unlock(rwl);
        return;
    }

    // Get the size of the file.
    struct stat statbuf;
    if (fstat(fd, &statbuf) == -1) {
        close(fd);
        res = &RESPONSE_INTERNAL_SERVER_ERROR;
        uint16_t status_code = response_get_code(res);
        concat_audit_log(method, uri, status_code, id_str);
        conn_send_response(conn, res);

        reader_unlock(rwl);
        return;
    }

    // Check if the file is a directory。
    if (S_ISDIR(statbuf.st_mode)) {
        close(fd);
        res = &RESPONSE_FORBIDDEN;
        uint16_t status_code = response_get_code(res);
        concat_audit_log(method, uri, status_code, id_str);
        conn_send_response(conn, res);

        reader_unlock(rwl);
        return;
    }

    // Send the file
    concat_audit_log(method, uri, 200, id_str);
    const Response_t *response = conn_send_file(conn, fd, (uint64_t) statbuf.st_size);
    if (response != NULL) {
        // If sending the file failed, send an appropriate error response
        close(fd);
        res = &RESPONSE_FORBIDDEN;
        uint16_t status_code = response_get_code(res);
        concat_audit_log(method, uri, status_code, id_str);
        conn_send_response(conn, response);

        reader_unlock(rwl);
        return;
    }

    // Close the file
    close(fd);

    reader_unlock(rwl);
}

void handle_put(conn_t *conn) {
    const Request_t *req = conn_get_request(conn);
    const char *method = request_get_str(req);

    char *uri = conn_get_uri(conn);
    const Response_t *res = NULL;
    //debug("Handling PUT request for %s", uri);

    char *id_str = conn_get_header(conn, "Request-Id");
    if (!id_str) {
        id_str = "0";
    }

    // Create a temp file
    struct stat stat_temp;
    char temp_filename[] = "temp_fileXXXXXX";
    int temp_fd = mkstemp(temp_filename);
    if (temp_fd < 0) {
        res = &RESPONSE_INTERNAL_SERVER_ERROR;
        conn_send_response(conn, res);
        return;
    }
    res = conn_recv_file(conn, temp_fd);
    if (res != NULL) {
        conn_send_response(conn, res);
        close(temp_fd);
        unlink(temp_filename);
        return;
    }
    if (fstat(temp_fd, &stat_temp) == -1) {
        res = &RESPONSE_INTERNAL_SERVER_ERROR;
        conn_send_response(conn, res);
        close(temp_fd);
        unlink(temp_filename);
        return;
    }
    ssize_t temp_file_size = stat_temp.st_size;
    lseek(temp_fd, 0, SEEK_SET);


    rwlock_t *rwl = NULL;
    pthread_mutex_lock(&mutex);
    if (!look_for_item(fl, uri, &rwl)) {
        rwl = rwlock_new(WRITERS, 0);
        insert_to_file_lock(fl, uri, rwl);
    }
    pthread_mutex_unlock(&mutex);
    writer_lock(rwl);
    // Check if file already exists before opening it.
    bool file_exists = (access(uri, F_OK) == 0);

    // Open the file.
    struct stat statbuf;
    int fd = open(uri, O_WRONLY | O_CREAT | O_TRUNC, 0666); // Create or overwrite the file
    if (fd < 0) {
        // File open error
        if (errno == EACCES || S_ISDIR(statbuf.st_mode)) {
            res = &RESPONSE_FORBIDDEN;
        } else {
            res = &RESPONSE_INTERNAL_SERVER_ERROR;
        }
        uint16_t status_code = response_get_code(res);
        concat_audit_log(method, uri, status_code, id_str);
        conn_send_response(conn, res);
        close(temp_fd);
        unlink(temp_filename);
        writer_unlock(rwl);
        return;
    }

    // Receive the file
    uint16_t status_code = file_exists ? 200 : 201;
    concat_audit_log(method, uri, status_code, id_str);

    //Write from temp_file to target file;
    pass_n_bytes(temp_fd, fd, temp_file_size);

    // Send the response
    res = file_exists ? &RESPONSE_OK : &RESPONSE_CREATED;
    conn_send_response(conn, res);

    // Close the file
    close(temp_fd);
    unlink(temp_filename);
    close(fd);
    writer_unlock(rwl);
}

void handle_unsupported(conn_t *conn) {
    //debug("Handling unsupported request");

    // Send responses
    conn_send_response(conn, &RESPONSE_NOT_IMPLEMENTED);
}
