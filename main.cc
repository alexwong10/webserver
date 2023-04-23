#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/epoll.h>
#include "locker.h"
#include "threadpool.h"
#include <signal.h>
#include "http_conn.h"

#define MAX_FD 65535
#define MAX_EVENT_NUMBER 10000


void addsig(int sig, void(handler)(int))
{
    struct sigaction sa;
    memset(&sa, '\0', sizeof(sa));
    sa.sa_handler = handler;
    sigfillset(&sa.sa_mask);
    sigaction(sig, &sa, NULL);
}


// why extern ?
extern void addfd(int epollfd, int fd, bool one_shot);
extern void removefd(int epollfd, int fd);
extern void modfd(int epollfd, int fd, int ev);


int main(int argc, char const *argv[])
{
    if(argc <= 1)
    {
        printf("usage: %s port_number\n", basename(argv[0]));
        exit(-1);
    }

    int port = atoi(argv[1]);

    // why?
    addsig(SIGPIPE, SIG_IGN);

    Threadpool<HttpConn> * pool = NULL;
    try
    {
        pool = new Threadpool<HttpConn>;
    }
    catch(const std::exception& e)
    {
        exit(-1);
    }
    

    HttpConn * clients = new HttpConn[MAX_FD];

    int listen_fd = socket(PF_INET, SOCK_STREAM, 0);
    int ret = 0;
    struct sockaddr_in address;
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(port);

    int reuse = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    ret = bind(listen_fd, (struct sockaddr*) & address, sizeof(address));
    ret = listen(listen_fd, 5);

    epoll_event events[MAX_EVENT_NUMBER];
    int epoll_fd = epoll_create(5);

    addfd(epoll_fd, listen_fd, false);
    HttpConn::epoll_fd_ = epoll_fd;

    while (true)
    {
        int num = epoll_wait(epoll_fd, events, MAX_EVENT_NUMBER, -1);
        if ((num < 0) && (errno != EINTR))
        {
            printf("epoll failure\n");
            break;
        }
        
        for (int i = 0; i < num; i++)
        {
            int sockfd = events[i].data.fd;
            if (sockfd == listen_fd)
            {
                struct sockaddr_in client_address;
                socklen_t client_addrlen = sizeof(client_address);
                int connfd = accept(listen_fd, (struct sockaddr*)&client_address, &client_addrlen);
                if (connfd < 0)
                {
                    printf("errno is: %d\n", errno);
                    continue;
                }
                if (HttpConn::client_count_ >= MAX_FD)
                {
                    close(connfd);
                }
                
                clients[connfd].Init(connfd, client_address);
            } else if (events[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR))
            {
                clients[sockfd].CloseConn();
            } else if (events[i].events & EPOLLIN)
            {
                if (clients[sockfd].Read())
                {
                    pool->Append(clients + sockfd);
                } else
                {
                    clients[sockfd].CloseConn();
                }
            } else if (events[i].events & EPOLLOUT)
            {
                if (!clients[sockfd].Write())
                {
                    clients[sockfd].CloseConn();
                }
            }   
        }
    }

    close(epoll_fd);
    close(listen_fd);
    delete [] clients;
    delete pool;

    return 0;
}
