#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <openssl/aes.h>

#define PORT 8080
#define BUFFER_SIZE 1024
#define PSK "ganna_be_secure"

// AES key (same as server)
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

int main() {

    int sock;
    struct sockaddr_in server_address;
    char buffer[BUFFER_SIZE] = {0};

    sock = socket(AF_INET, SOCK_STREAM, 0);

    server_address.sin_family = AF_INET;
    server_address.sin_port = htons(PORT);
    server_address.sin_addr.s_addr = INADDR_ANY;

    connect(sock, (struct sockaddr*)&server_address, sizeof(server_address));

    // AUTH
    send(sock, PSK, strlen(PSK), 0);

    int bytes = read(sock, buffer, BUFFER_SIZE);
    buffer[bytes] = '\0';

    if (strcmp(buffer, "AUTH_OK") != 0) {
        printf("Authentication failed\n");
        close(sock);
        return 0;
    }

    printf("Authentication successful\n");

    // SEND encrypted message (16 bytes block)
    unsigned char message[16] = "Hello server!!";
    unsigned char encrypted[16] = {0};

    aes_encrypt(message, encrypted);
    send(sock, encrypted, 16, 0);

    // RECEIVE encrypted response
    unsigned char response[16] = {0};
    unsigned char decrypted[16] = {0};

    read(sock, response, 16);
    aes_decrypt(response, decrypted);

    printf("Server: %s\n", decrypted);

    close(sock);
    return 0;
}