
//=====================================================================================================================
// Name        : client.cc
// Author      : Fengcheng Yu
// Date        : 2024.7.1 - 2024.7.2
// Description : 本项目中client进程client.cc编写
//                  client.cc中需要提供的方法如下:
//                  void* worker(void* args);
//                        worker线程调用的方法(生产数据放到管道中等待pc对象的epoll获取)
//                  void callback(connection* conn);
//                        主线程epoll服务EPOLLIN就绪后，进行的回调操作(在本操作结束之前，会开启epoll的EPOLLOUT事件允许写出)
//=====================================================================================================================

#include "../Utils/comm.hpp"
#include "../Utils/epoll_control.hpp"
#define WORKER_NUMBER 3
using namespace yufc;

void* worker(void* args) {
    __thread_data* td = (__thread_data*)args;
    poll_control* pc = (poll_control*)td->__args;
    // 在这里构造Task
    std::random_device rd;
    std::mt19937 gen(rd());
    std::exponential_distribution<> dist(pc->__lambda); // 这里用命令行传递过来的参数
    size_t mesg_number = 0;
    while (true) {
        mesg_number++;
        double interval = dist(gen); // 生成符合负指数分布的随机数
        unsigned int sleepTime = static_cast<unsigned int>(std::floor(interval)); // 负指数
        sleep(sleepTime);
        // 这里要生成一条数据
        struct message msg;
        msg.mesg_number = mesg_number;
        msg.src_tid = pthread_self();
        memset(msg.data, '0', sizeof(msg.data));
        // 现在数据已经生成好了，现在需要发给conn，通过管道的方式，那么这个管道的fd在哪？
        std::cout << "generate a mesg[" << mesg_number << "], src_tid: " << msg.src_tid << std::endl;
        int cur_fd = pc->__worker_thread_name_fd_map[td->__name]; // 所以只需要把信息放到cur_fd的管道里面就可以了
        // 在把消息放进去之前，先encode一下，协议定制！
        std::string encoded = encode(msg) + "\n\r\n"; // "\n\r\n" 就是防止粘包的标识
        // 写到管道中去
        write(cur_fd, encoded.c_str(), encoded.size());
        if (mesg_number >= MESG_NUMBER) {
            // 最多发MESG_NUMBER条消息
            pc->__worker_finish_count++; // 设置退出信号
            break;
        }
    }
    return nullptr;
}

void callback(connection* conn) {
    auto& q = conn->__tsvr->__local_cache;
    std::string buffer;
    while (!q.empty()) {
        // 访问队列前端的元素
        std::string single_msg = q.front();
        buffer += single_msg + "\n\r\n";
        q.pop();
    }
    // 此时buffer里就是要发送的数据了，发送的fd是哪个？conn->__tsvr->__connector_to_connector_fd
    auto send_conn = conn->__tsvr->__connection_map[conn->__tsvr->__connector_to_connector_fd];
    send_conn->__out_buffer += buffer;
    conn->__tsvr->enable_read_write(send_conn, true, true); // 允许写!
}

void Usage() {
    std::cerr << "Usage: " << std::endl
              << "\t./client lambda" << std::endl;
}

int main(int argc, char** argv) {
    if (argc != 2) {
        Usage();
        exit(1);
    }
    // 0. 提取命令行参数
    double lambda_arg = std::stod(argv[1]);
    // 1. 获取管道文件和对应的文件描述符，一共4个管道，7个fd
    // 1.1 c->c的fd
    int connector_to_connector_fd = open(ipcPath.c_str(), O_WRONLY | O_NONBLOCK); // 按照写的方式打开
    //      这个管道文件不需要client创建，client只需要创建3个管道，server需要创建4个管道
    assert(connector_to_connector_fd >= 0);
    // 1.2 c->w的fd
    auto out = get_client_worker_fifo(WORKER_NUMBER, clientIpcRootPath);
    std::vector<int> worker_fds = out.first;
    std::vector<int> connector_to_worker_fds = out.second;
    // 2. 构造并运行pc对象
    poll_control* pc = new poll_control(worker,
        callback, WORKER_NUMBER, worker_fds, connector_to_worker_fds, connector_to_connector_fd, lambda_arg, CLIENT);
    pc->dispather();
    // 3. 关闭管道文件
    close(connector_to_connector_fd);
    for (auto e : worker_fds)
        close(e);
    for (auto e : connector_to_worker_fds)
        close(e);
    // 4. 删掉三个管道文件
    delete_fifo(WORKER_NUMBER, clientIpcRootPath);
    return 0;
}