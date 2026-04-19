#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <time.h>
#include <openssl/evp.h>

#define PORT 8080
#define BUF 4096

typedef enum { ENTRY, MEDIUM, TOP } level_t;

unsigned char KEY[32] = "01234567890123456789012345678901";
unsigned char IV[16]  = "0123456789012345";

pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;

//AES
int encrypt(unsigned char *p, int len, unsigned char *c) {
    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    int l, cl;
    EVP_EncryptInit_ex(ctx, EVP_aes_256_cbc(), NULL, KEY, IV);
    EVP_EncryptUpdate(ctx, c, &l, p, len); cl = l;
    EVP_EncryptFinal_ex(ctx, c + l, &l); cl += l;
    EVP_CIPHER_CTX_free(ctx);
    return cl;
}

int decrypt(unsigned char *c, int len, unsigned char *p) {
    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    int l, pl;
    EVP_DecryptInit_ex(ctx, EVP_aes_256_cbc(), NULL, KEY, IV);
    EVP_DecryptUpdate(ctx, p, &l, c, len); pl = l;
    EVP_DecryptFinal_ex(ctx, p + l, &l); pl += l;
    EVP_CIPHER_CTX_free(ctx);
    return pl;
}

// soc logging
void log_event(const char *type, const char *user, const char *lvl, const char *msg) {
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    char ts[64];
    strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", t);

    pthread_mutex_lock(&lock);
    printf("[%s] [%-5s] [%s@%-6s] %s\n", ts, type, user, lvl, msg);
    pthread_mutex_unlock(&lock);
}
//communication
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

//auth
int authenticate(char *u, char *p, level_t *lvl) {
    FILE *f = fopen("users.txt", "r");
    if (!f) return 0;
    char U[50], P[50], R[20];
    while (fscanf(f, "%s %s %s", U, P, R) == 3) {
        if (!strcmp(u, U) && !strcmp(p, P)) {
            if (!strcmp(R, "entry")) *lvl = ENTRY;
            else if (!strcmp(R, "medium")) *lvl = MEDIUM;
            else *lvl = TOP;
            fclose(f);
            return 1;
        }
    }
    fclose(f);
    return 0;
}

