#include <iostream>
#include <unistd.h>
#include <netinet/in.h>
#include <string.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <vector>
#include <map>
using namespace std;
#define MAXLINE 1024

int message(int, string &);

class tasktoDo
{
public:
    tasktoDo(string &name, string &i, int p)
    {
        toiptr = tooptr = to;
        fname = name;
        forDownload = true;
        lfd = cfd = -1;
        eof = phase = 0;
        id = i;
        pos = p;
        string tmp = id + "_" + fname;
        openFile(tmp);
    }

    tasktoDo(string &name, int fd, string &i, int p)
    {
        friptr = froptr = fr;
        fname = name;
        forDownload = false;
        lfd = cfd = -1;
        eof = phase = 0;
        ffd = fd;
        id = i;
        pos = p;
    }

    void openFile(string &name)
    {
        ffd = open(name.c_str(), O_RDONLY);
        if(ffd < 0)
            cerr << "open file err" << endl;
    }

    void sendName(int sockfd)
    {
        off_t fsize = lseek(ffd, 0, SEEK_END);
        lseek(ffd, 0, SEEK_SET);
        string msg = "filename " + fname + " " + to_string(fsize) + "\n";
        message(sockfd, msg);
    }

    int uploadFile()
    {
        if(eof == 0 && friptr < &fr[MAXLINE])
        {
            if((n = read(cfd, friptr, &fr[MAXLINE]-friptr)) < 0)
            {
                if(errno != EWOULDBLOCK)
                {
                    cerr << "read err from socket" << endl;
                    return -1;
                }
            }
            else if(n == 0)
            {
                eof = 1;
                if(froptr == friptr)
                {
                    close(ffd);
                    return 1;
                }
            }
            else
            {
                friptr += n;
            }
        }

        if((n = (ssize_t )(friptr - froptr)) > 0)
        {
            if((nwritten = write(ffd, froptr, (size_t )n)) < 0)
            {
                if(errno != EWOULDBLOCK)
                {
                    cerr << "write err to file" << endl;
                    return -1;
                }
            }
            else
            {
                froptr += nwritten;
                if(froptr == friptr)
                    froptr = friptr = fr;
                if(eof)
                {
                    close(ffd);
                    return 1;
                }
            }
        }
        return 0;
    }

    int downloadFile()
    {
        if(eof == 0 && toiptr < &to[MAXLINE])
        {
            if((n = read(ffd, toiptr, &to[MAXLINE]-toiptr)) < 0)
            {
                cerr << "read err from file" << endl;
                return -1;
            }
            else if(n == 0)
            {
                eof = 1;
                if(tooptr == toiptr)
                {
                    close(cfd);
                    return 1;
                }
            }
            else
            {
                toiptr += n;
            }
        }

        if((n = (ssize_t )(toiptr - tooptr)) > 0)
        {
            if((nwritten = write(cfd, tooptr, (size_t )n)) < 0)
            {
                if(errno != EWOULDBLOCK)
                {
                    cerr << "write err to socket" << endl;
                    return -1;
                }
            }
            else
            {
                tooptr += nwritten;
                if(tooptr == toiptr)
                    tooptr = toiptr = to;
                if(eof)
                {
                    close(cfd);
                    return 1;
                }
            }
        }
        return 0;
    }
    ~tasktoDo(){};
    bool    forDownload;
    string  fname, id;
    int     ffd, lfd, cfd, pos, phase;
private:
    char    to[MAXLINE], fr[MAXLINE];
    char    *toiptr, *tooptr, *friptr, *froptr;
    ssize_t n, nwritten;
    int     eof;
};

int message(int fd, string &mes)
{
    char tmp[MAXLINE];
    strcpy(tmp, mes.c_str());
    if(write(fd, tmp, strlen(tmp)) < 0) {
        cout << "fail to sent message" << endl;
        return 0;
    }
    return 1;
}

