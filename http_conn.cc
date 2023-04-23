#include "http_conn.h"

const char *kOk200Title = "OK";
const char *kError400Title = "Bad Request";
const char *kErro400Form = "Your request has bad syntax or is inherently impossible to satisfy.\n";
const char *kError403Title = "Forbidden";
const char *kError403Form = "You do not have permission to get file from this server.\n";
const char *kError404Title = "Not Found";
const char *kError404Form = "The requested file was not found on this server.\n";
const char *kError500Title = "Internal Error";
const char *kError500Form = "There was an unusual problem serving the requested file.\n";

// FIXME, set soft root directory
const char *kDocRoot = "/mnt/d/home/Graduate/Grade3/cpp/webserver/resources";

int setnonblocking(int fd)
{
    int old_flag = fcntl(fd, F_GETFL);
    int new_flag = old_flag | O_NONBLOCK;
    fcntl(fd, F_SETFL, new_flag);
    return old_flag;
}

void addfd(int epollfd, int fd, bool one_shot)
{
    epoll_event event;
    event.data.fd = fd;
    // event.events = EPOLLIN | EPOLLRDHUP;
    event.events = EPOLLIN | EPOLLRDHUP;

    // why EPOLLONESHOT
    if (one_shot)
    {
        event.events |= EPOLLONESHOT;
    }

    epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);
    setnonblocking(fd);
}

void removefd(int epollfd, int fd)
{
    epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, 0);
    close(fd);
}

// reset EPOLLONESHOT?
void modfd(int epollfd, int fd, int ev)
{
    epoll_event event;
    event.data.fd = fd;
    event.events = ev | EPOLLET | EPOLLONESHOT | EPOLLRDHUP;
    epoll_ctl(epollfd, EPOLL_CTL_MOD, fd, &event);
}

int HttpConn::epoll_fd_ = -1;
int HttpConn::client_count_ = 0;

void HttpConn::CloseConn()
{
    if (sockfd_ != -1)
    {
        removefd(epoll_fd_, sockfd_);
        sockfd_ = -1;
        client_count_--;
    }
}

