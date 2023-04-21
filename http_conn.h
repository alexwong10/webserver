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

static const int kReadBufferSize = 2048;
// why 1024?
static const int kWriteBufferSize = 1024;
static const int kFilenameLen = 200;


class HttpConn
{
public:
    static int epoll_fd_;
    static int client_count_;
    
    HttpConn() {}
    ~HttpConn() {}

    void Process();
    void Init(int sockfd, const sockaddr_in & addr);
    void CloseConn();
    bool Read();
    bool Write();

    void Init();
    HTTP_CODE ProcessRead();
    bool ProcessWrite(HTTP_CODE ret);
    HTTP_CODE ParseRequestLine(char *text);
    HTTP_CODE ParseHeader(char *text);
    HTTP_CODE ParseContent(char *text);
    HTTP_CODE DoRequest();
    void Unmap();

    LINE_STATUS ParseLine();
    char *GetLine() { return read_buf_ + read_index_; };

    // arguments ... what does it mean?
    bool AddResponse(const char *format, ...);
    bool AddContent(const char *content);
    bool AddContentType();
    bool AddStatusLine(int status, const char *title);
    void AddHeader(int content_length);
    bool AddContentLength(int content_length);
    bool AddKeepAlive();
    bool AddBlankLine();

private:
    int sockfd_;
    sockaddr_in address_;
    CHECK_STATE check_state_;

    char read_buf_[kReadBufferSize];

    int checked_index_;
    int start_line_;
    int read_index_;
    int content_length_;
    char real_file_[kFilenameLen];

    char * url_;
    char * version_;
    char * host_;
    METHOD method_;
    bool keep_alive_;

    char write_buf_[kWriteBufferSize]; // 写缓冲区
    int write_index_;                     // 写缓冲区中待发送的字节数
    char * file_address_;                // 客户请求的目标文件被mmap到内存中的起始位置
    struct stat file_stat_;             
    struct iovec iovecs_[2];                // 我们将采用writev来执行写操作，所以定义下面两个成员，其中m_iv_count表示被写内存块的数量。
    int iovec_count_;

    int bytes_to_send_;
    int bytes_sent_;
};

#endif // HTTPCONNECTION_H