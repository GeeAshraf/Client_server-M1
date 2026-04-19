#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <openssl/evp.h>

#define PORT 8080
#define BUF 4096

unsigned char KEY[32] = "01234567890123456789012345678901";
unsigned char IV[16]  = "0123456789012345";

//AES encrypt
int encrypt(unsigned char *plaintext, int len, unsigned char *cipher) {
    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    int l, cl;
    EVP_EncryptInit_ex(ctx, EVP_aes_256_cbc(), NULL, KEY, IV);
    EVP_EncryptUpdate(ctx, cipher, &l, plaintext, len); cl = l;
    EVP_EncryptFinal_ex(ctx, cipher + l, &l); cl += l;
    EVP_CIPHER_CTX_free(ctx);
    return cl;
}

//AES decrrypt
int decrypt(unsigned char *cipher, int len, unsigned char *plain) {
    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    int l, pl;
    EVP_DecryptInit_ex(ctx, EVP_aes_256_cbc(), NULL, KEY, IV);
    EVP_DecryptUpdate(ctx, plain, &l, cipher, len); pl = l;
    EVP_DecryptFinal_ex(ctx, plain + l, &l); pl += l;
    EVP_CIPHER_CTX_free(ctx);
    return pl;
}

void send_msg(int sock, const char *msg) {
    unsigned char enc[BUF];
    int len = encrypt((unsigned char*)msg, strlen(msg), enc);
    send(sock, &len, sizeof(int), 0);
    send(sock, enc, len, 0);
}

int recv_msg(int sock, char *buf) {
    int len;
    if (recv(sock, &len, sizeof(int), 0) <= 0) return -1;
    unsigned char enc[BUF], dec[BUF];
    int t = 0;
    while (t < len) {
        int r = recv(sock, enc + t, len - t, 0);
        if (r <= 0) return -1;
        t += r;
    }
    int dl = decrypt(enc, len, dec);
    dec[dl] = 0;
    memcpy(buf, dec, dl + 1);
    return dl;
}

void download(int sock, char *f) {
    FILE *fp = fopen(f, "wb");
    if (!fp) { printf("File error\n"); return; }
    int n; char b[BUF];
    while (1) {
        if (recv(sock, &n, sizeof(int), 0) <= 0) break;
        if (n <= 0) break;
        int t = 0;
        while (t < n) t += recv(sock, b + t, n - t, 0);
        fwrite(b, 1, n, fp);
    }
    fclose(fp);
    printf("Download complete.\n");
}

void upload(int sock, char *f) {
    FILE *fp = fopen(f, "rb");
    if (!fp) { printf("File not found locally.\n"); return; }
    char b[BUF]; int n;
    while ((n = fread(b, 1, BUF, fp)) > 0) {
        send(sock, &n, sizeof(int), 0);
        send(sock, b, n, 0);
    }
    n = 0; send(sock, &n, sizeof(int), 0);
    fclose(fp);
    char r[100]; recv_msg(sock, r);
    printf("%s", r);
}

void write_file(int sock) {
    char r[200]; recv_msg(sock, r);
    printf("%s", r);
    char line[BUF];
    while (1) {
        printf("> ");
        fgets(line, BUF, stdin);
        line[strcspn(line, "\n")] = 0;
        send_msg(sock, line);
        if (!strcmp(line, "END")) break;
    }
    recv_msg(sock, r);
    printf("%s", r);
}

int main() {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in s = {0};
    s.sin_family = AF_INET;
    s.sin_port = htons(PORT);
    s.sin_addr.s_addr = inet_addr("127.0.0.1");

    if (connect(sock, (struct sockaddr*)&s, sizeof(s)) < 0) {
        perror("Connection failed");
        return 1;
    }
    printf("Connected to server\n");

    char u[50], p[50];
    printf("Username: "); scanf("%s", u);
    printf("Password: "); scanf("%s", p);

    char cred[100];
    sprintf(cred, "%s %s", u, p);
    send_msg(sock, cred);

    char r[BUF];
    recv_msg(sock, r);
    if (!strcmp(r, "FAIL")) {
        printf("Auth Failed.\n");
        return 0;
    }

    recv_msg(sock, r);
    printf("Authenticated. Role: %s\n", r);
    getchar(); // Clear buffer

    while (1) {
        char cmd[BUF];
        printf(">> ");
        if (!fgets(cmd, BUF, stdin)) break;
        cmd[strcspn(cmd, "\n")] = 0;
        if (strlen(cmd) == 0) continue;

        
        if (!strcmp(cmd, "exit")) {
            send_msg(sock, "exit");
            break; 
        }

        char c[50] = {0}, a[200] = {0};
        sscanf(cmd, "%s %s", c, a);

        if (!strcmp(c, "upload")) {
            send_msg(sock, cmd);
            upload(sock, a);
        } else if (!strcmp(c, "download")) {
            send_msg(sock, cmd);
            download(sock, a);
        } else if (!strcmp(c, "write")) {
            send_msg(sock, cmd);
            write_file(sock);
        } else {
            send_msg(sock, cmd);
            if (recv_msg(sock, r) > 0) printf("%s", r);
        }
    }

    printf("Exiting...\n");
    close(sock);
    return 0;
}