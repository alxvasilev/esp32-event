#ifndef ESP32_EVENT_LIB_H
#define ESP32_EVENT_LIB_H
#include "uv-errno.h"
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <stdint.h>
#include <lwip/sockets.h>
#ifdef __cplusplus
    #define UV_CAPI extern "C"
#else
    #define UV_CAPI
#endif

typedef int32_t uv_time_t;
typedef TaskHandle_t uv_thread_t;

struct uv_message;
typedef void(*uv_msg_handler)(uv_message*);
struct uv_message
{
    uv_msg_handler mCfunc;
    uv_message* mNext;
#ifdef __cplusplus
    uv_message(uv_msg_handler cfunc): mCfunc(cfunc) {}
#endif
};

struct uv_poll_t;
struct uv_timer_t;
struct uv_async_t;

struct uv_loop_t
{
    uv_poll_t* mPolls;
    uv_timer_t* mTimers;
    uv_time_t mNow;
    void* data;
    int mCtrlRecvFd;
    int mCtrlSendFd;
    struct sockaddr_in mCtrlAddr;
    uv_message* mMsgQueue;
    uv_message* mMsgQueueLast;
    uint8_t mMsgQueueLen;
    SemaphoreHandle_t mMsgQueueMutex;
    StaticSemaphore_t mMsgQueueMutexMem;
    uv_thread_t mTaskId;
    uint8_t mFlags;
    uint8_t mPollEventsUpdated; //need this to be settable atomically from another thread, so we can't implement it as a bit in mFlags
};

enum {
    UV_UNKNOWN_HANDLE = 0,
    UV_ASYNC,
    UV_IDLE,
    UV_POLL,
/*
    UV_STREAM,
    UV_TCP,
    UV_UDP,
*/
    UV_TIMER,
  UV_HANDLE_TYPE_MAX
};
typedef uint8_t uv_handle_type;

typedef enum {
    UV_RUN_FOREVER = -2,
    UV_RUN_TILL_NOACTIVE = -1,
    UV_RUN_DEFAULT = UV_RUN_TILL_NOACTIVE,
    UV_RUN_ONCE = 1,
    UV_RUN_NOWAIT = 0
} uv_run_mode;

#define _UV_HANDLE_MEMBERS \
    uv_loop_t* loop;       \
    void* data;            \
    uv_handle_type type;

struct uv_handle_t
{
    _UV_HANDLE_MEMBERS
};

enum {
    UV_READABLE = 1,
    UV_WRITABLE = 2,
    UV_DISCONNECT = 4,
    UV_PRIORITIZED = 8,
    UVX_EVENTS_UPDATED = 128
};
typedef void(*uv_close_cb)(uv_handle_t*);

typedef uint8_t uv_poll_event;
typedef void (*uv_poll_cb)(uv_poll_t* handle, int status, int events);
struct uv_poll_t
{
    _UV_HANDLE_MEMBERS
    int fd;
    uint8_t mEvents;
    uv_poll_cb mCallback;
    uv_poll_t* mNext;
};

typedef void (*uv_timer_cb)(uv_timer_t* handle);
struct uv_timer_t
{
    _UV_HANDLE_MEMBERS
    uv_time_t mFireTime;
    uv_time_t mPeriod;
    uv_timer_cb mCallback;
    uv_timer_t* mNext;
};

#define UV_MAXTIME (((uv_time_t)1 << (sizeof(uv_time_t)*8-1))-1)

typedef void (*uv_async_cb)(uv_async_t*);
struct uv_async_t
{
    _UV_HANDLE_MEMBERS
    uv_async_cb mCallback;
    uint8_t mActive;
    uv_async_t* mNext;
};

