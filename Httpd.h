#ifndef Httpd_h
#define Httpd_h

#include <iostream>
#include <sys/socket.h>
#include <netdb.h>
#include <sys/types.h>
#include <thread>
#include <mutex>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <memory>
#include <queue>
#include <vector>
#include <semaphore.h>

class Thread_Pool;
class Respond_Message;
const std::string httpdname = "Server: Think Tiny Httpd\r\n";

//服务器类
class Server
{
public:
    Server(int th_num = 4, int wn_max = 16);
    ~Server();
    void start_up(int port);//启动服务器，参数为服务器运行端口
    void error_exit(std::string error_mes);//异常返回

private:
    Thread_Pool th_pool;
    Respond_Message re_message;
    int server_port;
    int server_socket;
};

//线程池类
class Thread_Pool
{
public:
    Thread_Pool(int th_num = 4, int wn_max = 16);
    ~Thread_Pool();

    enum Method{GET,POST};//服务器所支持的方法
    void thread_start();//线程启动函数
    void accept_request(int client);//响应请求，执行工作
    void serve_file(int client, std::string file);//当请求为静态内容
    void execute_cgi(int client, std::string path, Method method, std::string query_string);//请求为动态内容，执行cgi

    void work_insert(int object);//将工作加入线程池工作队列
    int work_remove();//从工作队列中取出工作

private:
    /*工作队列*/
    std::queue<int> worklist;//工作队列
    int worknum_max;//最大任务数量
    sem_t mutex;//互斥锁提供对工作队列的互斥访问
    sem_t slots,tasks;//slots表示工作队列剩余容量，tasks表示工作队列任务数量

    /*工作者线程*/
    std::vector<std::shared_ptr<std::thread>> pool;
    int thread_num;
};

//报文响应类
class Respond_Message
{
public:
    enum Status{OK,Not_Found,Not_Implemented};
    void respond(Status st, int client);//根据状态码返回不同响应报文
    void status_ok_200(int client);//OK:200
    void status_not_found_404(int client);//Not Found:404
    void status_not_implemented_501(int client);//Not Implemented:501
};

#endif