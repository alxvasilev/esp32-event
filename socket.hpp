#include "esp32-event.hpp"
#include <string.h>
#include <list>

class TcpSocket
{
public:
//  typedef void(*FreeFunc)(void*);
    struct Buffer
    {
        uint8_t* buf;
        size_t size;
        bool empty() const { return size < 1; }
        Buffer(): buf(nullptr), size(0) {};
        Buffer(const void* aBuf, size_t aSize)
        : buf(new uint8_t[aSize]), size(aSize)
        {
            memcpy(buf, aBuf, aSize);
        }
        Buffer(Buffer&& other)
        : buf(other.buf), size(other.size)
        {
            other.release();
        }
        ~Buffer() { if (buf) delete[] buf; }
        void moveFrom(Buffer& other)
        {
            free();
            buf = other.buf;
            size = other.size;
            other.release();
        }
        void free()
        {
            if (!buf)
                return;
            delete[] buf;
            size = 0;
        }
        void release()
        {
            buf = nullptr;
            size = 0;
        }
        void allocAndCopy(const void* aBuf, size_t aSize)
        {
            assert(empty());
            buf = new uint8_t[aSize];
            size = aSize;
            memcpy(buf, aBuf, aSize);
        }
    };
protected:
    uv_poll_t mPoll;
    Buffer mPartialSendBuf;
    std::list<Buffer> mSendBufs;
    void* mUserp;
    bool mRecvCalled = false;
    void onSocketWritable();
    static void pollEventCb(uv_poll_t* handle, int status, int events);
    void monitorWritable();
    void dontMonitorWritable();
    void monitorReadable();
    void dontMonitorReadable();
public:
    int fd() const { return mPoll.fd; }
    const uv_poll_t& pollDesc() const { return mPoll; }
    bool isWritable() const { return (mPartialSendBuf.empty()); }
    TcpSocket(uv_loop_t* loop);
    int send(const void* data, size_t len);
    int recv(void* buf, size_t bufsize);
    //====
    virtual void onReadable() {}
    virtual void onWritable() {}
    virtual void onError(int code) {}
    virtual void onDisconnect() {}
};

class TcpClientSocket: public TcpSocket
{
protected:
    static void connectCb(uv_poll_t* handle, int status, int events);
public:
    using TcpSocket::TcpSocket;
    int connectStrIp(const char* ip, uint16_t port);
    int connectHost(const char* host, uint16_t port);
    int connectIp4(sockaddr_in& addr);
//====
    virtual void onConnect() { printf("default onconnect()\n");}
};