void HttpConn::Init(int sockfd, const sockaddr_in &addr)
{
    sockfd_ = sockfd;
    address_ = addr;
    int reuse = 1;
    setsockopt(sockfd_, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    addfd(epoll_fd_, sockfd_, true);
    client_count_++;
    Init();
}


void HttpConn::Init()
{
    check_state_ = CHECK_STATE_REQUESTLINE;
    checked_index_ = 0;
    start_line_ = 0;
    read_index_ = 0;
    write_index_ = 0;

    host_ = 0;
    method_ = GET;
    url_ = 0;
    version_ = 0;
    keep_alive_ = false;

    content_length_ = 0;
    bzero(read_buf_, kReadBufferSize);
    bzero(write_buf_, kReadBufferSize);
    bzero(real_file_, kFilenameLen);

    bytes_to_send_ = 0;
    bytes_sent_ = 0;
}

// why one-time
bool HttpConn::Read()
{
    if (read_index_ >= kReadBufferSize)
    {
        return false;
    }
    
    int bytes_read = 0;
    while (true)
    {
        bytes_read = recv(sockfd_, read_buf_ + read_index_, kReadBufferSize - read_index_, 0);
        if (bytes_read == -1)
        {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
            {
                break;
            }
            return false;
        } else if (bytes_read == 0)
        {
            return false;
        }
        read_index_ += bytes_read;        
    }
    
    printf("read data: %s\n", read_buf_);
    return true;
}

LINE_STATUS HttpConn::ParseLine()
{
    char temp;
    for (; checked_index_ < read_index_; ++checked_index_)
    {
        temp = read_buf_[checked_index_];
        if (temp == '\r')
        {
            if ((checked_index_ + 1) == read_index_)
            {
                return LINE_OPEN;
            }
            else if (read_buf_[checked_index_ + 1] == '\n')
            {
                read_buf_[checked_index_++] = '\0';
                read_buf_[checked_index_++] = '\0';
                return LINE_OK;
            }
            return LINE_BAD;
        }
        else if (temp == '\n')
        {
            if ((checked_index_ > 1) && (read_buf_[checked_index_ - 1] == '\r'))
            {
                read_buf_[checked_index_ - 1] = '\0';
                read_buf_[checked_index_++] = '\0';
                return LINE_OK;
            }
            return LINE_BAD;
        }
    }
    return LINE_OPEN;
}

HTTP_CODE HttpConn::ParseRequestLine(char *text)
{
    url_ = strpbrk(text, " \t");
    if (!url_)
    {
        return BAD_REQUEST;
    }

    *url_++ = '\0';
    char *method = text;

    if (strcasecmp(method, "GET") == 0)
    {
        method_ = GET;
    }
    else
    {
        return BAD_REQUEST;
    }

    version_ = strpbrk(url_, " \t");
    if (!version_)
    {
        return BAD_REQUEST;
    }
    *version_++ = '\0';
    if (strcasecmp(version_, "HTTP/1.1") != 0)
    {
        return BAD_REQUEST;
    }

    if (strncasecmp(url_, "http://", 7) == 0)
    {
        url_ += 7;
        url_ = strchr(url_, '/');
    }

    if (!url_ || url_[0] != '/')
    {
        return BAD_REQUEST;
    }

    check_state_ = CHECK_STATE_HEADER;
    return NO_REQUEST;
}

HTTP_CODE HttpConn::ParseHeader(char *text)
{
    if (text[0] == '\0')
    {
        if (content_length_ != 0)
        {
            check_state_ = CHECK_STATE_CONTENT;
            return NO_REQUEST;
        }
        return GET_REQUEST;
    }
    else if (strncasecmp(text, "Content-Length:", 15) == 0)
    {
        text += 15;
        text += strspn(text, "\t");
        content_length_ = atol(text);
    }
    else if (strncasecmp(text, "Connection:", 11) == 0)
    {
        text += 11;
        text += strspn(text, "\t");
        if (strcasecmp(text, "keep-alive") == 0)
        {
            keep_alive_ = true;
        }
    }
    else if (strncasecmp(text, "Host:", 5) == 0)
    {
        text += 5;
        text += strspn(text, " \t");
        host_ = text;
    }
    else
    {
        printf("Unknown header %s\n", text);
    }
    return NO_REQUEST;
}

HTTP_CODE HttpConn::ParseContent(char *text)
{
    // FIXME, parse contents
    if (read_index_ >= (content_length_ + checked_index_))
    {
        text[content_length_] = '\0';
        return GET_REQUEST;
    }
    return NO_REQUEST;
}

HTTP_CODE HttpConn::ProcessRead()
{
    LINE_STATUS line_status = LINE_OK;
    HTTP_CODE ret = NO_REQUEST;

    char * text = 0;
    while (((check_state_ == CHECK_STATE_CONTENT) && (line_status == LINE_OK))
        || ((line_status = ParseLine()) == LINE_OK))
    {
        text = GetLine();
        start_line_ = checked_index_;
        printf("read one line: %s\n", text);
        switch (check_state_)
        {
        case CHECK_STATE_REQUESTLINE:
            ret = ParseRequestLine(text);
            if (ret == BAD_REQUEST)
            {
                return BAD_REQUEST;
            }
            
            break;
        case CHECK_STATE_HEADER:
            ret = ParseHeader(text);
            if (ret == BAD_REQUEST)
            {
                return BAD_REQUEST;
            } else if (ret == GET_REQUEST)
            {
                return DoRequest();
            }
            break;
        case CHECK_STATE_CONTENT:
            ret = ParseContent(text);
            if (ret == GET_REQUEST)
            {
                return DoRequest();
            }
            line_status = LINE_OPEN;
            break;
        default:
            return INTERNAL_ERROR;
        }
    }
    return NO_REQUEST;
}

HTTP_CODE HttpConn::DoRequest()
{
    strcpy(real_file_, kDocRoot);
    int len = strlen(kDocRoot);
    strncpy(real_file_ + len, url_, kFilenameLen - len - 1);

    if (stat(real_file_, &file_stat_) < 0)
    {
        return NO_RESOURCE;
    }

    if (!(file_stat_.st_mode & S_IROTH))
    {
        return FORBIDDEN_REQUEST;
    }
    
    if (S_ISDIR(file_stat_.st_mode))
    {
        return BAD_REQUEST;    
    }
    
    int fd = open(real_file_, O_RDONLY);
    // why MAP_PRIVATE?
    file_address_ = (char *) mmap(0, file_stat_.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    return FILE_REQUEST;
}

void HttpConn::Unmap()
{
    if (file_address_)
    {
        munmap(file_address_, file_stat_.st_size);
        file_address_ = 0;
    }
}

bool HttpConn::Write()
{
    int temp = 0;

    if (bytes_to_send_ == 0)
    {
        modfd(epoll_fd_, sockfd_, EPOLLIN);
        Init();
        return true;
    }

    while (1)
    {
        temp = writev(sockfd_, iovecs_, iovec_count_);
        if (temp <= -1)
        {
            if (errno == EAGAIN)
            {
                modfd(epoll_fd_, sockfd_, EPOLLOUT);
                return true;
            }
            Unmap();
            return false;
        }
        bytes_to_send_ -= temp;
        bytes_sent_ += temp;

        // why?
        if (bytes_sent_ >= iovecs_[0].iov_len)
        {
            iovecs_[0].iov_len = 0;
            iovecs_[1].iov_base = file_address_ + (bytes_sent_ - write_index_);
            iovecs_[1].iov_len = bytes_to_send_;
        }
        else
        {
            iovecs_[0].iov_base = write_buf_ + bytes_sent_;
            iovecs_[0].iov_len = iovecs_[0].iov_len - temp;
        }

        if (bytes_to_send_ <= 0)
        {
            Unmap();
            modfd(epoll_fd_, sockfd_, EPOLLIN);
            if (keep_alive_)
            {
                Init();
                return true;
            }
            else
            {
                return false;
            }
        }
    }
}

bool HttpConn::AddResponse(const char *format, ...)
{
    if (write_index_ >= kWriteBufferSize)
    {
        return false;
    }
    va_list arg_list;
    va_start(arg_list, format);
    // what vsnprintf
    int len = vsnprintf(write_buf_ + write_index_, kWriteBufferSize - 1 - write_index_, format, arg_list);
    if (len >= (kWriteBufferSize - 1 - write_index_))
    {
        return false;
    }
    write_index_ += len;
    va_end(arg_list);
    return true;
}

bool HttpConn::AddStatusLine(int status, const char *title)
{
    return AddResponse("%s %d %s\r\n", "HTTP/1.1", status, title);
}

void HttpConn::AddHeader(int content_len)
{
    AddContentLength(content_len);
    AddContentType();
    AddKeepAlive();
    AddBlankLine();
}

bool HttpConn::AddContentLength(int content_len)
{
    return AddResponse("Content-Length: %d\r\n", content_len);
}

bool HttpConn::AddKeepAlive()
{
    return AddResponse("Connection: %s\r\n", (keep_alive_ == true) ? "keep-alive" : "close");
}

bool HttpConn::AddBlankLine()
{
    return AddResponse("%s", "\r\n");
}

bool HttpConn::AddContent(const char *content)
{
    return AddResponse("%s", content);
}

bool HttpConn::AddContentType()
{
    return AddResponse("Content-Type:%s\r\n", "text/html");
}

bool HttpConn::ProcessWrite(HTTP_CODE ret)
{
    switch (ret)
    {
    case INTERNAL_ERROR:
        AddStatusLine(500, kError500Title);
        AddHeader(strlen(kError500Form));
        if (!AddContent(kError500Form))
        {
            return false;
        }
        break;
    case BAD_REQUEST:
        AddStatusLine(400, kError400Title);
        AddHeader(strlen(kErro400Form));
        if (!AddContent(kErro400Form))
        {
            return false;
        }
        break;
    case NO_RESOURCE:
        AddStatusLine(404, kError404Title);
        AddHeader(strlen(kError404Form));
        if (!AddContent(kError404Form))
        {
            return false;
        }
        break;
    case FORBIDDEN_REQUEST:
        AddStatusLine(403, kError403Title);
        AddHeader(strlen(kError403Form));
        if (!AddContent(kError403Form))
        {
            return false;
        }
    case FILE_REQUEST:
        AddStatusLine(200, kOk200Title);
        AddHeader(file_stat_.st_size);
        iovecs_[0].iov_base = write_buf_;
        iovecs_[0].iov_len = write_index_;
        iovecs_[1].iov_base = file_address_;
        iovecs_[1].iov_len = file_stat_.st_size;
        iovec_count_ = 2;
        bytes_to_send_ = write_index_ + file_stat_.st_size;
        return true;
    default:
        return false;
    }

    iovecs_[0].iov_base = write_buf_;
    iovecs_[0].iov_len = write_index_;
    iovec_count_ = 1;
    bytes_to_send_ = write_index_;
    return true;
}

void HttpConn::Process()
{
    HTTP_CODE read_ret = ProcessRead();
    if (read_ret == NO_REQUEST)
    {
        modfd(epoll_fd_, sockfd_, EPOLLIN);
        return;
    }

    // printf("parse http request\n");
    bool write_ret = ProcessWrite(read_ret);
    if (!write_ret)
    {
        CloseConn();
    }
    modfd(epoll_fd_, sockfd_, EPOLLOUT);
}