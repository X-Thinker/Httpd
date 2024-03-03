#include "Httpd.h"

/*服务器实现 开始*/
Server::Server(int th_num, int wn_max):
th_pool(th_num, wn_max),server_port(0),server_socket(-1){}
Server::~Server(){}
void Server::start_up(int port)
{
    struct addrinfo hints, *listp, *p;
    int listenfd, optval = 1;

    //设置addrinfo参数
    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE | AI_ADDRCONFIG | AI_NUMERICSERV;

    //把int型的port转换为port_ch来作为getaddrinfo函数的参数
    char port_ch[6]={0};
    sprintf(port_ch, "%d", port);
    getaddrinfo(NULL, port_ch, &hints, &listp);

    //获得listp列表，存储了有可能能够使用的套接字地址
    for(p = listp; p; p = p->ai_next)
    {
        if((listenfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) < 0)
            continue; //不断尝试获取套接字
        //端口复用，在服务器终止，重启后立即开始接收请求
        setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, (const void*)&optval, sizeof(int));
        //绑定文件描述符和端口号、ip地址
        if(bind(listenfd, p->ai_addr, p->ai_addrlen) == 0) break;
        //失败，关闭描述符
        close(listenfd);
    }
    freeaddrinfo(listp);
    if(!p)
        error_exit("目前没有可用地址");
    if(listen(listenfd, 5) < 0)
    {
        close(listenfd);
        error_exit("开启侦听失败");
    }

    port = atoi(port_ch);
    server_port = port;
    server_socket = listenfd;
    //在终端显示服务器运行在port端口上
    std::cout << "httpd running on port " << port << std::endl;
    
    //客户套接字
    struct sockaddr_in client_name;
    socklen_t client_name_len = sizeof(client_name);
    int client_socket = -1;
    
    //服务器启动，进入循环，接收请求
    while(1)
    {
        client_socket = accept(server_socket, (struct sockaddr*)&client_name, &client_name_len);
        if(client_socket == -1)
            error_exit("接收请求失败");
        //将请求加入线程池工作队列，如果工作队列已满则将阻塞
        th_pool.work_insert(client_socket);
    }

    close(server_socket);
    return;
}
void Server::error_exit(std::string error_mes)
{
    perror(error_mes.c_str());
    exit(1);
}
/*服务器实现 结束*/

