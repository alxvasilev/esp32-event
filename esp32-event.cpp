#include "esp32-event.hpp"
#include <lwip/sockets.h>
#include <string.h>
#include <esp_log.h>

#ifndef UV_MSGQUEUE_MAX_LEN
    #define UV_MSGQUEUE_MAX_LEN 4
#endif
#define UV_LOG_DEBUG(fmtString,...) ESP_LOGD("UV", fmtString, ##__VA_ARGS__)
#define UV_LOG_ERROR(fmtString,...) ESP_LOGE("UV", fmtString, ##__VA_ARGS__)
#define UV_LOG_WARN(fmtString,...) ESP_LOGW("UV", fmtString, ##__VA_ARGS__)

#define loop_assert_thread(loop)                                   \
    assert(!loop->mTaskId || (uv_thread_self() == loop->mTaskId))
#define handle_assert_not_active(handle)  /*                       \
    assert(!(handle->type & UV_ACTIVE_BIT)); */

enum {
    kTerminate = 1,
    kPollsUpdated = 2
};

static inline void mutexLock(SemaphoreHandle_t mutex)
{
    while(xSemaphoreTake(mutex, portMAX_DELAY ) != pdPASS);
}
static inline void mutexUnlock(SemaphoreHandle_t mutex)
{
    xSemaphoreGive(mutex);
}

struct MutexLocker
{
    SemaphoreHandle_t mMutex;
    MutexLocker(SemaphoreHandle_t mutex): mMutex(mutex) { mutexLock(mutex); }
    ~MutexLocker()
    {
        mutexUnlock(mMutex);
    }
};
struct LoopTaskSetter
{
    uv_loop_t* mLoop;
    LoopTaskSetter(uv_loop_t* loop)
        :mLoop(loop) { loop->mTaskId = uv_thread_self(); }
    ~LoopTaskSetter() { mLoop->mTaskId = NULL; }
};

int udpsock_create_and_bind();

UV_CAPI int uv_loop_init(uv_loop_t* loop)
{
    memset(loop, 0, sizeof(uv_loop_s));
    loop->mMsgQueueMutex = xSemaphoreCreateMutexStatic(&loop->mMsgQueueMutexMem);
    int sock = udpsock_create_and_bind();
    if (sock < 0)
    {
        return UV_ENOMEM;
    }
    loop->mCtrlRecvFd = sock;
    size_t addrlen = sizeof(loop->mCtrlAddr);
    getsockname(sock, (struct sockaddr*)&loop->mCtrlAddr, &addrlen);
    UV_LOG_DEBUG("Control socket created and bound to port %u", ntohs(loop->mCtrlAddr.sin_port));
    sock = udpsock_create_and_bind();
    if (sock < 0)
    {
        close(loop->mCtrlRecvFd);
        loop->mCtrlRecvFd = -1;
        return UV_ENOMEM;
    }
    loop->mCtrlSendFd = sock;
    return 0;
}

int udpsock_create_and_bind()
{
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock == -1)
    {
        UV_LOG_ERROR("uv_loop_init: Error creating control socket");
        return UV_ENOMEM;
    }
    struct sockaddr_in addr;
    memset((char *) &addr, 0, sizeof(addr));

    addr.sin_family = AF_INET;
    addr.sin_port = htons(0);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if(bind(sock, (struct sockaddr*)&addr, sizeof(addr)) == -1)
    {
        close(sock);
        UV_LOG_ERROR("uv_loop_init: Error binding control socket to local interface: %s", strerror(errno));
        return -1;
    }
    return sock;
}

UV_CAPI int uv_loop_post_message(uv_loop_s* loop, uv_message* msg)
{
    while(loop->mMsgQueueLen > UV_MSGQUEUE_MAX_LEN);
    MutexLocker locker(loop->mMsgQueueMutex);
    msg->mNext = NULL;
    if (loop->mMsgQueueLast)
    {
        loop->mMsgQueueLast->mNext = msg;
    }
    else
    {
        assert(!loop->mMsgQueue);
        loop->mMsgQueue = msg;
    }
    loop->mMsgQueueLast = msg;
    loop->mMsgQueueLen++;
    int rc = sendto(loop->mCtrlSendFd, "\0", 1, MSG_DONTWAIT, (struct sockaddr*)&loop->mCtrlAddr, sizeof(struct sockaddr_in));
    if (rc < 0)
    {
        UV_LOG_ERROR("uv_message_post: sendto() returned error %s", strerror(errno));
        return UV_ENOMEM;
    }
    UV_LOG_DEBUG("Posted message %p to queue", msg);
    return 0;
}

