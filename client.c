#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>

#define PORT 8080
#define BUF 4096

// send
void send_msg(int sock, const char *msg) {
    int len = strlen(msg);
    send(sock, &len, sizeof(int), 0);
    send(sock, msg, len, 0);
}

// recv
int recv_msg(int sock, char *buf, int size) {
    int len = 0;

    if (recv(sock, &len, sizeof(int), 0) <= 0)
        return -1;

    if (len <= 0 || len >= size)
        return -1;

    int total = 0;
    while (total < len) {
        int n = recv(sock, buf + total, len - total, 0);
        if (n <= 0) return -1;
        total += n;
    }

    buf[len] = 0;
    return len;
}

// download
void download_file(int sock, char *filename) {
    FILE *fp = fopen(filename, "wb");
    if (!fp) {
        printf("Cannot create file\n");
        return;
    }

    int n;
    char buffer[BUF];

    while (1) {
        if (recv(sock, &n, sizeof(int), 0) <= 0)
            break;

        if (n == 0) break;

        int total = 0;
        while (total < n) {
            int r = recv(sock, buffer + total, n - total, 0);
            total += r;
        }

        fwrite(buffer, 1, n, fp);
    }

    fclose(fp);
    printf("Download complete\n");
}

// upload
void upload_file(int sock, char *filename) {

    FILE *fp = fopen(filename, "rb");
    if (!fp) {
        printf("File not found on client side\n");
        return;
    }

    char buffer[BUF];
    int n;

    while ((n = fread(buffer, 1, BUF, fp)) > 0) {
        send(sock, &n, sizeof(int), 0);
        send(sock, buffer, n, 0);
    }

    n = 0;
    send(sock, &n, sizeof(int), 0);

    fclose(fp);

    char response[100];
    recv_msg(sock, response, sizeof(response));
    printf("%s", response);
}

// write 
void write_file(int sock, char *filename) {

    char response[200];

    if (recv_msg(sock, response, sizeof(response)) <= 0)
        return;

    printf("%s", response);

    char lines[100][BUF];
    char line[BUF];
    int count = 0;

    while (count < 100) {

        printf("> ");
        fflush(stdout);

        if (!fgets(line, BUF, stdin))
            break;

        // remove newline
        line[strcspn(line, "\r\n")] = 0;

        // detect END
        if (strcmp(line, "END") == 0)
            break;

        // safe copy + newline
        strncpy(lines[count], line, BUF - 2);
        lines[count][BUF - 2] = '\0';
        strcat(lines[count], "\n");

        count++;
    }

    // send count
    send(sock, &count, sizeof(int), 0);

    // send lines
    for (int i = 0; i < count; i++) {
        send_msg(sock, lines[i]);
    }

    // receive done
    if (recv_msg(sock, response, sizeof(response)) > 0)
        printf("%s", response);
}

// main
int main() {

    int sock = socket(AF_INET, SOCK_STREAM, 0);

    struct sockaddr_in server;
    server.sin_family = AF_INET;
    server.sin_port = htons(PORT);
    server.sin_addr.s_addr = inet_addr("127.0.0.1");

    if (connect(sock, (struct sockaddr*)&server, sizeof(server)) < 0) {
        perror("connect");
        return 1;
    }

    printf("Connected to server\n");

    char user[50], pass[50];

    printf("Username: ");
    scanf("%49s", user);

    printf("Password: ");
    scanf("%49s", pass);

    char credentials[120];
    snprintf(credentials, sizeof(credentials), "%s %s", user, pass);
    send_msg(sock, credentials);

    char response[100];

    if (recv_msg(sock, response, sizeof(response)) <= 0)
        return 1;

    if (strcmp(response, "FAIL") == 0) {
        printf("Authentication failed\n");
        return 0;
    }

    printf("Authenticated\n");

    if (recv_msg(sock, response, sizeof(response)) <= 0)
        return 1;

    printf("Your role: %s\n", response);

    getchar(); // clear newline

    // command loop
    while (1) {

        char cmd[BUF];
        char result[BUF];

        printf(">> ");
        fflush(stdout);

        if (!fgets(cmd, sizeof(cmd), stdin))
            break;

        // clean input
        cmd[strcspn(cmd, "\r\n")] = 0;
        if (strlen(cmd) == 0)
            continue;

        char command[50]={0}, arg[200]={0};
        sscanf(cmd,"%49s %199s",command,arg);

        if (!strcmp(command,"upload")) {

            FILE *fp=fopen(arg,"rb");
            if(!fp){
                printf("File not found on client side\n");
                continue;
            }
            fclose(fp);

            send_msg(sock,cmd);
            upload_file(sock,arg);
        }

        else if (!strcmp(command,"download")) {
            send_msg(sock,cmd);
            download_file(sock,arg);
        }

        else if (!strcmp(command,"write")) {
            send_msg(sock,cmd);
            write_file(sock,arg);

            // clear leftover input
            int c;
            while ((c = getchar()) != '\n' && c != EOF);
        }

        else {
            send_msg(sock,cmd);

            if (recv_msg(sock,result,sizeof(result))<=0) {
                printf("Server disconnected\n");
                break;
            }

            printf("%s\n",result);
        }
    }

    close(sock);
    return 0;
}