#ifndef ESP32_EVENT_LIB_HPP
#define ESP32_EVENT_LIB_HPP

#include "esp32-event.h"
#include <utility>

//A bit more efficient than std::unique_ptr - doesn't check for null in dtor
template <class M>
struct uvAutoDel
{
    M* mMsg;
    uvAutoDel(M* aMsg): mMsg(aMsg){}
    ~uvAutoDel() { delete mMsg; }
};

template <class F>
void uvLoopExecAsync(uv_loop_t* loop, F&& func)
{
    struct Message: public uv_message
    {
        F mFunc;
        Message(F&& func, uv_msg_handler cfunc)
        : uv_message(cfunc), mFunc(std::forward<F>(func)){}
    };
    Message* newmsg = new Message(std::forward<F>(func),
    [](uv_message* msg)
    {
        auto custMsg = static_cast<Message*>(msg);
        uvAutoDel<Message> autodel(custMsg);
        custMsg->mFunc();
    });
    uvx_loop_post_message(loop, newmsg);
}

template <class F>
auto uvLoopExecSync(uv_loop_t* loop, F&& func) -> decltype(func())
{
    class Message: public uv_message
    {
        F mFunc;
        decltype(func()) mRet;
        SemaphoreHandle_t mMutex;
        StaticSemaphore_t mMutexMem;
        Message(F&& func, uv_msg_handler cfunc)
        : uv_message(cfunc), mFunc(std::forward<F>(func)),
          mMutex(xSemaphoreCreateMutexStatic(&mMutexMem))
        {}
        ~Message() { vSemaphoreDelete(mMutex); }
    };
    Message* newmsg = new Message(std::forward<F>(func),
    [](uv_message* msg)
    {
        auto custMsg = static_cast<Message*>(msg);
        custMsg->mRet = custMsg->mFunc();
        xSemaphoreGive(custMsg->mMutex);
    });
    uv_loop_post_message(loop, newmsg);
    while(xSemaphoreTake(newmsg->mMutex, portMAX_DELAY) != pdPASS);
    uvAutoDel<Message> autodel(newmsg);
    return newmsg->mRet;
}
#endif