//client thread
void *client_thread(void *arg) {
    int sock = *(int*)arg; free(arg);
    char buf[BUF], user[50] = "UNKNOWN", pass[50];
    level_t lvl;

    if (recv_msg(sock, buf) <= 0) { close(sock); return NULL; }
    sscanf(buf, "%s %s", user, pass);

    if (!authenticate(user, pass, &lvl)) {
        log_event("WARN", user, "NONE", "AUTH_FAILED");
        send_msg(sock, "FAIL");
        close(sock);
        return NULL;
    }

    char *L = (lvl == ENTRY) ? "ENTRY" : (lvl == MEDIUM) ? "MEDIUM" : "TOP";
    log_event("INFO", user, L, "SESSION_START");
    send_msg(sock, "AUTH_OK");
    send_msg(sock, L);

    while (1) {
        char cmd[BUF], res[BUF] = {0};
        if (recv_msg(sock, cmd) <= 0) break;

        
        if (!strcmp(cmd, "exit")) {
            log_event("INFO", user, L, "SESSION_END");
            break; 
        }

        char logmsg[BUF];
        snprintf(logmsg, sizeof(logmsg), "EXECUTE: %s", cmd);
        log_event("CMD", user, L, logmsg);

        char c[50] = {0}, a[200] = {0}, a2[200] = {0};
        sscanf(cmd, "%s %s %s", c, a, a2);

        //role logic
        if (lvl == ENTRY) {
            if (!strcmp(c, "ls")) {
                FILE *fp = popen("ls", "r");
                fread(res, 1, BUF - 1, fp);
                pclose(fp);
            } else if (!strcmp(c, "cat")) {
                FILE *fp = fopen(a, "r");
                if (!fp) strcpy(res, "File not found\n");
                else { fread(res, 1, BUF - 1, fp); fclose(fp); }
            } else {
                strcpy(res, "ACCESS DENIED\n");
            }
            send_msg(sock, res);
        } 
        else if (lvl == MEDIUM) {
            if (!strcmp(c, "ls") || !strcmp(c, "cat")) {
                char shell_cmd[BUF];
                snprintf(shell_cmd, sizeof(shell_cmd), "%s 2>&1", cmd);
                FILE *fp = popen(shell_cmd, "r");
                fread(res, 1, BUF - 1, fp);
                pclose(fp);
                send_msg(sock, res);
            } else if (!strcmp(c, "cp")) {
                FILE *f1 = fopen(a, "rb"), *f2 = fopen(a2, "wb");
                if (!f1 || !f2) strcpy(res, "Copy Error\n");
                else {
                    char b[BUF]; int n;
                    while ((n = fread(b, 1, BUF, f1)) > 0) fwrite(b, 1, n, f2);
                    fclose(f1); fclose(f2);
                    strcpy(res, "COPY DONE\n");
                }
                send_msg(sock, res);
            } else if (!strcmp(c, "write")) {
                FILE *fp = fopen(a, "w");
                send_msg(sock, "Enter lines (type 'END' to finish):\n");
                while (1) {
                    recv_msg(sock, buf);
                    if (!strcmp(buf, "END")) break;
                    fprintf(fp, "%s\n", buf);
                }
                fclose(fp);
                send_msg(sock, "WRITE SUCCESSFUL\n");
            } else {
                send_msg(sock, "ACCESS DENIED\n");
            }
        } 
        else { //top user
            if (!strcmp(c, "download")) {
                FILE *fp = fopen(a, "rb");
                int n; char b[BUF];
                if (!fp) { n = -1; send(sock, &n, sizeof(int), 0); }
                else {
                    while ((n = fread(b, 1, BUF, fp)) > 0) {
                        send(sock, &n, sizeof(int), 0);
                        send(sock, b, n, 0);
                    }
                    n = 0; send(sock, &n, sizeof(int), 0);
                    fclose(fp);
                }
            } else if (!strcmp(c, "upload")) {
                FILE *fp = fopen(a, "wb");
                int n; char b[BUF];
                while (1) {
                    recv(sock, &n, sizeof(int), 0);
                    if (n <= 0) break;
                    int t = 0; while (t < n) t += recv(sock, b + t, n - t, 0);
                    fwrite(b, 1, n, fp);
                }
                fclose(fp);
                send_msg(sock, "UPLOAD SUCCESSFUL\n");
            } else if (!strcmp(c, "write")) {
                FILE *fp = fopen(a, "w");
                send_msg(sock, "Enter lines (type 'END' to finish):\n");
                while (1) {
                    recv_msg(sock, buf);
                    if (!strcmp(buf, "END")) break;
                    fprintf(fp, "%s\n", buf);
                }
                fclose(fp);
                send_msg(sock, "WRITE SUCCESSFUL\n");
            } else {
                
                char shell_cmd[BUF + 10];
                snprintf(shell_cmd, sizeof(shell_cmd), "%s 2>&1", cmd);
                FILE *fp = popen(shell_cmd, "r");
                if (fp) {
                    size_t bytes = fread(res, 1, BUF - 1, fp);
                    if (bytes == 0) strcpy(res, "OK (Command executed)\n");
                    pclose(fp);
                } else {
                    strcpy(res, "Execution Error\n");
                }
                send_msg(sock, res);
            }
        }
    }
    close(sock);
    return NULL;
}

int main() {
    int s = socket(AF_INET, SOCK_STREAM, 0);
    int opt = 1;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in a = {0};
    a.sin_family = AF_INET;
    a.sin_port = htons(PORT);
    a.sin_addr.s_addr = INADDR_ANY;

    bind(s, (struct sockaddr*)&a, sizeof(a));
    listen(s, 5);

    printf("server active on port %d\n", PORT);
    printf("---------------------------------------------------------------\n");

    while (1) {
        int *c = malloc(sizeof(int));
        *c = accept(s, NULL, NULL);
        log_event("NET", "SYSTEM", "ROOT", "New connection established");
        pthread_t t;
        pthread_create(&t, NULL, client_thread, c);
        pthread_detach(t);
    }
    return 0;
}