/*线程池实现 开始*/
Thread_Pool::Thread_Pool(int th_num, int wn_max)
{
    if(th_num > 16)
    {
        th_num = 4;
        std::cout << "线程数量超出上限16,自动设置线程数量为4" << std::endl;
    }
    if(wn_max > 64)
    {
        wn_max = 16;
        std::cout << "工作队列最大数量超出上限64,自动设置最大工作数量为16" << std::endl;
    }
    thread_num = th_num;
    worknum_max = wn_max;

    //初始化各个信号量
    sem_init(&mutex, 0 ,1);
    sem_init(&slots, 0 ,worknum_max);
    sem_init(&tasks, 0 ,0);

    //创建工作者线程
    for(int i = 1; i <= thread_num; i++)
    {
        std::shared_ptr<std::thread> new_thread(new std::thread(&Thread_Pool::thread_start, this));
        new_thread->detach();//分离线程
        pool.push_back(new_thread);
    }
}
Thread_Pool::~Thread_Pool(){}
void Thread_Pool::thread_start()
{
    while(1)
    {
        /*从工作队列中取出客户描述符并执行工作*/
        int connfd = work_remove();
        accept_request(connfd);
        close(connfd);
    }
}
void Thread_Pool::work_insert(int object)
{
    sem_wait(&slots);
    sem_wait(&mutex);
    worklist.push(object);
    sem_post(&mutex);
    sem_post(&tasks);
}
int Thread_Pool::work_remove()
{
    sem_wait(&tasks);
    sem_wait(&mutex);
    int tk = worklist.front();
    worklist.pop();
    sem_post(&mutex);
    sem_post(&slots);
    return tk;
}
void *Thread_Pool::accept_request(int client)
{
    std::string buf, path, method, url, query_string;
    int bytenum;
    struct stat st; 
    Method md = POST;
    bool server_type = false;//区分静态内容和动态内容，默认为静态

    bytenum = httpd_getline(client, buf, 1024);
    //输入流，用来读取方法，url
    std::stringstream cin_stream(buf);
    //读取方法和url，http版本不读取
    cin_stream >> method >> url;
    if(method != "GET" && method != "POST")
    {
        //httpd只定义了GET和POST方法
        re_message.respond(Respond_Message::Not_Implemented, client);
        return NULL;
    }
    //POST方法则为动态内容
    if(method == "POST")
        server_type = true;
    if(method == "GET")
    {
        md = GET;
        //如果GET方法带?参数则请求动态内容，设置环境变量
        std::size_t pos = url.find("?");
        if(pos != std::string::npos)
        {    
            query_string = std::string(url, pos + 1);
            url.erase(pos);
            server_type = true;
        }
    }

    path = "htdocs" + url;
    //如果path是一个目录，默认设置首页为index.html
    if(path.back() == '/')
        path += "index.html";

    //访问的网页不存在，读取所有请求头部信息，返回404
    if(stat(path.c_str(), &st) == -1)
    {
        while(bytenum > 0 && buf != "\n")
            bytenum = httpd_getline(client, buf, 1024);
        re_message.respond(Respond_Message::Not_Found, client);
    }
    else 
    {
        //访问文件为目录则转到默认首页
        if((st.st_mode & S_IFMT) == S_IFDIR)
            path += "/index.html";

        //所有者，用户组，其他人具有可执行权限
        if((st.st_mode & S_IXUSR) || 
            (st.st_mode & S_IXGRP) || 
            (st.st_mode & S_IXOTH))
            server_type = true;

        if(server_type)
            execute_cgi(client, path, md, query_string);
        else 
            serve_file(client, path);
    }
    
    return NULL;
}
void Thread_Pool::serve_file(int client, std::string &file)
{
    int bytenum = 1;
    std::string buf;
    //读取并丢弃报头
    while(bytenum > 0 && buf != "\n")
            bytenum = httpd_getline(client, buf, 1024);

    //以只读打开目标文件
    std::ifstream target_file(file);
    if(!target_file.is_open())
        re_message.respond(Respond_Message::Not_Found, client);
    else 
    {
        //打开成功，状态200
        re_message.respond(Respond_Message::OK, client);
        //buf读入目标文件的所有内容
        std::stringstream tmp;
        tmp << target_file.rdbuf();
        buf = tmp.str();
        //向客户端发送buf
        send(client, buf.c_str(), buf.length(), 0);
    }
    target_file.close();
}
void Thread_Pool::execute_cgi(int client, std::string &path, Method method, std::string &query_string)
{
    std::string buf;
    int cgi_output[2], cgi_input[2];
    pid_t pid;
    int status = 0, bytenum = 1, content_length = -1;

    //GET方法直接丢弃报头
    if(method = GET)
        while(bytenum > 0 && buf != "\n")
            bytenum = httpd_getline(client, buf, 1024);
    else
    {
        while(bytenum > 0 && buf != "\n")
        {
            bytenum = httpd_getline(client, buf, 1024);
            size_t pos = buf.find("Content-Length: ");
            if(pos == std::string::npos)
                continue;
            else 
                content_length = std::stoi(std::string(buf, pos + 16));
        }
        //没有找到 Content-Length
        if(content_length == -1)
        {
            re_message.respond(Respond_Message::Bad_Request, client);
            return;
        }
    }

    //正确,返回状态码 200
    buf = "HTTP/1.0 200 OK\r\n";
    send(client, buf.c_str(), buf.length(), 0);

    //建立进程写管道
    if(pipe(cgi_output) < 0)
    {
        re_message.respond(Respond_Message::Internal_Server_Error, client);
        return;
    }
    //建立进程读管道
    if(pipe(cgi_input) < 0)
    {
        re_message.respond(Respond_Message::Internal_Server_Error, client);
        return;
    }

    //子进程
    if((pid = fork()) < 0)
    {
        re_message.respond(Respond_Message::Internal_Server_Error, client);
        return;
    }

    if(pid == 0) //子进程工作
    {
        std::string meth_env, query_env, length_env;

        //把 STDOUT 重定向到 cgi_output 的写入端
        dup2(cgi_output[1], 1);
        //把 STDIN 重定向到 cgi_input 的读取端
        dup2(cgi_input[0], 0);
        //关闭 cgi_input 的写入端 和 cgi_output 的读取端
        close(cgi_input[1]);
        close(cgi_output[0]);
        //设置method环境变量
        std::string meth = (method == GET ? "GET" : "POST");
        meth_env = "REQUEST_METHOD=" + meth;
        putenv(meth_env.data());

        if(method = GET)
        {
            //设置 query string 环境变量
            query_env = "QUERY_STRING=" + query_string;
            putenv(query_env.data());
        }
        else
        {
            //设置 content length 环境
            length_env = "CONTENT_LENGTH=" + std::to_string(content_length);
            putenv(length_env.data());
        }
        //execl函数执行 cgi程序
        execl(path.c_str(), path.c_str(), NULL);
        exit(0);
    }
    else // 父进程工作
    {
        //关闭 cgi_input 的读取端 和 cgi_output 的写入端
        close(cgi_output[1]);
        close(cgi_input[0]);
        char ch = '\0';
        if(method == POST)
            for(int i = 0; i < content_length; i++)
            {
                recv(client, &ch, 1, 0);
                //把 POST 数据写入 cgi_input，已重定向到 STDIN
                write(cgi_input[1], &ch, 1);
            }
        //读取 cgi_output 的管道输出到客户端，已重定向到STDOUT
        while(read(cgi_output[0], &ch, 1) > 0)
            send(client, &ch, 1, 0);
        
        //关闭管道
        close(cgi_output[0]);
        close(cgi_input[1]);
        //等待子进程结束
        waitpid(pid, &status, 0);
    }
}
/*线程池实现 结束*/

