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
#include <cctype>
#include <chrono>
#include <fcntl.h>
#include <iomanip>
#include <iostream>
#include <random>
#include <signal.h>
#include <sstream>
#include <stdexcept>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

// ---------------------------------------- 核心管道文件相关配置 ----------------------------------------
#define MODE 0666 // 定义管道文件的权限
#define SIZE 102400 // 管道设计的大小要足够大，避免在测试打满server缓冲区时崩溃
std::string ipcPath = "../temp/fifo.ipc";
std::string serverIpcRootPath = "../temp/server_fifo"; // server_fifo1.ipc, server_fifo2.ipc, server_fifo3.ipc ...
std::string clientIpcRootPath = "../temp/client_fifo"; // client_fifo1.ipc, client_fifo2.ipc, client_fifo3.ipc ...

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

// ----------------------------------- pc对象相关配置 -----------------------------------
#define THREAD_NUM_DEFAULT 3 // 默认三个线程
enum {
    CLIENT,
    SERVER
};
using PC_MODE = int;

// ----------------------------------- 通信相关配置 -----------------------------------
#define MESG_NUMBER 10 // 定义发n条消息之后，Client自动退出
// 消息结构
// 按照要求设置消息
struct message {
    size_t mesg_number;
    uint64_t src_tid; // 8个字节
    char data[4096]; // 4096个字节
};
// 序列化
std::string encode(const message& msg) {
    std::ostringstream out;
    // 编码 mesg_number 和 src_tid 为十六进制字符串
    out << std::hex << msg.mesg_number << '|' << msg.src_tid << '|';
    // 编码 data，处理特殊字符
    for (int i = 0; i < 4096; i++) {
        if (std::isprint(msg.data[i]) && msg.data[i] != '%') {
            out << msg.data[i];
        } else {
            out << '%' << std::setw(2) << std::setfill('0') << std::hex << (unsigned int)(unsigned char)msg.data[i];
        }
    }
    return out.str();
}
// 反序列化
bool decode(const std::string& serialized, message& msg) {
    std::istringstream in(serialized);
    std::string mesg_number_hex, tid_hex;
    if (!std::getline(in, mesg_number_hex, '|') || !std::getline(in, tid_hex, '|'))
        return false;
    // 解析 mesg_number
    std::istringstream mesg_number_stream(mesg_number_hex);
    mesg_number_stream >> std::hex >> msg.mesg_number;
    // 解析 src_tid
    std::istringstream tid_stream(tid_hex);
    tid_stream >> std::hex >> msg.src_tid;
    // 解析 data
    std::string data;
    std::getline(in, data); // 读取剩余部分作为 data
    size_t i = 0, j = 0;
    while (i < data.size() && j < 4096) {
        if (data[i] == '%' && i + 2 < data.size()) {
            std::istringstream hex_char(data.substr(i + 1, 2));
            int value;
            hex_char >> std::hex >> value;
            msg.data[j++] = static_cast<char>(value);
            i += 3; // 跳过 "%XX"
        } else {
            msg.data[j++] = data[i++];
        }
    }
    return true;
}
// 切割报文 -- 处理粘包问题
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