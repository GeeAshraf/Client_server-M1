#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <openssl/aes.h>

#define PORT 8080
#define BUFFER_SIZE 1024
#define PSK "ganna_be_secure"

// AES key (16 bytes = 128-bit)
unsigned char aes_key[16] = "ganna_securekey";

// AES Encrypt
void aes_encrypt(unsigned char *input, unsigned char *output) {
    AES_KEY enc_key;
    AES_set_encrypt_key(aes_key, 128, &enc_key);
    AES_encrypt(input, output, &enc_key);
}

// AES Decrypt
void aes_decrypt(unsigned char *input, unsigned char *output) {
    AES_KEY dec_key;
    AES_set_decrypt_key(aes_key, 128, &dec_key);
    AES_decrypt(input, output, &dec_key);
}

// Thread function
void *handle_client(void *socket_desc) {
    int new_socket = *(int*)socket_desc;
    free(socket_desc);

    unsigned char buffer[16] = {0};
    unsigned char decrypted[16] = {0};

    // AUTH
    char client_key[BUFFER_SIZE] = {0};
    int key_len = read(new_socket, client_key, BUFFER_SIZE);
    client_key[key_len] = '\0';

    if (strcmp(client_key, PSK) != 0) {
        send(new_socket, "AUTH_FAIL", 9, 0);
        close(new_socket);
        return NULL;
    }

    send(new_socket, "AUTH_OK", 7, 0);
    printf("Client authenticated\n");

    // RECEIVE encrypted message
    read(new_socket, buffer, 16);

    aes_decrypt(buffer, decrypted);
    printf("Client: %s\n", decrypted);

    // SEND encrypted response
    unsigned char response[16] = "Hello client!!";
    unsigned char encrypted[16] = {0};

    aes_encrypt(response, encrypted);
    send(new_socket, encrypted, 16, 0);

    close(new_socket);
    return NULL;
}

int main() {
    int server_fd;
    struct sockaddr_in address;
    int addrlen = sizeof(address);

    server_fd = socket(AF_INET, SOCK_STREAM, 0);

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);

    bind(server_fd, (struct sockaddr*)&address, sizeof(address));
    listen(server_fd, 5);

    printf("Server listening on port %d...\n", PORT);

    while (1) {
        int *new_socket = malloc(sizeof(int));
        *new_socket = accept(server_fd, (struct sockaddr*)&address, (socklen_t*)&addrlen);

        pthread_t thread_id;
        pthread_create(&thread_id, NULL, handle_client, (void*)new_socket);
        pthread_detach(thread_id);
    }

    close(server_fd);
    return 0;
}