/*报文响应实现 开始*/
void Respond_Message::respond(Status st, int client)
{
    switch (st)
    {
    case OK: status_ok_200(client); break;
    case Bad_Request: status_bad_request_400(client); break;
    case Not_Found: status_not_found_404(client); break;
    case Internal_Server_Error: status_internal_server_error_500(client); break;
    case Not_Implemented: status_not_implemented_501(client); break;
    default: break;
    }
}
void Respond_Message::status_ok_200(int client)
{
    std::string message;
    message += "HTTP/1.0 200 OK\r\n";
    message += httpdname;
    message += "Content-Type: text/html\r\n\r\n";
    send(client, message.c_str(), message.length(), 0);
}
void Respond_Message::status_bad_request_400(int client)
{
    std::string message;
    message += "HTTP/1.0 400 BAD REQUEST\r\n";
    message += "Content-type: text/html\r\n\r\n";
    message += "<P>Your browser sent a bad request, ";
    message += "such as a POST without a Content-Length.\r\n";
    send(client, message.c_str(), message.length(), 0);
}
void Respond_Message::status_not_found_404(int client)
{
    std::string message;
    message += "HTTP/1.0 404 NOT FOUND\r\n";
    message += httpdname;
    message += "Content-Type: text/html\r\n\r\n";
    message += "<HTML><TITLE>Not Found</TITLE>\r\n";
    message += "<BODY><P>The server could not fulfill\r\n";
    message += "your request because the resource specified\r\n";
    message += "is unavailable or nonexistent.\r\n";
    message += "</BODY></HTML>\r\n";
    send(client, message.c_str(), message.length(), 0);
}
void Respond_Message::status_internal_server_error_500(int client)
{
    std::string message;
    message += "HTTP/1.0 500 Internal Server Error\r\n";
    message += "Content-type: text/html\r\n\r\n";
    message += "<P>Error prohibited CGI execution.\r\n";
    send(client, message.c_str(), message.length(), 0);
}
void Respond_Message::status_not_implemented_501(int client)
{
    std::string message;
    message += "HTTP/1.0 501 Method Not Implemented\r\n";
    message += httpdname;
    message += "Content-Type: text/html\r\n\r\n";
    message += "<HTML><HEAD><TITLE>Method Not Implemented\r\n";
    message += "</TITLE></HEAD>\r\n";
    message += "<BODY><P>HTTP request method not supported.\r\n";
    message += "</BODY></HTML>\r\n";
    send(client, message.c_str(), message.length(), 0);
}
/*报文响应实现 结束*/

/*辅助函数实现 开始*/
int httpd_getline(int fd, std::string &buf, int size)
{
    //字节数记录本次读取的数量
    int bytecount = 0, res_recv = 0;
    char ch = '\0';
    buf.clear();
 
    /*把终止条件统一为 \n 换行符，标准化 buf 数组*/
    while ((bytecount < size - 1) && (ch != '\n'))
    {
        /*一次仅接收一个字节*/
        res_recv = recv(fd, &ch, 1, 0);
        if (res_recv > 0)
        {
            /*收到 \r 则继续接收下个字节，因为换行符可能是 \r\n */
            if (ch == '\r')
            {
                /*使用 MSG_PEEK 标志查看下一次接收的字节但不读取*/
                res_recv = recv(fd, &ch, 1, MSG_PEEK);
                /*但如果是换行符则接收*/
                if ((res_recv > 0) && (ch == '\n'))
                    recv(fd, &ch, 1, 0);
                else
                    ch = '\n';
            }
            buf.push_back(ch);    
            bytecount++;
        }
        else
            ch = '\n';
    }
    /*返回此次接收的字节数*/
    return bytecount;
}
/*辅助函数实现 结束*/
