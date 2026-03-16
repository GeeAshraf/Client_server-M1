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

    int sock;
    struct sockaddr_in server_address;
    char buffer[BUFFER_SIZE] = {0};

    sock = socket(AF_INET, SOCK_STREAM, 0);

    server_address.sin_family = AF_INET;
    server_address.sin_port = htons(PORT);
    server_address.sin_addr.s_addr = INADDR_ANY;

    connect(sock, (struct sockaddr*)&server_address, sizeof(server_address));

    /* AUTHENTICATION */

    send(sock, PSK, strlen(PSK), 0);

    int bytes = read(sock, buffer, BUFFER_SIZE);
    buffer[bytes] = '\0';

    if (strcmp(buffer, "AUTH_OK") != 0) {
        printf("Authentication failed\n");
        close(sock);
        return 0;
    }

    printf("Authentication successful\n");

    //send encrypted message

    char message[] = "Hello from client";
    int len = strlen(message);

    xor_encrypt_decrypt(message, len);

    send(sock, message, len, 0);

    // RECEIVE ENCRYPTED RESPONSE

    bytes = read(sock, buffer, BUFFER_SIZE);

    xor_encrypt_decrypt(buffer, bytes);

    printf("Server: %.*s\n", bytes, buffer);

    close(sock);

    return 0;
}