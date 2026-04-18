#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <sys/time.h>

#define PORT 8080
#define BUF 4096
#define TIMEOUT_SEC 300

typedef enum { ENTRY, MEDIUM, TOP } level_t;

// safe path
int safe_path(char *arg) {
    return !(strstr(arg, "..") || arg[0] == '/');
}

// recv
int recv_msg(int sock, char *buf, int size) {
    int len = 0;
    if (recv(sock, &len, sizeof(int), 0) <= 0) return -1;
    if (len <= 0 || len >= size) return -1;

    int total = 0;
    while (total < len) {
        int n = recv(sock, buf + total, len - total, 0);
        if (n <= 0) return -1;
        total += n;
    }
    buf[len] = 0;
    return len;
}

// send
int send_msg(int sock, const char *buf) {
    int len = strlen(buf);
    send(sock, &len, sizeof(int), 0);
    return send(sock, buf, len, 0);
}

// auth
int authenticate(char *user, char *pass, level_t *lvl) {
    FILE *f = fopen("./users.txt", "r");
    if (!f) return 0;

    char u[50], p[50], role[20];

    while (fscanf(f, "%49s %49s %19s", u, p, role) == 3) {
        if (!strcmp(user, u) && !strcmp(pass, p)) {
            if (!strcmp(role, "entry")) *lvl = ENTRY;
            else if (!strcmp(role, "medium")) *lvl = MEDIUM;
            else *lvl = TOP;
            fclose(f);
            return 1;
        }
    }
    fclose(f);
    return 0;
}

// ls
int do_ls(char *res, int size) {
    FILE *fp = popen("ls", "r");
    int n = fread(res, 1, size - 1, fp);
    res[n] = 0;
    pclose(fp);
    return strlen(res);
}

// cat
int do_cat(char *arg, char *res, int size) {
    if (!safe_path(arg)) return sprintf(res, "INVALID PATH\n");

    FILE *fp = fopen(arg, "r");
    if (!fp) return sprintf(res, "FILE NOT FOUND\n");

    int n = fread(res, 1, size - 1, fp);
    res[n] = 0;
    fclose(fp);
    return strlen(res);
}

// rm
int do_rm(char *arg, char *res) {
    if (!safe_path(arg)) return sprintf(res, "INVALID PATH\n");

    return (remove(arg) == 0) ?
        sprintf(res, "FILE DELETED\n") :
        sprintf(res, "DELETE FAILED\n");
}

// cp
int do_cp(char *src, char *dst, char *res) {
    if (!safe_path(src) || !safe_path(dst))
        return sprintf(res, "INVALID PATH\n");

    FILE *f1 = fopen(src, "rb");
    if (!f1) return sprintf(res, "SOURCE NOT FOUND\n");

    FILE *f2 = fopen(dst, "wb");
    if (!f2) {
        fclose(f1);
        return sprintf(res, "CANNOT CREATE DEST\n");
    }

    char buf[BUF];
    int n;
    while ((n = fread(buf, 1, BUF, f1)) > 0)
        fwrite(buf, 1, n, f2);

    fclose(f1);
    fclose(f2);
    return sprintf(res, "COPY DONE\n");
}

// download
int do_download(int sock, char *arg) {
    if (!safe_path(arg)) {
        send_msg(sock, "INVALID PATH");
        return -1;
    }

    FILE *fp = fopen(arg, "rb");
    if (!fp) {
        send_msg(sock, "FILE NOT FOUND");
        return -1;
    }

    char buf[BUF];
    int n;

    while ((n = fread(buf, 1, BUF, fp)) > 0) {
        send(sock, &n, sizeof(int), 0);
        send(sock, buf, n, 0);
    }

    n = 0;
    send(sock, &n, sizeof(int), 0);

    fclose(fp);
    return 0;
}

// upload
int do_upload(int sock, char *arg) {

    if (!safe_path(arg)) {
        send_msg(sock, "INVALID PATH");
        return -1;
    }

    int n;
    if (recv(sock, &n, sizeof(int), 0) <= 0)
        return -1;

    if (n == 0)
        return -1;

    FILE *fp = fopen(arg, "wb");
    if (!fp) {
        send_msg(sock, "CANNOT CREATE FILE");
        return -1;
    }

    char buffer[BUF];

    while (1) {
        int total = 0;

        while (total < n) {
            int r = recv(sock, buffer + total, n - total, 0);
            if (r <= 0) break;
            total += r;
        }

        fwrite(buffer, 1, n, fp);

        if (recv(sock, &n, sizeof(int), 0) <= 0)
            break;

        if (n == 0) break;
    }

    fclose(fp);
    send_msg(sock, "UPLOAD DONE\n");
    return 0;
}

