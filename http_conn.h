#ifndef HTTPCONNECTION_H
#define HTTPCONNECTION_H

#include <sys/epoll.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <stdarg.h>
#include <errno.h>
#include "locker.h"
#include <sys/uio.h>
#include <string.h>

enum METHOD
{
    GET = 0,
    POST,
    HEAD,
    PUT,
    DELETE,
    TRACE,
    OPTIONS,
    CONNECT
};

enum CHECK_STATE
{
    CHECK_STATE_REQUESTLINE = 0,
    CHECK_STATE_HEADER,
    CHECK_STATE_CONTENT
};

enum HTTP_CODE
{
    NO_REQUEST,
    GET_REQUEST,
    BAD_REQUEST,
    NO_RESOURCE,
    FORBIDDEN_REQUEST,
    FILE_REQUEST,
    INTERNAL_ERROR,
    CLOSED_CONNECTION
};

enum LINE_STATUS
{
    LINE_OK = 0,
    LINE_BAD,
    LINE_OPEN
};

class Http_conn
{
public:
    static int m_epollfd;
    static int m_client_count;
    static const int READ_BUFFER_SIZE = 2048;
    static const int WRITE_BUFFER_SIZE = 2048;


    Http_conn() {}
    ~Http_conn() {}

    void process();
    void init(int sockfd, const sockaddr_in & addr);
    void close_conn();
    bool read();
    bool write();

private:
    int m_sockfd;
    sockaddr_in m_address;
    CHECK_STATE m_check_state;

    char m_read_buf[READ_BUFFER_SIZE];

    int m_checked_index;
    int m_start_line;
    int m_read_index;

    char * m_url;
    char * m_version;
    char * m_host;
    METHOD m_method;
    bool m_linger;

    void init();
    HTTP_CODE process_read();
    HTTP_CODE parse_request_line(char * text);
    HTTP_CODE parse_header(char * text);
    HTTP_CODE parse_content(char * text);
    HTTP_CODE do_request();

    LINE_STATUS parse_line();
    char * get_line() {return m_read_buf + m_read_index;};

};

#endif