# esp32-event
This is a libuv-like eventloop library that monitors sockets for I/O events,
implements timers and a message queue.
The API is designed to be API compatible with the core libuv API -
uv_loop_t, uv_timer_t, uv_poll_t, uv_async_t. Additionally, it
has a 'control channel' that can wake up the event loop and pass it a message.
uv_async_t is implemented on top of that mechanism, but it also allows marshalling
arbitrary function calls via a message queue. A C++11 wrapper is implemented on
top of that that makes things more convenient and also allows the caller thread
to wait for the eventloop thread to return a value, thus implementing synchronous
marshalling.
