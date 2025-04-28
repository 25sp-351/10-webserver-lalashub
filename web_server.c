#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <sys/stat.h>

#define BUFFER_SIZE 8192

void *handle_client(void *arg);

void send_response(int client_socket, int status_code, const char *content_type, const char *body, size_t body_length) {
    char header[512];
    sprintf(header,
            "HTTP/1.1 %d OK\r\n"
            "Content-Type: %s\r\n"
            "Content-Length: %zu\r\n"
            "Connection: close\r\n"
            "\r\n",
            status_code, content_type, body_length);

    send(client_socket, header, strlen(header), 0);
    send(client_socket, body, body_length, 0);
}

void send_file_response(int client_socket, const char *file_path) {
    int fd = open(file_path, O_RDONLY);
    if (fd < 0) {
        const char *msg = "File Not Found.";
        send_response(client_socket, 404, "text/plain", msg, strlen(msg));
        return;
    }

    struct stat st;
    fstat(fd, &st);
    size_t size = st.st_size;

    char *buffer = malloc(size);
    read(fd, buffer, size);

    const char *ext = strrchr(file_path, '.');
    const char *content_type = "application/octet-stream";
    if (ext) {
        if (strcmp(ext, ".html") == 0) content_type = "text/html";
        else if (strcmp(ext, ".png") == 0) content_type = "image/png";
        else if (strcmp(ext, ".jpg") == 0 || strcmp(ext, ".jpeg") == 0) content_type = "image/jpeg";
        else if (strcmp(ext, ".txt") == 0) content_type = "text/plain";
    }

    send_response(client_socket, 200, content_type, buffer, size);

    free(buffer);
    close(fd);
}

void handle_calc(int client_socket, char *operation, char *num1_str, char *num2_str) {
    double num1 = atof(num1_str);
    double num2 = atof(num2_str);
    double result = 0;
    char body[256];

    if (strcmp(operation, "add") == 0) result = num1 + num2;
    else if (strcmp(operation, "mul") == 0) result = num1 * num2;
    else if (strcmp(operation, "div") == 0) {
        if (num2 == 0) {
            sprintf(body, "<html><body><h1>Cannot divide by zero</h1></body></html>");
            send_response(client_socket, 400, "text/html", body, strlen(body));
            return;
        } else {
            result = num1 / num2;
        }
    } else {
        sprintf(body, "<html><body><h1>Invalid operation</h1></body></html>");
        send_response(client_socket, 400, "text/html", body, strlen(body));
        return;
    }

    sprintf(body, "<html><body><h1>Result: %.2f</h1></body></html>", result);
    send_response(client_socket, 200, "text/html", body, strlen(body));
}

void *handle_client(void *arg) {
    int client_socket = *(int *)arg;
    free(arg);

    char buffer[BUFFER_SIZE];
    memset(buffer, 0, BUFFER_SIZE);
    recv(client_socket, buffer, BUFFER_SIZE, 0);

    printf("Request:\n%s\n", buffer);

    char method[8], path[1024];
    sscanf(buffer, "%s %s", method, path);

    if (strcmp(method, "GET") != 0) {
        const char *msg = "Only GET supported.";
        send_response(client_socket, 400, "text/plain", msg, strlen(msg));
        close(client_socket);
        return NULL;
    }

    if (strncmp(path, "/static/", 8) == 0) {
        char file_path[1024] = "./static";
        strcat(file_path, path + 7); // remove /static prefix
        send_file_response(client_socket, file_path);

    } else if (strncmp(path, "/calc/", 6) == 0) {
        char operation[10], num1[50], num2[50];
        if (sscanf(path, "/calc/%[^/]/%[^/]/%s", operation, num1, num2) == 3) {
            handle_calc(client_socket, operation, num1, num2);
        } else {
            const char *msg = "Invalid calc URL.";
            send_response(client_socket, 400, "text/plain", msg, strlen(msg));
        }

    } else {
        const char *msg = "Unknown path.";
        send_response(client_socket, 404, "text/plain", msg, strlen(msg));
    }

    close(client_socket);
    return NULL;
}

int main(int argc, char *argv[]) {
    int port = 80;
    if (argc == 3 && strcmp(argv[1], "-p") == 0) {
        port = atoi(argv[2]);
    }

    int server_fd, new_socket;
    struct sockaddr_in address;
    int addrlen = sizeof(address);

    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == 0) {
        perror("socket failed");
        exit(EXIT_FAILURE);
    }

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(port);

    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address))<0) {
        perror("bind failed");
        exit(EXIT_FAILURE);
    }

    if (listen(server_fd, 10) < 0) {
        perror("listen");
        exit(EXIT_FAILURE);
    }

    printf("Listening on port %d...\n", port);

    while (1) {
        new_socket = accept(server_fd, (struct sockaddr *)&address, (socklen_t*)&addrlen);
        if (new_socket < 0) {
            perror("accept");
            continue;
        }

        int *pclient = malloc(sizeof(int));
        *pclient = new_socket;

        pthread_t t;
        pthread_create(&t, NULL, handle_client, pclient);
        pthread_detach(t);
    }

    close(server_fd);
    return 0;
}
