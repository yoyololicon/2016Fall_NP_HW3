#include <iostream>
#include <unistd.h>
#include <netinet/in.h>
#include <string.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
using namespace std;
#define MAXLINE 1024

void str_cli(FILE *, int, char*);
int translate_file(char*, char*);

int main(int argc, char **argv)
{
    int                         sockfd;
    struct sockaddr_in          servaddr;
    struct hostent              *hints;

    if(argc != 4)
    {
        cout << "./client <ip> <port> <username>" << endl;
        return 1;
    }
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if(sockfd < 0)
        cerr << "ERROR opening socket" << endl;

    hints = gethostbyname(argv[1]);
    if(hints == NULL)
    {
        cerr << "no such host" << endl;
        return 1;
    }

    bzero(&servaddr, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_port = htons((uint16_t )atoi(argv[2]));
    bcopy(hints->h_addr, (char*)&servaddr.sin_addr.s_addr, (size_t)hints->h_length);

    if(connect(sockfd, (struct sockaddr *) &servaddr, sizeof(servaddr)) < 0)
    {
        cerr << "connection fail" << endl;
        return 1;
    }

    char id[50] = "CLIENTID: " ;
    strcat(id, argv[3]);
    write(sockfd, id, strlen(id));

    str_cli(stdin, sockfd, argv[1]);

    return 0;
}

void str_cli(FILE *fp, int sockfd, char *addr)
{
    int     maxfdp1, stdineof, sfd, rfd, n, upload, download, total_bytes = 0, seconds;
    off_t   fsize;
    fd_set  rset;
    char    sendline[MAXLINE], recvline[MAXLINE];
    string  fname;

    stdineof = 0;
    FD_ZERO(&rset);
    upload = 0;
    download = 0;
    for(;;)
    {
        if(stdineof == 0)
            FD_SET(fileno(fp), &rset);
        FD_SET(sockfd, &rset);

        maxfdp1 = max(fileno(fp), sockfd)+1;

        select(maxfdp1, &rset, NULL, NULL, NULL);

        if(FD_ISSET(sockfd, &rset))
        {
            if((n = read(sockfd, recvline, MAXLINE)) == 0)
            {
                if(stdineof != 1)
                    cout << "serv terminated prematurely" << endl;
                return;
            }
            recvline[n] = 0;
            if(upload)
            {
                if((rfd = translate_file(recvline, addr)) <= 0)
                    continue;
                cout << "Uploading file : " << fname << endl;
                cout << "Progress : [";
                cout.flush();
                while((n = read(sfd, sendline, MAXLINE)))
                {
                    if(n < 0)
                    {
                        printf("Read error\n");
                        break;
                    }
                    n = write(rfd,sendline,(size_t )n);
                    if(n < 0)
                    {
                        printf("Write error\n");
                        break;
                    }
                    total_bytes+=n;
                    if(total_bytes >= fsize)
                    {
                        cout << "#";
                        cout.flush();
                        total_bytes-=fsize;
                    }
                }
                cout << "]" << endl;
                close(sfd);
                close(rfd);
                upload = total_bytes = 0;
                cout << "Upload " << fname << " complete!" << endl;
            }
            else if(download)
            {
                if((sfd = translate_file(recvline, addr)) <= 0)
                    continue;
                cout << "Downloading file : " << fname << endl;
                cout << "Progress : [";
                cout.flush();
                while((n = read(sfd, recvline, MAXLINE)))
                {
                    if(n < 0)
                    {
                        printf("Read error\n");
                        break;
                    }
                    n = write(rfd, recvline,(size_t )n);
                    if(n < 0)
                    {
                        printf("Write error\n");
                        break;
                    }
                    total_bytes+=n;
                    if(total_bytes >= fsize)
                    {
                        cout << "#";
                        cout.flush();
                        total_bytes-=fsize;
                    }
                }
                cout << "]" << endl;
                close(sfd);
                close(rfd);
                download = total_bytes = 0;
                cout << "Download " << fname << " complete!" << endl;
            }
            else if(strncmp(recvline, "filename ", 9) == 0)
            {
                char* tok = strtok(recvline+9, " \n\r");
                fname = string(tok);
                tok = strtok(NULL, " \n\r");
                fsize = atoi(tok)/10;
                rfd = open(fname.c_str(), O_RDWR | O_CREAT, 0644);
                if(rfd < 0)
                {
                    cout << "fail to open file " << fname << endl;
                    continue;
                }
                download = 1;
                strcpy(sendline, "OK\n");
                write(sockfd, sendline, 3);
            }
            else
                cout << string(recvline);
        }

        if(FD_ISSET(fileno(fp), &rset))
        {
            if(fgets(sendline, MAXLINE, fp) == NULL)
                cerr << "reading erro occur" << endl;

            if(strcmp(sendline, "/exit\n") == 0)
            {
                stdineof = 1;
                shutdown(sockfd, SHUT_WR);
                FD_CLR(fileno(fp), &rset);
                continue;
            }
            else if(strncmp(sendline, "/put ", 5) == 0)
            {
                strcpy(recvline, sendline+5);
                char* tok = strtok(recvline, " \n\r");
                if(tok == NULL)
                    continue;
                sfd = open(tok, O_RDONLY);
                if(sfd < 0)
                {
                    cout << "fail to open file " << string(tok) << endl;
                    continue;
                }
                upload = 1;
                fname = string(tok);
                fsize = lseek(sfd, 0, SEEK_END)/10;
                lseek(sfd, 0, SEEK_SET);
            }
            else if(strncmp(sendline, "/sleep ", 7) == 0)
            {
                seconds = atoi(sendline+7);
                cout << "Client starts to sleep" << endl;
                for(int i = 1; i <= seconds; i++)
                {
                    cout << "sleep " << i << endl;
                    sleep(1);
                }
                cout << "Client wakes up" << endl;
                continue;
            }
            if (write(sockfd, sendline, strlen(sendline)) <= 0)
                cout << "send fail" << endl;
        }
    }
}
int translate_file(char* serv_port, char* addr)
{
    int                 sockfd;
    struct sockaddr_in  servaddr;
    struct hostent      *hints;

    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if(sockfd < 0)
        cerr << "ERROR opening socket" << endl;

    hints = gethostbyname(addr);
    if(hints == NULL)
    {
        cerr << "no such host" << endl;
        return -1;
    }
    bzero(&servaddr, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_port = htons((uint16_t )atoi(serv_port));
    bcopy(hints->h_addr, (char*)&servaddr.sin_addr.s_addr, (size_t)hints->h_length);

    if(connect(sockfd, (struct sockaddr *) &servaddr, sizeof(servaddr)) < 0) {
        perror("translate_file");
        return 0;
    }
    return sockfd;
}