void loopProcessMessages(uv_loop_s* loop)
{
    //Message handlers may queue new message
    while(loop->mMsgQueue)
    {
        uv_message* msg;
        //Detach the whole message queue
        mutexLock(loop->mMsgQueueMutex);
        //Queue is not locked between the check in while() and here,
        //but it's only us that can consume the queue, so msg can't be set
        //to null meanwhile
        msg = loop->mMsgQueue;
        assert(msg);
        loop->mMsgQueue = loop->mMsgQueueLast = NULL;
        loop->mMsgQueueLen = 0;
        mutexUnlock(loop->mMsgQueueMutex);

        // Empty the control socket packet queue
        uint8_t buf;
        socklen_t len = sizeof(sockaddr_in);
        while(recvfrom(loop->mCtrlRecvFd, &buf, 1, MSG_DONTWAIT, (sockaddr*)&loop->mCtrlAddr, &len) > 0);

        // Process messages. Message queue is not locked, so message handlers can post new messages
        while(msg)
        {
            auto next = msg->mNext;
            UV_LOG_DEBUG("Processing message %p", msg);
            msg->mCfunc(msg); //msg may be deleted at this point
            msg = next;
        }
    }
}

inline uv_time_t IRAM_ATTR now_ms()
{
    return xTaskGetTickCount() * portTICK_PERIOD_MS;
}

//insert in order of mFireTime
void timerAdd(uv_timer_s* timer)
{
    handle_assert_not_active(timer);
    auto loop = timer->loop;
    auto curr = loop->mTimers;
    if (!curr)
    {
        loop->mTimers = timer;
        timer->mNext = NULL;
    }
    else if (curr->mFireTime >= timer->mFireTime)
    {
        curr->mNext = loop->mTimers;
        loop->mTimers = curr;
    }
    else
    {
        for (;; curr=curr->mNext)
        {
            uv_timer_t* next = curr->mNext;
            if (!next)
            {
                curr->mNext = timer;
                timer->mNext = NULL;
                return;
            }
            if (next->mFireTime >= timer->mFireTime)
            {
                timer->mNext = next;
                curr->mNext = timer;
                return;
            }
        }
        assert(false);
    }
    UV_LOG_DEBUG("Timer %p scheduled to fire after %d ms", timer, timer->mFireTime-now_ms());
}

template <class T>
T* listGetPrevious(T* head, T* item)
{
    for (T* curr=head; curr; curr=curr->mNext)
    {
        if (curr->mNext == item)
            return curr;
    }
    return NULL;
}

UV_CAPI int uv_timer_init(uv_loop_t* loop, uv_timer_t* handle)
{
    handle->loop = loop;
    handle->type = UV_TIMER;
    handle->mPeriod = 0; //we need to know if a timer has never been started for uv_timer_again()
    return 0;
}

UV_CAPI int uv_timer_start(uv_timer_t* timer, uv_timer_cb cb, uv_time_t timeout, uv_time_t repeat)
{
    handle_assert_not_active(timer);
    timer->mPeriod = repeat;
    timer->mFireTime = now_ms()+timeout;
    timer->mCallback = cb;
    timerAdd(timer);
    return 0;
}

UV_CAPI int uv_timer_stop(uv_timer_t* timer)
{
    auto prev = listGetPrevious(timer->loop->mTimers, timer);
    if (prev)
    {
        prev->mNext = timer->mNext;
        return 0;
    }
    return UV_ENOENT;
}

UV_CAPI int uv_timer_again(uv_timer_t* timer)
{
    if (!timer->mPeriod)
    {
        return UV_EINVAL;
    }
    uv_timer_stop(timer);
    timer->mFireTime = now_ms() + timer->mPeriod;
    timerAdd(timer);
    return 0;
}

bool loopMaybeFireFirstTimer(uv_loop_s* loop, uv_time_t now)
{
    auto timer = loop->mTimers;
    if (!timer || (timer->mFireTime > now))
        return false;

    loop->mTimers = timer->mNext; //remove timer
    timer->type &= ~UV_ACTIVE_BIT;
    if (timer->mPeriod)
    {
        timer->mFireTime = now + timer->mPeriod;
    }
    else
    {
        timer->mFireTime = UV_MAXTIME;
    }
    timer->mCallback(timer);
    handle_assert_not_active(timer);

    timerAdd(timer);
    return true;
}