// write (FIXED PROTOCOL)
int do_write(int sock, char *file) {

    if (!safe_path(file)) {
        send_msg(sock, "INVALID PATH\n");
        return -1;
    }

    FILE *fp = fopen(file, "w");
    if (!fp) {
        send_msg(sock, "CANNOT OPEN FILE\n");
        return -1;
    }

    send_msg(sock, "START EDITING (END to finish)\n");

    int count;
    if (recv(sock, &count, sizeof(int), 0) <= 0) {
        fclose(fp);
        return -1;
    }

    char buf[BUF];

    for (int i = 0; i < count; i++) {
        if (recv_msg(sock, buf, sizeof(buf)) <= 0)
            break;

        fprintf(fp, "%s", buf);
    }

    fclose(fp);

    send_msg(sock, "WRITE DONE\n");

    return 0;
}

// command
int execute_command(char *cmd, char *res, int size, level_t lvl) {

    char c[50]={0}, a[200]={0}, a2[200]={0};
    sscanf(cmd, "%49s %199s %199s", c, a, a2);

    if (!strcmp(c,"help")) {
        if (lvl==ENTRY) return sprintf(res,"ls, cat\n");
        if (lvl==MEDIUM) return sprintf(res,"ls, cat, whoami, cp, write\n");
        return sprintf(res,"ALL COMMANDS\n");
    }

    if (lvl==ENTRY) {
        if (!strcmp(c,"ls")) return do_ls(res,size);
        if (!strcmp(c,"cat")) return do_cat(a,res,size);
        return sprintf(res,"ACCESS DENIED\n");
    }

    if (lvl==MEDIUM) {
        if (!strcmp(c,"ls")) return do_ls(res,size);
        if (!strcmp(c,"cat")) return do_cat(a,res,size);
        if (!strcmp(c,"whoami")) return sprintf(res,"MEDIUM\n");
        if (!strcmp(c,"cp")) return do_cp(a,a2,res);
        if (!strcmp(c,"write")) return -4;
        return sprintf(res,"ACCESS DENIED\n");
    }

    if (lvl==TOP) {
        if (!strcmp(c,"ls")) return do_ls(res,size);
        if (!strcmp(c,"cat")) return do_cat(a,res,size);
        if (!strcmp(c,"whoami")) return sprintf(res,"TOP\n");
        if (!strcmp(c,"rm")) return do_rm(a,res);
        if (!strcmp(c,"download")) return -2;
        if (!strcmp(c,"upload")) return -3;
        if (!strcmp(c,"write")) return -4;
        return sprintf(res,"OK\n");
    }

    return 0;
}

// thread
void *client_thread(void *arg) {
    int sock = *(int*)arg;
    free(arg);

    char buf[BUF], user[50], pass[50];
    level_t lvl;

    struct timeval tv = {TIMEOUT_SEC,0};
    setsockopt(sock,SOL_SOCKET,SO_RCVTIMEO,&tv,sizeof(tv));

    if (recv_msg(sock,buf,sizeof(buf))<=0) { close(sock); return NULL; }

    sscanf(buf,"%s %s",user,pass);
    printf("Login: %s\n",user);

    if (!authenticate(user,pass,&lvl)) {
        send_msg(sock,"FAIL");
        close(sock);
        return NULL;
    }

    printf("%s logged in\n", user);

    send_msg(sock,"AUTH_OK");
    send_msg(sock,(lvl==ENTRY)?"ENTRY":(lvl==MEDIUM)?"MEDIUM":"TOP");

    while (1) {
        char cmd[BUF], res[BUF];

        if (recv_msg(sock,cmd,sizeof(cmd))<=0) break;

        int st = execute_command(cmd,res,sizeof(res),lvl);

        if (st==-2) { char a[200]; sscanf(cmd,"%*s %s",a); do_download(sock,a); }
        else if (st==-3){ char a[200]; sscanf(cmd,"%*s %s",a); do_upload(sock,a); }
        else if (st==-4){ char a[200]; sscanf(cmd,"%*s %s",a); do_write(sock,a); }
        else send_msg(sock,res);
    }

    printf("%s disconnected\n", user);
    close(sock);
    return NULL;
}

// main
int main() {
    int s=socket(AF_INET,SOCK_STREAM,0);
    int opt=1;
    setsockopt(s,SOL_SOCKET,SO_REUSEADDR,&opt,sizeof(opt));

    struct sockaddr_in addr={0};
    addr.sin_family=AF_INET;
    addr.sin_port=htons(PORT);
    addr.sin_addr.s_addr=INADDR_ANY;

    bind(s,(struct sockaddr*)&addr,sizeof(addr));
    listen(s,5);

    printf("Server running on %d\n",PORT);

    while (1) {
        int *c=malloc(sizeof(int));
        *c=accept(s,NULL,NULL);

        printf("Client connected\n");

        pthread_t t;
        pthread_create(&t,NULL,client_thread,c);
        pthread_detach(t);
    }
}