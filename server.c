#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>

#define PORT 8080
#define BUFFER_SIZE 1024
#define PSK "ganna_be_secure"

void xor_encrypt_decrypt(char *data, int len) {
    int key_len = strlen(PSK);
    for (int i = 0; i < len; i++) {
        data[i] ^= PSK[i % key_len];
    }
}

int main() {
    int server_fd, new_socket;
    struct sockaddr_in address;
    int addrlen = sizeof(address);
    char buffer[BUFFER_SIZE] = {0};

    server_fd = socket(AF_INET, SOCK_STREAM, 0);

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);

    bind(server_fd, (struct sockaddr*)&address, sizeof(address));
    listen(server_fd, 3);

    printf("Server listening on port %d...\n", PORT);

    new_socket = accept(server_fd, (struct sockaddr*)&address, (socklen_t*)&addrlen);

    //auth

    char client_key[BUFFER_SIZE] = {0};
    int key_len = read(new_socket, client_key, BUFFER_SIZE);

    client_key[key_len] = '\0';

    if (strcmp(client_key, PSK) != 0) {
        send(new_socket, "AUTH_FAIL", 9, 0);
        close(new_socket);
        close(server_fd);
        return 0;
    }

    send(new_socket, "AUTH_OK", 7, 0);
    printf("Client authenticated successfully\n");

    //recieve encrypt msg

    int bytes = read(new_socket, buffer, BUFFER_SIZE);

    xor_encrypt_decrypt(buffer, bytes);

    printf("Decrypted client message: %.*s\n", bytes, buffer);

    //send encrypted response

    char response[] = "Hello from secure server";
    int len = strlen(response);

    xor_encrypt_decrypt(response, len);

    send(new_socket, response, len, 0);

    close(new_socket);
    close(server_fd);

    return 0;
}