UV_CAPI void uv_timer_set_repeat(uv_timer_t* timer, uv_time_t repeat)
{
    timer->mPeriod = repeat;
    timer->mFireTime = now_ms()+repeat;
    timerAdd(timer);
}


enum { kRunError = -1, kRunTimeout = 0, kRunHadEvent = 1, kRunStopped = 2 };
UV_CAPI int uv_loop_run_once(uv_loop_t* loop, uv_time_t timeout)
{
    LoopTaskSetter lts(loop);
    fd_set rfds, wfds, efds;
    int highest; //prevent warning about uninitialzied variable
    loop->mFlags |= kPollsUpdated;
    uv_time_t now = uv_now(loop);
    uv_time_t until = now + timeout;
    auto timer = loop->mTimers;
    if (timer && timer->mFireTime < until)
    {
        until = timer->mFireTime;
    }
    uv_time_t timeToWait;
    while((timeToWait = until-now) >= 0)
    {
        if (loopMaybeFireFirstTimer(loop, now))
        {
            continue;
        }
        if (loop->mFlags & kPollsUpdated)
        {
            loop->mFlags &= ~kPollsUpdated;
            FD_ZERO(&rfds);
            FD_ZERO(&wfds);
            FD_ZERO(&efds);
            highest = loop->mCtrlRecvFd;
            for(uv_poll_s* pfd = loop->mPolls; pfd; pfd = pfd->mNext)
            {
                int fd = pfd->fd;
                uv_poll_event events = pfd->mEvents;
                if (events & UV_READABLE)
                {
                    FD_SET(fd, &rfds);
                }
                if (events * UV_WRITABLE)
                {
                    FD_SET(fd, &wfds);
                }
                FD_SET(fd, &efds);
                if (fd > highest)
                {
                    highest = fd;
                }
            }
            FD_SET(loop->mCtrlRecvFd, &rfds);
        }
        struct timeval tv = {
            .tv_sec = timeToWait / 1000,
            .tv_usec = (timeToWait % 1000)*1000
        };
        fd_set arfds = rfds;
        fd_set awfds = wfds;
        fd_set aefds = efds;

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
        // Prevent warning about highest not being initialized
        int rc = lwip_select(highest+1, &arfds, &awfds, &aefds, &tv);
#pragma GCC diagnostic pop

        if (rc > 0)
        {
            if (FD_ISSET(loop->mCtrlRecvFd, &rfds))
            {
                UV_LOG_DEBUG("Loop woken up by control socket");
                loopProcessMessages(loop);
                if (loop->mFlags & kTerminate)
                {
                    loop->mFlags &= ~kTerminate;
                    return kRunStopped;
                }
            }
            for(uv_poll_s* pfd = loop->mPolls; pfd; pfd = pfd->mNext)
            {
                int fd = pfd->fd;
                uv_poll_event subscrEvents = pfd->mEvents;
                uv_poll_event events = 0;
                if (FD_ISSET(fd, &arfds))
                {
                    events |= UV_READABLE;
                }
                if (FD_ISSET(fd, &awfds))
                {
                    events |= UV_WRITABLE;
                }
                if (FD_ISSET(fd, &aefds))
                {
                    pfd->mCallback(pfd, UV_EIO, events & subscrEvents);
                    return kRunHadEvent;
                }
                else //no error
                {
                    events &= subscrEvents;
                    if (events)
                    {
                        pfd->mCallback(pfd, 0, events);
                        return kRunHadEvent;
                    }
                }
            }
        }
        else if (rc < 0)
        {
            UV_LOG_ERROR("select() returned error %s(%d)", strerror(errno), errno);
            return kRunError;
        }
        now = now_ms();
        loopMaybeFireFirstTimer(loop, now);
    }
    return kRunTimeout;
}

int loopHasActive(uv_loop_s* loop)
{
    return loop->mPolls || loop->mTimers || loop->mMsgQueue;
}

UV_CAPI int uv_run(uv_loop_t* loop, uv_run_mode mode)
{
    switch (mode)
    {
    case UV_RUN_DEFAULT:
    {
        while(loopHasActive(loop))
        {
            int rc = uv_loop_run_once(loop, 3600000); //1 hour max wait
            if (rc == kRunStopped)
                return loopHasActive(loop);
        }
        return 0;
    }
    case UV_RUN_ONCE:
    {
        int hasActive;
        while((hasActive = loopHasActive(loop)) && (uv_loop_run_once(loop, 3600000) == kRunTimeout)); //1 hour max wait
        return hasActive;
    }
    case UV_RUN_NOWAIT:
    {
        uv_loop_run_once(loop, 0);
        return loopHasActive(loop);
    }
    }
    return -1;
}