UV_CAPI int uv_loop_init(uv_loop_t* loop);
/* Expand this list if necessary. */
#define UV_ERRNO_MAP(XX)                                                      \
  XX(E2BIG, "argument list too long")                                         \
  XX(EACCES, "permission denied")                                             \
  XX(EADDRINUSE, "address already in use")                                    \
  XX(EADDRNOTAVAIL, "address not available")                                  \
  XX(EAFNOSUPPORT, "address family not supported")                            \
  XX(EAGAIN, "resource temporarily unavailable")                              \
  XX(EAI_ADDRFAMILY, "address family not supported")                          \
  XX(EAI_AGAIN, "temporary failure")                                          \
  XX(EAI_BADFLAGS, "bad ai_flags value")                                      \
  XX(EAI_BADHINTS, "invalid value for hints")                                 \
  XX(EAI_CANCELED, "request canceled")                                        \
  XX(EAI_FAIL, "permanent failure")                                           \
  XX(EAI_FAMILY, "ai_family not supported")                                   \
  XX(EAI_MEMORY, "out of memory")                                             \
  XX(EAI_NODATA, "no address")                                                \
  XX(EAI_NONAME, "unknown node or service")                                   \
  XX(EAI_OVERFLOW, "argument buffer overflow")                                \
  XX(EAI_PROTOCOL, "resolved protocol is unknown")                            \
  XX(EAI_SERVICE, "service not available for socket type")                    \
  XX(EAI_SOCKTYPE, "socket type not supported")                               \
  XX(EALREADY, "connection already in progress")                              \
  XX(EBADF, "bad file descriptor")                                            \
  XX(EBUSY, "resource busy or locked")                                        \
  XX(ECANCELED, "operation canceled")                                         \
  XX(ECHARSET, "invalid Unicode character")                                   \
  XX(ECONNABORTED, "software caused connection abort")                        \
  XX(ECONNREFUSED, "connection refused")                                      \
  XX(ECONNRESET, "connection reset by peer")                                  \
  XX(EDESTADDRREQ, "destination address required")                            \
  XX(EEXIST, "file already exists")                                           \
  XX(EFAULT, "bad address in system call argument")                           \
  XX(EFBIG, "file too large")                                                 \
  XX(EHOSTUNREACH, "host is unreachable")                                     \
  XX(EINTR, "interrupted system call")                                        \
  XX(EINVAL, "invalid argument")                                              \
  XX(EIO, "i/o error")                                                        \
  XX(EISCONN, "socket is already connected")                                  \
  XX(EISDIR, "illegal operation on a directory")                              \
  XX(ELOOP, "too many symbolic links encountered")                            \
  XX(EMFILE, "too many open files")                                           \
  XX(EMSGSIZE, "message too long")                                            \
  XX(ENAMETOOLONG, "name too long")                                           \
  XX(ENETDOWN, "network is down")                                             \
  XX(ENETUNREACH, "network is unreachable")                                   \
  XX(ENFILE, "file table overflow")                                           \
  XX(ENOBUFS, "no buffer space available")                                    \
  XX(ENODEV, "no such device")                                                \
  XX(ENOENT, "no such file or directory")                                     \
  XX(ENOMEM, "not enough memory")                                             \
  XX(ENONET, "machine is not on the network")                                 \
  XX(ENOPROTOOPT, "protocol not available")                                   \
  XX(ENOSPC, "no space left on device")                                       \
  XX(ENOSYS, "function not implemented")                                      \
  XX(ENOTCONN, "socket is not connected")                                     \
  XX(ENOTDIR, "not a directory")                                              \
  XX(ENOTEMPTY, "directory not empty")                                        \
  XX(ENOTSOCK, "socket operation on non-socket")                              \
  XX(ENOTSUP, "operation not supported on socket")                            \
  XX(EPERM, "operation not permitted")                                        \
  XX(EPIPE, "broken pipe")                                                    \
  XX(EPROTO, "protocol error")                                                \
  XX(EPROTONOSUPPORT, "protocol not supported")                               \
  XX(EPROTOTYPE, "protocol wrong type for socket")                            \
  XX(ERANGE, "result too large")                                              \
  XX(EROFS, "read-only file system")                                          \
  XX(ESHUTDOWN, "cannot send after transport endpoint shutdown")              \
  XX(ESPIPE, "invalid seek")                                                  \
  XX(ESRCH, "no such process")                                                \
  XX(ETIMEDOUT, "connection timed out")                                       \
  XX(ETXTBSY, "text file is busy")                                            \
  XX(EXDEV, "cross-device link not permitted")                                \
  XX(UNKNOWN, "unknown error")                                                \
  XX(EOF, "end of file")                                                      \
  XX(ENXIO, "no such device or address")                                      \
  XX(EMLINK, "too many links")                                                \
  XX(EHOSTDOWN, "host is down")                                               \
  XX(EREMOTEIO, "remote I/O error")                                           \
  XX(ENOTTY, "inappropriate ioctl for device")                                \

