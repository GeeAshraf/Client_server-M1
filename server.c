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

// AES key setting 16 bit
unsigned char aes_key[16] = "ganna_securekey";

//clobal client counter
int client_count = 0;
pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;


// AES encrypt
void aes_encrypt(unsigned char *input, unsigned char *output) {
    AES_KEY enc_key;
    AES_set_encrypt_key(aes_key, 128, &enc_key);
    AES_encrypt(input, output, &enc_key);
}

// AES decrypt
void aes_decrypt(unsigned char *input, unsigned char *output) {
    AES_KEY dec_key;
    AES_set_decrypt_key(aes_key, 128, &dec_key);
    AES_decrypt(input, output, &dec_key);
}


// Thread function
void *handle_client(void *socket_desc) {
    int new_socket = *(int*)socket_desc;
    free(socket_desc);

    //assigning new clnt id
    pthread_mutex_lock(&lock);
    client_count++;
    int id = client_count;
    pthread_mutex_unlock(&lock);

    printf("Client %d connected\n", id);

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
    printf("Client %d authenticated\n", id);

    // RECEIVE encrypted msg
    read(new_socket, buffer, 16);
    aes_decrypt(buffer, decrypted);

    //make each client prints different msg
    if (id == 1) {
        printf("Client 1 says: %s\n", decrypted);
    } 
    else if (id == 2) {
        printf("Client 2 says: %s\n", decrypted);
    } 
    else if (id == 3) {
        printf("Client 3 says: %s\n", decrypted);
    } 
    else {
        printf("Client %d says: %s\n", id, decrypted);
    }

    //make each client reply with different msgs
    unsigned char response[16] = {0};

    if (id == 1) {
        memcpy(response, "Hi Client 1!!!", 16);
    } 
    else if (id == 2) {
        memcpy(response, "Hello Client 2!", 16);
    } 
    else if (id == 3) {
        memcpy(response, "Welcome Clnt 3", 16);
    } 
    else {
        memcpy(response, "Hello client!!", 16);
    }

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