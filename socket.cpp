#include "esp32-event.hpp"
#include "socket.hpp"
#include <sys/ioctl.h>
#include <esp_log.h>

#define LOG_DEBUG(fmt,...) ESP_LOGD("ASOCK", fmt, ##__VA_ARGS__)

TcpSocket::TcpSocket(uv_loop_t* loop)
{
    int sockfd = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    unsigned long mode = 1;
    if (ioctlsocket(sockfd, FIONBIO, &mode) < 0)
    {
        close(sockfd);
        perror("ioctl(FIONBIO) failed");
        abort();
    }
    uv_poll_init(loop, &mPoll, sockfd);
    mPoll.data = this;
}

inline void TcpSocket::monitorWritable()
{
    printf("monitorWritable\n");
    uvx_poll_add_events(&mPoll, UV_WRITABLE);
}

inline void TcpSocket::dontMonitorWritable()
{
    printf("dontMonitorWritable\n");
    uvx_poll_remove_events(&mPoll, UV_WRITABLE);
}
inline void TcpSocket::monitorReadable()
{
    printf("monitorReadable\n");
    uvx_poll_add_events(&mPoll, UV_READABLE);
}

inline void TcpSocket::dontMonitorReadable()
{
    printf("dontMonitorReadable\n");
    uvx_poll_remove_events(&mPoll, UV_READABLE);
}

int TcpSocket::send(const void* data, size_t len)
{
    if (!mPartialSendBuf.empty())
    {
        mSendBufs.emplace_back(data, len);
        return 0;
    }
    auto rc = ::send(mPoll.fd, data, len, MSG_DONTWAIT);
    if (rc == len)
    {
        dontMonitorWritable();
        return 2;
    }
    else if (rc < 0)
    {
        if (errno == EWOULDBLOCK)
        {
            mPartialSendBuf.allocAndCopy(data, len);
            monitorWritable();
            return 0;
        }
        else
        {
            return -1;
        }
    }
    else if (rc == 0)
    {
        mPartialSendBuf.allocAndCopy(data, len);
        monitorWritable();
        return 0;
    }
    else // rc > 0, but rc < len
    {
        assert(rc < len);
        mPartialSendBuf.allocAndCopy(((char*)data)+rc, len-(size_t)rc);
        monitorWritable();
        return 1;
    }
}
int TcpSocket::recv(void* buf, size_t bufsize)
{
    int ret = ::recv(mPoll.fd, buf, bufsize, MSG_DONTWAIT);
    if (ret >= 0)
    {
        mRecvCalled = true;
    }
    return ret;
}

void TcpSocket::pollEventCb(uv_poll_t* handle, int status, int events)
{
    printf("socket I/O event\n");
#if ASYNC_SOCK_MARSHALL
    ::marshallCall([handle, events]()
    {
#endif
    auto self = static_cast<TcpSocket*>(handle->data);
    assert(self);
    if (status == UV_EIO)
    {
        LOG_DEBUG("socket %d error event", handle->fd);
        self->onError(ECONNRESET); //TODO: Maybe refine that
        return;
    }
    if (events & UV_WRITABLE)
    {
        LOG_DEBUG("socket %d writable", handle->fd);
        self->onSocketWritable();
    }
    if (events & UV_READABLE)
    {
        unsigned long avail;
#ifndef NDEBUG
        auto ret =
#endif
        ioctlsocket(self->mPoll.fd,FIONREAD, &avail);
        assert(ret == 0);
        if (avail)
        {
            LOG_DEBUG("socket %d readable", handle->fd);
            self->mRecvCalled = false;
            self->onReadable();
            if (!self->mRecvCalled)
            {
                self->dontMonitorReadable();
            }
        }
        else
        {
            LOG_DEBUG("socket %d closed", handle->fd);
            uv_poll_stop(&self->mPoll);
            self->onDisconnect();
        }
    }
#if ASYNC_SOCK_MARSHALL
    }
#endif
}

void TcpSocket::onSocketWritable()
{
    if (mPartialSendBuf.empty())
    {
        dontMonitorWritable();
        return;
    }
    Buffer buf(std::move(mPartialSendBuf));
    for(;;)
    {
        auto rc = ::send(mPoll.fd, buf.buf, buf.size, MSG_DONTWAIT);
        if (rc > 0)
        {
            if (rc == buf.size)
            {
                assert(rc == buf.size);
                if (!mSendBufs.empty())
                {
                    buf.moveFrom(mSendBufs.front());
                    mSendBufs.erase(mSendBufs.begin());
                    continue;
                }
                else
                {   //all sent
                    onWritable(); //may queue more data
                    if (mPartialSendBuf.empty())
                    { //nothing queued by onWritable()
                        dontMonitorWritable();
                    }
                }
            }
            else
            {
                assert(rc < buf.size);
                mPartialSendBuf.allocAndCopy(buf.buf+rc, buf.size-(size_t)rc);
            }
            return;
        }
        else if (rc < 0)
        {
            if (errno == EWOULDBLOCK)
            {
                mPartialSendBuf.moveFrom(buf);
            }
            else
            {
                dontMonitorWritable();
                onError(errno);
            }
        }
        else
        {
            assert(rc == 0);
            mPartialSendBuf.moveFrom(buf);
        }
        return;
    }
}

void TcpClientSocket::connectCb(uv_poll_t* poll, int status, int events)
{
    auto& self = *static_cast<TcpClientSocket*>(poll->data);
    uv_poll_stop(poll);
    if (status)
    {
        LOG_DEBUG("socket %p: Async error", &self);
        self.onError(ECONNREFUSED);
    }
    else
    {
        LOG_DEBUG("socket %p: Connected", &self);
        self.onConnect();
        uv_poll_start(poll, UV_READABLE, pollEventCb);
    }
}

int TcpClientSocket::connectStrIp(const char* ip, uint16_t port)
{
    assert(mPoll.fd);
    struct sockaddr_in addr;
    if (!inet_aton(ip, &addr.sin_addr))
    {
        return EINVAL;
    }
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    LOG_DEBUG("Connecting to %s:%hu...", ip, port);
    return connectIp4(addr);
}

int TcpClientSocket::connectIp4(sockaddr_in& addr)
{
    auto ret = ::connect(mPoll.fd, (const sockaddr*)&addr, sizeof(addr));
    if (ret < 0 && errno != EINPROGRESS)
    {
        LOG_DEBUG("Connect error: %s(%d)", strerror(errno), errno);
        return errno;
    }
    uv_poll_start(&mPoll, UV_WRITABLE, connectCb);
    return 0;
}

int TcpClientSocket::connectHost(const char* host, uint16_t port)
{
    auto dnsReq = new uvx_dns_resolve4_t;
    dnsReq->addr.sin_port = htons(port);
    auto ret = uvx_dns_resolve4(mPoll.loop, host, dnsReq,
    [](uvx_dns_resolve4_t* req) -> int
    {
        printf("user resolve cb\n");
        auto self = static_cast<TcpClientSocket*>(req->data);
        if (req->error)
        {
            self->onError(ENOENT);
        }
        else
        {
            self->connectIp4(req->addr);
        }
        delete req;
        return 1;
    }, this);
    return (ret == 0) ? 0 : -1;
}
