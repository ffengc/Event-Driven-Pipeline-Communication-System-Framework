//============================================================================
// Name        : comm.hpp
// Author      : Fengcheng Yu
// Date        : 2024.7.1
// Description : 配置文件头文件
//============================================================================

#ifndef _COMM_HPP_
#define _COMM_HPP_

#include "./log.hpp"
#include <assert.h>
#include <chrono>
#include <fcntl.h>
#include <iostream>
#include <random>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <random>

// ---------------------------------------- 核心管道文件相关配置 ----------------------------------------
#define MODE 0666 // 定义管道文件的权限
#define SIZE 128 // 管道设计的大小要足够大，避免在测试打满server缓冲区时崩溃
std::string ipcPath = "../Utils/fifo.ipc";

// ---------------------------------------- 3个通信管道文件相关配置 ----------------------------------------
std::pair<std::vector<int>, std::vector<int>> get_client_worker_fifo(int worker_number, std::string root_path) {
    std::vector<int> in_fd; // 写端的3个fd
    std::vector<int> out_fd; // 读端端3个fd
    for (int i = 0; i < worker_number; i++) {
        // "../Utils/client_fifo" + "{i+1}.fifo"
        // 构造真正确的管道文件地址
        std::string real_path = root_path + std::to_string(i + 1) + ".ipc";
        // 先创建这个管道文件
        if (mkfifo(real_path.c_str(), MODE) < 0) {
            // 创建管道文件失败
            perror("mkfifo");
            exit(-1);
        }
        // 获取两端的地址
        int fd2 = open(real_path.c_str(), O_RDONLY | O_NONBLOCK); // 这里要注意顺序
        int fd1 = open(real_path.c_str(), O_WRONLY | O_NONBLOCK);
        in_fd.push_back(fd1); // 按照写的方式打开
        out_fd.push_back(fd2); // 按照读的方式打开
    }
    return { in_fd, out_fd };
}
void delete_fifo(int worker_number, std::string root_path) {
    for (int i = 0; i < worker_number; i++) {
        // "../Utils/client_fifo" + "{i+1}.fifo"
        // 构造真正确的管道文件地址
        std::string real_path = root_path + std::to_string(i + 1) + ".ipc";
        unlink(real_path.c_str());
    }
}


// --------------------------------- Client或者Server里通信管道相关配置 ---------------------------------
std::string serverIpcRootPath = "../Utils/server_fifo"; // server_fifo1.ipc, server_fifo2.ipc, server_fifo3.ipc ... 归worker管
std::string clientIpcRootPath = "../Utils/client_fifo"; // client_fifo1.ipc, client_fifo2.ipc, client_fifo3.ipc ... 归worker管


// ----------------------------------- pc对象相关配置 -----------------------------------
#define THREAD_NUM_DEFAULT 3 // 默认三个线程
// #define CACHE_MAX_SIZE_DEFAULT 20 // 默认缓冲区最大为20
enum {
    CLIENT,
    SERVER
};
using PC_MODE = int;

// ----------------------------------- 通信相关配置 -----------------------------------
#define MESG_NUMBER 50 // 定义发n条消息之后，Client自动退出
#define WAIT_IO_DONE 2 // 最后client和server会打印程序运行时间，避免前面有io没有结束，导致打印时间的语句不实在最后一行，所以在打印最后的时间语句前统一睡眠，等待io结束


// 消息结构
// 按照要求设置消息
struct message {
    uint64_t src_tid;  // 8个字节
    char data[4096];   // 4096个字节
};
// 序列化
std::string encode(const message& msg) {
    std::string result;
    result.resize(sizeof(msg.src_tid) + sizeof(msg.data)); // 确保有足够的空间来存储整个结构体
    // 复制src_tid到result
    memcpy(&result[0], &msg.src_tid, sizeof(msg.src_tid));
    // 复制data到result，紧接着src_tid后面
    memcpy(&result[sizeof(msg.src_tid)], msg.data, sizeof(msg.data));
    return result;
}
// 反序列化
bool decode(const std::string& encoded, message& msg) {
    if (encoded.size() != sizeof(msg.src_tid) + sizeof(msg.data))
        return false; // 如果encoded字符串大小不符合预期，则解码失败
    // 从字符串中复制src_tid到结构体
    memcpy(&msg.src_tid, &encoded[0], sizeof(msg.src_tid));
    // 从字符串中复制data到结构体，紧接着src_tid后面
    memcpy(msg.data, &encoded[sizeof(msg.src_tid)], sizeof(msg.data));
    return true;
}
// 切割报文
std::vector<std::string> extract_messages(std::string& buffer) {
    std::vector<std::string> messages;
    std::string delimiter = "\n\r\n";
    size_t pos = 0;
    std::string token;
    while ((pos = buffer.find(delimiter)) != std::string::npos) {
        token = buffer.substr(0, pos);
        messages.push_back(token);
        buffer.erase(0, pos + delimiter.length());
    }
    return messages;
}


#endif