UV_CAPI int uv_poll_start(uv_poll_t* handle, int events, uv_poll_cb cb)
{
    loop_assert_thread(handle->loop);
    int wasActive = handle->type & UV_ACTIVE_BIT;
    handle->mEvents = events;
    handle->mCallback = cb;
    if (!wasActive) //already active, update it
    {
        handle->type |= UV_ACTIVE_BIT;
        auto loop = handle->loop;
        handle->mNext = loop->mPolls;
        loop->mPolls = handle;
    }
    return 0;
}

UV_CAPI int uv_poll_stop(uv_poll_t* poll)
{
    loop_assert_thread(poll->loop);
    auto prev = listGetPrevious(poll->loop->mPolls, poll);
    if (prev)
    {
        prev->mNext = poll->mNext;
    }
    poll->type &= ~UV_ACTIVE_BIT;
    return 0;
}

UV_CAPI int uv_async_init(uv_loop_t* loop, uv_async_t* async, uv_async_cb cb)
{
    async->loop = loop;
    async->mCallback = cb;
    return 0;
}

struct AsyncExecMessage: public uv_message
{
    uv_async_t* mAsync;
    AsyncExecMessage(uv_async_t* async)
    : uv_message(handler), mAsync(async)
    {
        async->type |= UV_ACTIVE_BIT;
    }
    static void handler(uv_message* msg)
    {
        auto async = static_cast<AsyncExecMessage*>(msg)->mAsync;
        if (async) //if the async handle was closed, this will be set to NULL
        {
            async->type &= ~UV_ACTIVE_BIT; //first reset the but, so that the callback can add it again and not get EALREADY
            async->mCallback(async);
        }
        delete static_cast<AsyncExecMessage*>(msg);
    }
};

UV_CAPI int uv_async_send(uv_async_t* async)
{
    if (async->type & UV_ACTIVE_BIT)
        return UV_EALREADY;
    auto newmsg = new AsyncExecMessage(async);
    return uv_loop_post_message(async->loop, newmsg);
}

template <class T>
bool listRemoveItem(T& head, T item)
{
    if (item == head)
    {
        head = item->mNext;
        return true;
    }
    for (auto curr = head; curr; curr = curr->mNext)
    {
        if (item == curr->mNext)
        {
            curr->mNext = item->mNext;
            return true;
        }
    }
    return false;
}

void pollClose(uv_poll_t* poll)
{
    auto loop = poll->loop;
    listRemoveItem(loop->mPolls, poll);
    loop->mFlags |= kPollsUpdated;
}

void timerClose(uv_timer_t* timer)
{
    listRemoveItem(timer->loop->mTimers, timer);
}

void asyncClose(uv_async_t* async)
{
    auto loop = async->loop;
    MutexLocker locker(loop->mMsgQueueMutex);
    for (auto msg = loop->mMsgQueue; msg; msg = msg->mNext)
    {
        if (msg->mCfunc == AsyncExecMessage::handler) //it's a message that executes an async_t handle
        {
            auto am = static_cast<AsyncExecMessage*>(msg);
            if (am->mAsync == async)
            {
                am->mAsync->type &= ~UV_ACTIVE_BIT;
                am->mAsync = NULL;
                return;
            }
        }
    }
}

void doCloseHandle(uv_handle_t* handle)
{
    loop_assert_thread(handle->loop);
    switch(handle->type & ~UV_ACTIVE_BIT)
    {
    case UV_POLL:
        pollClose((uv_poll_t*)handle);
        return;
    case UV_TIMER:
        timerClose((uv_timer_t*)handle);
        return;
    case UV_ASYNC:
        asyncClose((uv_async_t*)handle);
        return;
    }
}

UV_CAPI void uv_close(uv_handle_t* handle, uv_close_cb cb)
{
    uvLoopExecAsync(handle->loop,
    [handle, cb]()
    {
        doCloseHandle(handle);
        cb(handle);
    });
}
UV_CAPI void uv_stop(uv_loop_t* loop)
{
    loop->mFlags |= kTerminate;
}

UV_CAPI int uv_loop_alive(uv_loop_t* loop)
{
    return loop->mPolls || loop->mTimers || loop->mMsgQueue;
}

UV_CAPI int uv_loop_close(uv_loop_t* loop)
{
    if (uv_loop_alive(loop))
        return UV_EBUSY;
    vSemaphoreDelete(loop->mMsgQueueMutex);
    return 0;
}
