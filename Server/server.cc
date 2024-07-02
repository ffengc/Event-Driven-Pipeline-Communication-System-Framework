
//=====================================================================================================================
// Name        : server.cc
// Author      : Fengcheng Yu
// Date        : 2024.7.1 - 2024.7.2
// Description : 本项目中server进程server.cc编写
//                  server.cc中需要提供的方法如下:
//                  void* worker(void* args);
//                        worker线程调用的方法(从管道中获取pc对象的epoll写过来的数据)
//                  void callback(connection* conn);
//                        主线程epoll服务EPOLLIN就绪后，进行的回调操作(在本操作结束之前，会开启epoll的EPOLLOUT事件允许写出)
//=====================================================================================================================

#include "../Utils/comm.hpp"
#include "../Utils/epoll_control.hpp"
using namespace yufc;
#define WORKER_NUMBER 3

std::string ReadFromIpc(int fd) {
    while (1) {
        // 3. 开始通信
        // 读文件
        char buffer[SIZE];
        // while (true)
        // {
        memset(buffer, '\0', sizeof(buffer)); // 先把读取的缓冲区设置为0
        ssize_t s = read(fd, buffer, sizeof(buffer) - 1);
        // 最好不要让缓冲区写满，因为没有\0
        if (s > 0) {
            return std::string(buffer);
        } else if (s == 0) {
            logMessage(NORMAL, "client quit, I quit too\n");
            return std::string("quit");
        }
    }
}

// 这里的worker负责在对应管道里面获取数据
void* worker(void* args) {
    __thread_data* td = (__thread_data*)args;
    poll_control* pc = (poll_control*)td->__args;
    int my_fd = pc->__worker_thread_name_fd_map[td->__name]; // 这是我的管道文件描述符
    std::random_device rd;
    std::mt19937 gen(rd());
    std::exponential_distribution<> dist(pc->__lambda);
    while (true) {
        double interval = dist(gen); // 生成符合负指数分布的随机数
        unsigned int sleepTime = static_cast<unsigned int>(std::floor(interval)); // 负指数
        sleep(sleepTime);
        // 去哪个fd里面找数据？
        std::string buffer_msg = ReadFromIpc(my_fd); // 这里有问题，不能直接打印，要解包
        std::vector<std::string> final_msg = extract_messages(buffer_msg); // 解包
        for (auto e : final_msg) {
            struct message msg;
            decode(e, msg);
            // 此时我们就获取到数据了，打印一下
            std::cout << td->__name << " client# " << "I am " << "[" << msg.mesg_number << "]" << msg.src_tid << " from client" << std::endl; // 打印这个消息
        }
    }
    return nullptr;
}


void callback(connection* conn) {
    // 这里要控制平均分配给三个worker，而不是只丢给一个
    auto connector_to_worker_fds = conn->__tsvr->__connector_to_worker_fds;
    assert(connector_to_worker_fds.size() == conn->__tsvr->__worker_thread_num);
    // 随机数生成器初始化
    std::random_device rd; // 随机数发生器
    std::mt19937 gen(rd()); // 以 rd() 为种子的 Mersenne Twister 生成器
    std::uniform_int_distribution<> distrib(0, connector_to_worker_fds.size() - 1); // 分布在有效索引范围内
    auto& q = conn->__tsvr->__local_cache; // 这里一定要引用
    std::cout << "queue size: " << q.size() << std::endl;
    std::string buffer;
    while (!q.empty()) {
        // 访问队列前端的元素
        std::string single_msg = q.front() + "\n\r\n";
        // 随机选取一个fd
        int index = distrib(gen); // 生成随机索引
        int selected_fd = connector_to_worker_fds[index]; // 根据索引提取元素
        // selected_fd 就是选取到的fd
        // selected_fd 的缓冲区在哪？
        connection* send_conn = conn->__tsvr->__connection_map[selected_fd];
        send_conn->__out_buffer += single_msg;
        q.pop();
    }
    // 循环允许三个去write
    for (auto e : connector_to_worker_fds) {
        auto cur_send_conn = conn->__tsvr->__connection_map[e];
        conn->__tsvr->enable_read_write(cur_send_conn, true, true); // 允许写!
    }
}

void Usage() {
    std::cerr << "Usage: " << std::endl
              << "\t./server lambda" << std::endl;
}

int main(int argc, char** argv) {
    if (argc != 2) {
        Usage();
        exit(1);
    }
    // 0. 提取命令行参数
    double lambda_arg = std::stod(argv[1]);
    // 1. 创建管道文件
    if (mkfifo(ipcPath.c_str(), MODE) < 0) {
        // 创建管道文件失败
        perror("mkfifo");
        exit(-1);
    }
    // 2. 正常的文件操作(只读的方式打开管道文件)
    int connector_to_connector_fd = open(ipcPath.c_str(), O_RDONLY | O_NONBLOCK); // 3
    assert(connector_to_connector_fd >= 0); // 文件打开失败
    // 3. 创建3个管道文件
    auto out = get_client_worker_fifo(WORKER_NUMBER, serverIpcRootPath);
    std::vector<int> connector_to_worker_fds = out.first;
    std::vector<int> worker_fds = out.second;
    // 3. 管道文件fd传给tc对象
    poll_control* pc = new poll_control(worker,
        callback, WORKER_NUMBER, worker_fds, connector_to_worker_fds, connector_to_connector_fd, lambda_arg, SERVER);
    pc->dispather();
    // 4. 关闭管道文件
    close(connector_to_connector_fd);
    for (auto e : worker_fds)
        close(e);
    for (auto e : connector_to_worker_fds)
        close(e);
    // 5. 删除管道文件
    delete_fifo(WORKER_NUMBER, serverIpcRootPath);
    unlink(ipcPath.c_str());
    return 0;
}