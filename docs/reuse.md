# 框架复用

复用该框架，需要`./Utils`和`./temp`目录中的内容。

构造`poll_control`对象：

```cpp
poll_control* pc = new poll_control(worker,
    callback, WORKER_NUMBER, worker_fds, connector_to_worker_fds, connector_to_connector_fd, lambda_arg, CLIENT);
pc->dispather();
```

其中参数含义如下所示：
- `worker`: worker线程要做的事情，类型为: `void* worker(void* args);`的函数指针。构造pc对象后，会启动`WORKER_NUMBER`个线程执行worker所定义的内容。
- `callback`: pc对象中维护的多路转接所要执行的回调，类型为: `void callback(connection* conn)`
  - `using callback_t = std::function<void(connection*)>;`
- `WORKER_NUMBER`: worker线程数量，类型为整型。
- `worker_fds`: 与worker线程通信的文件描述符列表，序列大小和`WORKER_NUMBER`相同。类型为: `std::vector<int>`。
- `connector_to_worker_fds`: pc对象中epoll和worker通信的文件描述符，也是epoll需要关心的文件描述符，序列大小和`WORKER_NUMBER`相同。类型为: `std::vector<int>`。
- `connector_to_connector_fd`: epoll对象关心的外界sock/fd，比如在这个项目中，`connector_to_connector_fd`就是和另一个进程进行管道通信的fd，类型为整型。
- `lambda_arg`: 类型为double，是本项目特有设置的参数，具体可见[readme](../README.md)，如果不需要可以删除。
- `CLIENT`: 本项目特有的类型，为整型，表示当前是客户端还是服务端，具体可见[readme](../README.md)，如果不需要可以删除。

注意：
- worker线程会在构造函数调用之后启动
- 多路转接服务需要手动调用 `dispather()` 转发才能启动。