# Framework reuse

- [简体中文](./reuse.md)
- [English](./reuse-en.md)

To reuse this framework, you need the contents in the `./Utils` and `./temp` directories.

Construct the `poll_control` object:

```cpp
poll_control* pc = new poll_control(worker,
callback, WORKER_NUMBER, worker_fds, connector_to_worker_fds, connector_to_connector_fd, lambda_arg, CLIENT);
pc->dispather();
```

The meaning of the parameters is as follows:
- `worker`: What the worker thread is going to do, the type is: `void* worker(void* args);` function pointer. After constructing the pc object, `WORKER_NUMBER` threads will be started to execute the content defined by the worker.
- `callback`: The callback to be executed by the multiplexing maintained in the pc object, type: `void callback(connection* conn)`
- `using callback_t = std::function<void(connection*)>;`
- `WORKER_NUMBER`: The number of worker threads, type is integer.
- `worker_fds`: The list of file descriptors for communicating with worker threads, the sequence size is the same as `WORKER_NUMBER`. Type: `std::vector<int>`.
- `connector_to_worker_fds`: The file descriptors for communication between epoll and worker in the pc object, which are also the file descriptors that epoll needs to care about, the sequence size is the same as `WORKER_NUMBER`. Type: `std::vector<int>`.
- `connector_to_connector_fd`: the external sock/fd that the epoll object cares about. For example, in this project, `connector_to_connector_fd` is the fd for pipe communication with another process, and its type is integer.
- `lambda_arg`: its type is double, and it is a parameter set specifically for this project. For details, see [readme](../README.md). If it is not needed, it can be deleted.
- `CLIENT`: a type specific to this project, which is an integer, indicating whether it is currently a client or a server. For details, see [readme](../README.md). If it is not needed, it can be deleted.

Note:
- The worker thread will be started after the constructor is called
- The multiplexing service needs to be manually called `dispather()` to forward before it can be started.