typedef enum {
#define XX(code, _) UV_ ## code = UV__ ## code,
  UV_ERRNO_MAP(XX)
#undef XX
  UV_ERRNO_MAX = UV__EOF - 1
} uv_errno_t;

static inline uv_time_t IRAM_ATTR uv_now(uv_loop_t* loop)
{
    //we don't cache the time
    return xTaskGetTickCount() * portTICK_PERIOD_MS;
}

static inline uv_thread_t uv_thread_self()
{
    return xTaskGetCurrentTaskHandle();
}

// Non-standard API
UV_CAPI int uv_loop_run_once(uv_loop_t* loop, uv_time_t timeout);
UV_CAPI int uvx_loop_post_message(uv_loop_t* loop, uv_message* msg);
//==
UV_CAPI int uv_run(uv_loop_t* loop, uv_run_mode mode);
UV_CAPI int uv_loop_alive(uv_loop_t* loop);
UV_CAPI int uv_loop_close(uv_loop_t* loop);

UV_CAPI int uv_timer_init(uv_loop_t* loop, uv_timer_t* handle);
UV_CAPI int uv_timer_start(uv_timer_t* timer, uv_timer_cb cb, uv_time_t timeout, uv_time_t repeat);
UV_CAPI int uv_timer_stop(uv_timer_t* timer);
UV_CAPI int uv_timer_again(uv_timer_t* timer);
UV_CAPI void uv_timer_set_repeat(uv_timer_t* timer, uv_time_t repeat);

static inline uint64_t uv_timer_get_repeat(const uv_timer_t* timer)
{
    return timer->mPeriod;
}
static inline int uv_poll_init(uv_loop_t* loop, uv_poll_t* handle, int fd)
{
    handle->loop = loop;
    handle->fd = fd;
    return 0;
}
UV_CAPI int uv_poll_start(uv_poll_t* handle, int events, uv_poll_cb cb);
UV_CAPI int uv_poll_stop(uv_poll_t* poll);
// Thread safe, can be called by any thread
UV_CAPI int uvx_poll_update_events(uv_poll_t* handle, int events);
UV_CAPI int uvx_poll_add_events(uv_poll_t* poll, int events);
UV_CAPI int uvx_poll_remove_events(uv_poll_t* poll, int events);


UV_CAPI int uv_async_init(uv_loop_t* loop, uv_async_t* async, uv_async_cb cb);
UV_CAPI int uv_async_send(uv_async_t* async);

struct uvx_dns_resolve4_t;
typedef int(*uvx_dns4_cb)(uvx_dns_resolve4_t*, int error);
struct uvx_dns_resolve4_t
{
    uv_loop_t* loop;
    const char* host;
    sockaddr_in addr;
    uvx_dns4_cb mUserCb;
    void* data;
    uint8_t error;
};
UV_CAPI int uvx_dns_resolve4(uv_loop_t* loop, const char* host, uvx_dns_resolve4_t* req, uvx_dns4_cb cb, void* userp);

struct uv_mutex_t
{
    SemaphoreHandle_t mHandle;
    StaticSemaphore_t mMemory;
};

static inline int uv_mutex_init(uv_mutex_t* mutex)
{
    mutex->mHandle = xSemaphoreCreateMutexStatic(&mutex->mMemory);
    return 0;
}
static inline int uv_mutex_init_recursive(uv_mutex_t* mutex)
{
    mutex->mHandle = xSemaphoreCreateRecursiveMutexStatic(&mutex->mMemory);
    return 0;
}
static inline void uv_mutex_lock(uv_mutex_t* mutex)
{
    while(xSemaphoreTake(mutex->mHandle, portMAX_DELAY) != pdPASS);
}
static inline int uv_mutex_trylock(uv_mutex_t* mutex)
{
    return (xSemaphoreTake(mutex->mHandle, 0) == pdPASS) ? 0 : UV_EBUSY;
}

static inline void uv_mutex_unlock(uv_mutex_t* mutex)
{
    xSemaphoreGive(mutex->mHandle);
}

static inline void uv_mutex_destroy(uv_mutex_t* handle)
{
    vSemaphoreDelete(handle->mHandle);
}
#endif
