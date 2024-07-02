//============================================================================
// Name        : epoll_control.hpp
// Author      : Fengcheng Yu
// Date        : 2024.7.1
//============================================================================

#ifndef __YUFC_POLL_CONTROL__
#define __YUFC_POLL_CONTROL__

#include "./comm.hpp"
#include "./log.hpp"
#include "./poll.hpp"
#include "./thread.hpp"
#include <assert.h>
#include <cerrno>
#include <functional>
#include <iostream>
#include <queue>
#include <string>
#include <unordered_map>

namespace yufc {
/**
 * 为了能够正常工作，常规的sock必须是要有自己独立的接收缓冲区和发送缓冲区！
 */

class poll_control;
class connection;
using func_t = std::function<void(connection*)>;
using callback_t = std::function<void(connection*)>; // 业务逻辑
/**
 * 对于client来说callback负责把cache的东西，放到发送的文件描述符中的out_buffer里去
 * 对于server来说callback就是把cache的东西，平均分配到3个worker线程对应的pipe_fd的out_buffer里去
 */
class connection {
public:
    connection(int fd = -1)
        : __fd(fd)
        , __tsvr(nullptr) { }
    ~connection() { }
    void set_callback(func_t recv_cb, func_t send_cb, func_t except_cb) {
        __recv_callback = recv_cb;
        __send_callback = send_cb;
        __except_callback = except_cb;
    }

public:
    int __fd; // io的文件描述符
    func_t __recv_callback;
    func_t __send_callback;
    func_t __except_callback;
    std::string __in_buffer; // 输入缓冲区（暂时没有处理二进制流）
    std::string __out_buffer; // 输出缓冲区
    poll_control* __tsvr; // 回指指针
};

class poll_control {
public:
    const static int gnum = 128; // 获取epoll继续事件的数量
    bool __quit_signal = false;

public:
    // conn的多路转接配置
    __epoll __poll; // 这里直接维护一个epoll
    std::unordered_map<int, connection*> __connection_map;
    struct epoll_event* __revs;
    int __revs_num;

public:
    // worker和connector的逻辑，connector一个，worker有n个
    std::vector<thread*> __worker_threads; // worker线程
    size_t __worker_thread_num; // worker线程数量
    double __lambda; // 负指数参数，消费频率/生产频率
    std::unordered_map<std::string, int> __worker_thread_name_fd_map;
    // 存储一下7个文件描述符
    std::vector<int> __worker_fds;
    std::vector<int> __connector_to_worker_fds;
    int __connector_to_connector_fd;
    std::queue<std::string> __local_cache; // 本地缓冲区
    callback_t __callback;
    PC_MODE __mode; // 当前是client端还是server端?
    size_t __worker_finish_count;

public:
    poll_control(void* (*worker)(void*) = nullptr, // worker 线程要做的事
        callback_t callback = nullptr, // 当前pc对象要做的事情
        int worker_number = THREAD_NUM_DEFAULT, // worker 线程个数
        std::vector<int> worker_fds = {}, // worker线程对应的通信管道的文件描述符
        std::vector<int> connector_to_worker_fds = {}, // conn和worker线程通信的fd
        int connector_to_connector_fd = 0, // conn和另一个conn通信的fd (conn管理的4个fd，都需要交给epoll来监管)
        double lambda = -1,
        PC_MODE mode = -1)
        : __worker_fds(worker_fds)
        , __connector_to_worker_fds(connector_to_worker_fds)
        , __connector_to_connector_fd(connector_to_connector_fd)
        , __poll(0) /* 这里给poll设置非阻塞 */
        , __revs_num(gnum)
        , __worker_thread_num(worker_number)
        , __lambda(lambda)
        , __mode(mode)
        , __callback(callback)
        , __worker_finish_count(0) {
        // 0. 检查合法输入参数合法性
        assert(worker != nullptr && callback != nullptr); // 检查回调非空
        assert(worker_number == worker_fds.size()); // 检查worker数量和管道fd数量是否相同
        assert(worker_number == connector_to_worker_fds.size() && worker_number == worker_fds.size());
        // 1. 创建worker线程
        for (int i = 1; i <= __worker_thread_num; i++) // 三个线程去进行worker任务
        {
            // 每个线程的fd
            int cur_fd = worker_fds[i - 1]; // 记得i-1
            __worker_threads.push_back(new thread(i, worker, this)); // 编号从1开始，0留给conn线程
            // worker只需要不断向cur_fd里面写东西就行了(client)
            // worker只需要不断向cur_fd里面拿东西就行了(server)
            // w线程里面如何找到对应的fd? 要通过ec对象来找，因此ec对象要维护一个map，w线程的名字->w线程应该操作的fd
            __worker_thread_name_fd_map[__worker_threads[__worker_threads.size() - 1]->name()] = cur_fd;
            // __worker_thread_name_fd_map[name] 就是这个worker应该操作的fd!
        }
        // 3. conn就是主线程，不是由
        // 3. 创建多路转接对象(conn才需要多路转接对象)
        __poll.create_poll();
        // 3. 添加conn_to_conn到epoll中，只需要处理发的逻辑(client)
        // 注意区分，如果是client端__connector_to_connector是写回调，否则是读
        if (__mode == CLIENT)
            __add_connection(connector_to_connector_fd, nullptr, std::bind(&poll_control::__sender, this, std::placeholders::_1), std::bind(&poll_control::__excepter, this, std::placeholders::_1));
        else if (__mode == SERVER) {
            __add_connection(connector_to_connector_fd, std::bind(&poll_control::__recver, this, std::placeholders::_1), nullptr, std::bind(&poll_control::__excepter, this, std::placeholders::_1));
        } else
            assert(false);
        // 4. 添加conn_to_worker到epoll中，只需要处理从3个管道拿数据的逻辑(client)
        for (size_t i = 0; i < __worker_thread_num; ++i) {
            if (__mode == CLIENT)
                __add_connection(connector_to_worker_fds[i], std::bind(&poll_control::__recver, this, std::placeholders::_1), nullptr, std::bind(&poll_control::__excepter, this, std::placeholders::_1));
            else if (__mode == SERVER)
                __add_connection(connector_to_worker_fds[i], nullptr, std::bind(&poll_control::__sender, this, std::placeholders::_1), std::bind(&poll_control::__excepter, this, std::placeholders::_1));
            else
                assert(false);
        }
        // 4. 构建一个获取就绪事件的缓冲区
        __revs = new struct epoll_event[__revs_num];
    }
    ~poll_control() {
        // join一下
        for (auto& iter : __worker_threads) {
            iter->join(); // 这个线程把自已join()一下
            delete iter;
        }
        for (auto& iter : __connection_map)
            delete iter.second;
        // close 所有的文件描述符，一共有7个
        for (auto& iter : __worker_fds)
            close(iter);
        for (auto& iter : __connector_to_worker_fds)
            close(iter);
        close(__connector_to_connector_fd);
        if (__revs)
            delete[] __revs;
    }

public:
    void dispather() {
        // 输入参数是上层的业务逻辑
        for (auto& iter : __worker_threads)
            iter->start();
        while (true && !__quit_signal && __worker_finish_count < __worker_thread_num)
            loop_once();
    }
    void loop_once() {
        // 捞取所有就绪事件到revs数组中
        int n = __poll.wait_poll(__revs, __revs_num);
        for (int i = 0; i < n; i++) {
            // 此时就可以去处理已经就绪事件了！
            int cur_fd = __revs[i].data.fd;
            uint32_t revents = __revs[i].events;
            // 将所有的异常，全部交给read和write来处理，所以异常直接打开in和out
            // read和write就会找except了！
            if (revents & EPOLLERR)
                revents |= (EPOLLIN | EPOLLOUT);
            if (revents & EPOLLHUP)
                revents |= (EPOLLIN | EPOLLOUT);
            // 如果in就绪了
            if (revents & EPOLLIN) {
                // 这个事件读就绪了 - 说明从worker的管道中看到了数据
                // 1. 先判断这个套接字是否在这个map中存在
                if (is_fd_in_map(cur_fd) && __connection_map[cur_fd]->__recv_callback != nullptr)
                    __connection_map[cur_fd]->__recv_callback(__connection_map[cur_fd]);
            }
            // 如果out就绪了 说明这个cur_fd是connector->connector的fd
            if (revents & EPOLLOUT) {
                if (is_fd_in_map(cur_fd) && __connection_map[cur_fd]->__send_callback != nullptr)
                    __connection_map[cur_fd]->__send_callback(__connection_map[cur_fd]);
            }
        }
    }

private:
    void __add_connection(int cur_fd, func_t recv_cb, func_t send_cb, func_t except_cb) {
        // 不同种类的套接字都可以调用这个方法
        // 0. ！先把sock弄成非阻塞！
        poll_control::set_non_block_fd(cur_fd);
        // 1. 构建conn对象，封装sock
        connection* conn = new connection(cur_fd);
        conn->set_callback(recv_cb, send_cb, except_cb);
        conn->__tsvr = this; // 让conn对象指向自己
        // 2. 添加cur_fd到poll中
        __poll.add_sock_to_poll(cur_fd, EPOLLIN | EPOLLET); // 默认开启读，但是不开写
        // 3. 把封装好的conn放到map里面去
        __connection_map.insert({ cur_fd, conn });
    }

private:
    void __recver(connection* conn) {
        // 非阻塞读取，所以要循环读取
        const int num = 102400;
        bool is_read_err = false;
        while (true) {
            char buffer[num];
            ssize_t n = read(conn->__fd, buffer, sizeof(buffer) - 1); // bug?
            if (n < 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) // 读取完毕了(正常的break)
                    break;
                else if (errno == EINTR)
                    continue;
                else {
                    logMessage(ERROR, "recv error, %d:%s", errno, strerror(errno));
                    conn->__except_callback(conn); // 异常了，调用异常回调
                    is_read_err = true;
                    break;
                }
            } else if (n == 0) {
                // logMessage(DEBUG, "client %d quit, server close %d", conn->__fd, conn->__fd);
                conn->__except_callback(conn);
                __quit_signal = true;
                is_read_err = true;
                break;
            }
            // 读取成功了
            buffer[n] = 0;
            conn->__in_buffer += buffer; // 放到缓冲区里面就行了
        } // end while
        // logMessage(DEBUG, "recv done, the inbuffer: %s", conn->__in_buffer.c_str());
        if (is_read_err == true)
            return;
        // 前面的读取没有出错
        // 这里就是上层的业务逻辑，如果对收到的报文做处理
        // 1. 切割报文，把单独的报文切出来
        // 2. 调用回调
        // __callback_func(conn, conn->__in_buffer);
        std::vector<std::string> outs = extract_messages(conn->__in_buffer);
        // outs是切割出来的报文，丢到缓冲区里去
        for (auto e : outs)
            conn->__tsvr->__local_cache.push(e);
        // 丢到缓冲区之后，还需要一个很重要的逻辑，就是要把东西放到输出out_fd(out_fd和conn->__fd不是同一个)
        conn->__tsvr->__callback(conn);
    }
    void __sender(connection* conn) {
        while (true) {
            ssize_t n = write(conn->__fd, conn->__out_buffer.c_str(), conn->__out_buffer.size());
            if (n > 0) {
                conn->__out_buffer.erase(0, n);
                if (conn->__out_buffer.empty())
                    break; // 发完了
            } else {
                if (errno == EAGAIN || errno == EWOULDBLOCK)
                    break;
                else if (errno == EINTR)
                    continue;
                else {
                    logMessage(ERROR, "send error, %d:%s", errno, strerror(errno));
                    conn->__except_callback(conn);
                    break;
                }
            }
        }
        // 走到这里，要么就是发完，要么就是发送条件不满足，下次发送
        if (conn->__out_buffer.empty())
            conn->__tsvr->enable_read_write(conn, true, false);
        else
            conn->__tsvr->enable_read_write(conn, true, true);
    }
    void __excepter(connection* conn) {
        // assert(false); // 不应该有这个问题，先assert一下做测试
        if (!is_fd_in_map(conn->__fd))
            return;
        // 1. 从epoll中移除
        if (!__poll.delete_from_epoll(conn->__fd))
            assert(false);
        // 2. 从map中移除
        __connection_map.erase(conn->__fd);
        // 3. close sock
        close(conn->__fd);
        // 4. delete conn
        delete conn;
        logMessage(DEBUG, "__excepter called\n");
    }

public:
    void enable_read_write(connection* conn, bool readable, bool writable) {
        uint32_t events = (readable ? EPOLLIN : 0) | (writable ? EPOLLOUT : 0);
        if (!__poll.control_poll(conn->__fd, events))
            logMessage(ERROR, "trigger write event fail");
    }
    bool is_fd_in_map(int sock) {
        auto iter = __connection_map.find(sock);
        if (iter == __connection_map.end())
            return false;
        return true;
    }
    static bool set_non_block_fd(int fd) { // 文件描述符设置为非阻塞的文件描述符
        int fl = fcntl(fd, F_GETFL);
        if (fl < 0)
            return false;
        fcntl(fd, F_SETFL, fl | O_NONBLOCK);
        return true;
    }
};
}

#endif