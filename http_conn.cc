#include "http_conn.h"

int Http_conn::m_epollfd = -1;
int Http_conn::m_client_count = 0;

void setnonblocking(int fd)
{
    int old_flag = fcntl(fd, F_GETFL);
    int new_flag = old_flag | O_NONBLOCK;
    fcntl(fd, F_SETFL, new_flag);
}

void addfd(int epollfd, int fd, bool one_shot)
{
    epoll_event event;
    event.data.fd = fd;
    // event.events = EPOLLIN | EPOLLRDHUP;
    event.events = EPOLLIN | EPOLLET | EPOLLRDHUP;

    // why EPOLLONESHOT
    if (one_shot)
    {
        event.events | EPOLLONESHOT;
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
    event.events = ev | EPOLLONESHOT | EPOLLRDHUP;
    epoll_ctl(epollfd, EPOLL_CTL_MOD, fd, &event);
}

void Http_conn::init(int sockfd, const sockaddr_in &addr)
{
    m_sockfd = sockfd;
    m_address = addr;
    int reuse = 1;
    setsockopt(m_sockfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    addfd(m_epollfd, m_sockfd, true);
    m_client_count++;
}


void Http_conn::init()
{
    m_check_state = CHECK_STATE_REQUESTLINE;
    m_checked_index = 0;
    m_start_line = 0;
    m_read_index = 0;
    m_method = GET;
    m_url = 0;
    m_version = 0;
    m_linger = 0;
    bzero(m_read_buf, READ_BUFFER_SIZE);
}


void Http_conn::close_conn()
{
    if (m_sockfd != -1)
    {
        removefd(m_epollfd, m_sockfd);
        m_sockfd = -1;
        m_client_count--;
    }   
}

// why one-time
bool Http_conn::read()
{
    if (m_read_index >= READ_BUFFER_SIZE)
    {
        return false;
    }
    
    int bytes_read = 0;
    while (true)
    {
        bytes_read = recv(m_sockfd, m_read_buf + m_read_index, READ_BUFFER_SIZE - m_read_index, 0);
        if (bytes_read == -1)
        {
            if (errno = EAGAIN || errno == EWOULDBLOCK)
            {
                break;
            }
            return false;
        } else if (bytes_read == 0)
        {
            return false;
        }
        m_read_index += bytes_read;        
    }
    
    printf("read data: %s\n", m_read_buf);
    return true;
}

bool Http_conn::write()
{
    printf("one-time write\n");
    return true;
}

void Http_conn::process()
{
    HTTP_CODE read_ret = process_read();
    if (read_ret == NO_REQUEST)
    {
        modfd(m_epollfd, m_sockfd, EPOLLIN);
        return;
    }
    

    printf("parse http request\n");
}

HTTP_CODE Http_conn::process_read()
{
    LINE_STATUS line_status = LINE_OK;
    HTTP_CODE ret = NO_REQUEST;

    char * text = 0;
    while (((line_status == CHECK_STATE_CONTENT) && (line_status == LINE_OK))
        || ((line_status = parse_line()) == LINE_OK))
    {
        text = get_line();
        m_start_line = m_checked_index;
        printf(" read one line\n");
        switch (m_check_state)
        {
        case CHECK_STATE_REQUESTLINE:
            ret = parse_request_line(text);
            if (ret == BAD_REQUEST)
            {
                return BAD_REQUEST;
            }
            
            break;
        case CHECK_STATE_HEADER:
            ret = parse_header(text);
            if (ret == BAD_REQUEST)
            {
                return BAD_REQUEST;
            } else if (ret == GET_REQUEST)
            {
                return do_request();
            }
            line_status = LINE_OPEN;
            break;
        case CHECK_STATE_CONTENT:
            ret = parse_content(text);
            if (ret == BAD_REQUEST)
            {
                return BAD_REQUEST;
            } else if (ret == GET_REQUEST)
            {
                return do_request();
            }
            line_status = LINE_OPEN;
            break;
        default:
            break;
        }
    }
    
}

HTTP_CODE Http_conn::parse_request_line(char *text)
{

}

HTTP_CODE Http_conn::parse_header(char *text)
{

}

HTTP_CODE Http_conn::parse_content(char *text)
{

}

LINE_STATUS Http_conn::parse_line()
{
    char temp;
    for (; m_checked_index < m_read_index; ++m_checked_index)
    {
        temp = m_read_buf[m_checked_index];
        if (temp == '\r')
        {
            if ((m_checked_index + 1) == m_read_index)
            {
                return LINE_OPEN;
            } else if (m_read_buf[m_checked_index + 1] == '\n')
            {
                m_read_buf[m_checked_index++] = '\0';
                m_read_buf[m_checked_index++] = '\0';
                return LINE_OK;
            }
            return LINE_BAD;
        } else if (temp == '\n')
        {
            if ((m_checked_index > 1) && (m_read_buf[m_checked_index-1] == '\r'))
            {
                m_read_buf[m_checked_index-1] = '\0';
                m_read_buf[m_checked_index++] = '\0';
                return LINE_OK;
            }
            return LINE_BAD;
        }
    }
}

HTTP_CODE Http_conn::do_request()
{

}