int main(int argc, char **argv) {
    int                             i, maxi, maxfd, listenfd, connfd, sockfd, ffd, flag, rand_port;
    int                             nready, client[FD_SETSIZE];
    ssize_t                         n;
    fd_set                          rset, allset;
    char                            buf[MAXLINE];
    socklen_t                       clilen;
    struct sockaddr_in              cliaddr, servaddr, fservaddr;
    string                          tmp, fname, id[FD_SETSIZE];
    map<string, vector<string>>     own_file;
    tasktoDo                        *readyTask[FD_SETSIZE];
    vector<tasktoDo*>               waitingTask;
    struct timeval                  timeout;

    if(argc != 2)
    {
        cout << "./server <port>" << endl;
        return 1;
    }

    listenfd = socket(AF_INET, SOCK_STREAM, 0);
    flag = fcntl(listenfd, F_GETFL, 0);
    fcntl(listenfd, F_SETFL, flag | O_NONBLOCK);

    bzero(&servaddr, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    servaddr.sin_port = htons((uint16_t)atoi(argv[1]));

    if (bind(listenfd, (struct sockaddr *) &servaddr, sizeof(servaddr)) < 0) {
        cout << "binding error" << endl;
        return 1;
    }
    listen(listenfd, 16);

    maxfd = listenfd;            /* initialize */
    maxi = -1;                    /* index into client[] array */
    for (i = 0; i < FD_SETSIZE; i++)
        client[i] = -1;            /* -1 indicates available entry */
    FD_ZERO(&allset);
    FD_SET(listenfd, &allset);
    srand((unsigned int)time(NULL));

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wmissing-noreturn"
    for (;;) {
        timeout.tv_sec = 0;
        timeout.tv_usec = 100000;
        rset = allset;        /* structure assignment */
        nready = select(maxfd + 1, &rset, NULL, NULL, &timeout);

        if (FD_ISSET(listenfd, &rset)) {    /* new client connection */
            clilen = sizeof(cliaddr);
            if ((connfd = accept(listenfd, (struct sockaddr *) &cliaddr, &clilen)) < 0) {
                if (errno != EAGAIN && errno != EWOULDBLOCK)
                    cerr << "accept err" << endl;
                continue;
            }

            for (i = 0; i < FD_SETSIZE; i++)
                if (client[i] < 0) {
                    client[i] = connfd;    /* save descriptor */
                    flag = fcntl(client[i], F_GETFL, 0);
                    fcntl(client[i], F_SETFL, flag | O_NONBLOCK);
                    break;
                }
            if (i == FD_SETSIZE)
                cout << "too many clients" << endl;

            FD_SET(connfd, &allset);    /* add new descriptor to set */
            if (connfd > maxfd)
                maxfd = connfd;            /* for select */
            if (i > maxi)
                maxi = i;                /* max index in client[] array */
            if (--nready <= 0)
                continue;                /* no more readable descriptors */
        }

        for (i = 0; i <= maxi; i++) {    /* check all clients for data */
            if ((sockfd = client[i]) < 0)
                continue;
            if (FD_ISSET(sockfd, &rset)) {
                n = read(sockfd, buf, MAXLINE);
                buf[n] = 0;
                char *tok = strtok(buf, " \n\r");
                if (n == 0) {
                    /*4connection closed by client */
                    close(sockfd);
                    FD_CLR(sockfd, &allset);
                    client[i] = -1;
                    id[i].clear();
                } else if (strcmp(tok, "CLIENTID:") == 0 && id[i].empty()) {
                    tok = strtok(NULL, " \n\r");
                    id[i] = string(tok);
                    string wellcome = "Welcome to the dropbox-like server! : " + id[i] + "\n";
                    message(sockfd, wellcome);

                    map<string, vector<string>>::iterator it = own_file.find(id[i]);
                    if (it != own_file.end()) {
                        //second connection for file transfer
                        for (vector<string>::iterator j = it->second.begin(); j != it->second.end(); j++) {
                            tasktoDo *ptr = new tasktoDo(*j, id[i], i);
                            waitingTask.push_back(ptr);
                        }
                    }
                } else if (strcmp(tok, "/put") == 0) {
                    tok = strtok(NULL, " \n\r");
                    string df(tok);
                    fname = id[i] + "_" + df;
                    ffd = open(fname.c_str(), O_RDWR | O_CREAT, 0644);
                    if (ffd < 0)
                        cerr << "file open err" << endl;
                    tasktoDo *ptr = new tasktoDo(df, ffd, id[i], i);
                    waitingTask.push_back(ptr);
                } else if (strncmp(tok, "OK", 2) == 0) {
                    if (readyTask[i] == NULL)
                        cerr << "not task but recv ok" << endl;
                    else if (readyTask[i]->phase != 1)
                        cerr << "phase not equal 1" << endl;
                    else
                        readyTask[i]->phase++;
                }
                if (--nready <= 0)
                    break;                /* no more readable descriptors */
            }
        }

        for (vector<tasktoDo *>::iterator it = waitingTask.begin(); it != waitingTask.end(); it++) {
            n = (*it)->pos;
            if (readyTask[n] == NULL) {
                readyTask[n] = *it;
                waitingTask.erase(it);
                if(it == waitingTask.end())
                    break;
            }
        }

        for (i = 0; i <= maxi; i++) {
            if (readyTask[i] != NULL) {
                if (FD_ISSET(readyTask[i]->lfd, &rset)) {
                    clilen = sizeof(cliaddr);
                    if ((connfd = accept(readyTask[i]->lfd, (struct sockaddr *) &cliaddr, &clilen)) < 0) {
                        if (errno != EAGAIN && errno != EWOULDBLOCK)
                            cerr << "accept err" << endl;
                        continue;
                    }
                    readyTask[i]->cfd = connfd;    /* save descriptor */
                    flag = fcntl(readyTask[i]->cfd, F_GETFL, 0);
                    fcntl(readyTask[i]->cfd, F_SETFL, flag | O_NONBLOCK);
                    FD_CLR(readyTask[i]->lfd, &allset);
                    if (!readyTask[i]->forDownload)
                    {
                        FD_SET(readyTask[i]->cfd, &allset);
                        if (readyTask[i]->cfd > maxfd)
                            maxfd = readyTask[i]->cfd;            /* for select */
                    }
                    else
                    {
                        FD_SET(readyTask[i]->ffd, &allset);
                        if(readyTask[i]->ffd > maxfd)
                            maxfd = readyTask[i]->ffd;
                    }
                    continue;
                }

                if (FD_ISSET(readyTask[i]->ffd, &rset)) {
                    if (readyTask[i]->downloadFile() == 1) {
                        close(readyTask[i]->ffd);
                        FD_CLR(readyTask[i]->ffd, &allset);
                        delete readyTask[i];
                        readyTask[i] = NULL;
                    }
                    continue;
                }

                if (FD_ISSET(readyTask[i]->cfd, &rset)) {
                    if (readyTask[i]->uploadFile() == 1) {
                        if (own_file.find(id[i]) != own_file.end())
                            own_file[id[i]].push_back(readyTask[i]->fname);
                        else {
                            vector<string> first_file;
                            first_file.push_back(readyTask[i]->fname);
                            own_file.insert(pair<string, vector<string>>(id[i], first_file));
                        }

                        for (int j = 0; j <= maxi; j++) {
                            if (client[j] > 0 && j != i && id[j] == id[i]) {
                                tasktoDo *ptr = new tasktoDo(readyTask[i]->fname, id[j], j);
                                waitingTask.push_back(ptr);
                            }
                        }
                        close(readyTask[i]->cfd);
                        FD_CLR(readyTask[i]->cfd, &allset);
                        delete readyTask[i];
                        readyTask[i] = NULL;
                    }
                    continue;
                }

                if (readyTask[i]->forDownload) {
                    switch (readyTask[i]->phase) {
                        case 0:
                            readyTask[i]->sendName(client[i]);
                            readyTask[i]->phase++;
                            break;
                        case 2:
                            readyTask[i]->lfd = socket(AF_INET, SOCK_STREAM, 0);
                            flag = fcntl(readyTask[i]->lfd, F_GETFL, 0);
                            fcntl(readyTask[i]->lfd, F_SETFL, flag | O_NONBLOCK);

                            bzero(&fservaddr, sizeof(fservaddr));
                            fservaddr.sin_family = AF_INET;
                            fservaddr.sin_addr.s_addr = htonl(INADDR_ANY);

                            rand_port = rand() % 45534 + 20001;
                            fservaddr.sin_port = htons((uint16_t) rand_port);

                            if (bind(readyTask[i]->lfd, (struct sockaddr *) &fservaddr, sizeof(fservaddr)) < 0) {
                                cout << "file transfer binding error" << endl;
                                continue;
                            }
                            listen(readyTask[i]->lfd, 1);

                            FD_SET(readyTask[i]->lfd, &allset);
                            if(readyTask[i]->lfd > maxfd)
                                maxfd = readyTask[i]->lfd;
                            write(client[i], to_string(rand_port).c_str(), to_string(rand_port).size());
                            readyTask[i]->phase++;
                            break;
                        default:
                            cout << "someone is sleeping" << endl;
                            break;
                    }
                } else {
                    switch (readyTask[i]->phase) {
                        case 0:
                            readyTask[i]->lfd = socket(AF_INET, SOCK_STREAM, 0);
                            flag = fcntl(readyTask[i]->lfd, F_GETFL, 0);
                            fcntl(readyTask[i]->lfd, F_SETFL, flag | O_NONBLOCK);

                            bzero(&fservaddr, sizeof(fservaddr));
                            fservaddr.sin_family = AF_INET;
                            fservaddr.sin_addr.s_addr = htonl(INADDR_ANY);

                            rand_port = rand() % 45534 + 20001;
                            fservaddr.sin_port = htons((uint16_t) rand_port);

                            if (bind(readyTask[i]->lfd, (struct sockaddr *) &fservaddr, sizeof(fservaddr)) < 0) {
                                cout << "file transfer binding error" << endl;
                                continue;
                            }
                            listen(readyTask[i]->lfd, 1);

                            FD_SET(readyTask[i]->lfd, &allset);
                            if(readyTask[i]->lfd > maxfd)
                                maxfd = readyTask[i]->lfd;
                            write(client[i], to_string(rand_port).c_str(), to_string(rand_port).size());
                            readyTask[i]->phase++;
                            break;
                        default:
                            cout << "someone is sleeping" << endl;
                            break;
                    }
                }
            }
        }
    }
#pragma clang diagnostic pop
}