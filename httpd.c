/* J. David's webserver */
/* This is a simple webserver.
 * Created November 1999 by J. David Blackstone.
 * CSE 4344 (Network concepts), Prof. Zeigler
 * University of Texas at Arlington
 */
 /* This program compiles for Sparc Solaris 2.6.
  * To compile for Linux:
  *  1) Comment out the #include <pthread.h> line.
  *  2) Comment out the line that defines the variable newthread.
  *  3) Comment out the two lines that run pthread_create().
  *  4) Uncomment the line that runs accept_request().
  *  5) Remove -lsocket from the Makefile.
  */
#include <stdio.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <ctype.h>
#include <strings.h>
#include <string.h>
#include <sys/stat.h>
#include <pthread.h>
#include <sys/wait.h>
#include <stdlib.h>
 
#define ISspace(x) isspace((int)(x))
 
#define SERVER_STRING "Server: jdbhttpd/0.1.0\r\n"
 
void* accept_request(void*);   //处理连接，子线程
void bad_request(int);      /*404错误*/
void cat(int, FILE*);       //处理文件，读取文件内容，发送到客户端
void cannot_execute(int);   /*500错误处理函数*/
void error_die(const char*);//错误处理函数处理
void execute_cgi(int, const char*, const char*, const char*);//cgi函数调用
int get_line(int, char*, int);//从缓冲区读取一行
void headers(int, const char*);//服务器成功响应，返回200
void not_found(int);        /*请求内容不存在404*/
void serve_file(int, const char*);//处理文件请求
int startup(u_short *);      //初始化服务器
void unimplemented(int);    //501仅仅实现了get或者post方法，其他方法错误处理
 
void* accept_request(void* pclient)
{
    char buf[1024];//缓冲区
    int numchars;
    char method[255];
    char url[255];
    char path[512];//路径
    size_t i, j;
    struct stat st;//文件状态信息
    int cgi = 0;   //标志是否调用CGI程序
    char* query_string = NULL;
 
    int client = *(int*)pclient;//建立连接的socket描述符
    
    numchars = get_line(client, buf, sizeof(buf));//获取一行HTTP请求报文
 
    i = 0; j = 0;
    //提取其中的方法post或get到method
    while (!ISspace(buf[j]) && (i < sizeof(method) - 1))//根据空格定位方法
    {
        method[i] = buf[j];
        i++; j++;
    }
    method[i] = '\0';
 
    //tinyhttpd只实现了get post 方法
    if (strcasecmp(method, "GET") && strcasecmp(method, "POST"))//如果不是get或者post方法就会打印出错误
    {
        unimplemented(client);
        return NULL;
    }
 
    //cgi为标志位，1表示开启CGI解析(POST方法)
    if (strcasecmp(method, "POST") == 0)
        cgi = 1;
 
    i = 0;
    //跳过method后面的空白字符
    while (ISspace(buf[j]) && (j < sizeof(buf)))
        j++;
    //获取url
    while (!ISspace(buf[j]) && (i < sizeof(url) - 1) && (j < sizeof(buf)))
    {
        url[i] = buf[j];
        i++; j++;
    }
    url[i] = '\0';
 
    //如果是get方法，url可能带？参数
    if (strcasecmp(method, "GET") == 0)
    {
        query_string = url;
        while ((*query_string != '?') && (*query_string != '\0'))
            query_string++;
        if (*query_string == '?')
        {
            //带参数需要执行cgi，解析参数
            cgi = 1;
            *query_string = '\0';
            query_string++;
        }
    }
    //以上 将起始行 解析完毕
 
 
    sprintf(path, "htdocs%s", url);
    //如果path是一个目录，默认设置首页为index.html 
    if (path[strlen(path) - 1] == '/')
        strcat(path, "index.html");
 
    //函数定义:    int stat(const char *file_name, struct stat *buf);
     //函数说明:    通过文件名filename获取文件信息，并保存在buf所指的结构体stat中
     //返回值:     执行成功则返回0，失败返回-1，错误代码存于errno（需要include <errno.h>）
    if (stat(path, &st) == -1) {
        //访问的网页不存在，则不断的读取剩余的请求头部信息，并丢弃错误404
        while ((numchars > 0) && strcmp("\n", buf))  /* read & discard headers */
            numchars = get_line(client, buf, sizeof(buf));
        not_found(client);
    }
    else
    {
        //访问你的网页存在则进行处理
        //S_IFDIR 判断是否为目录
        if ((st.st_mode & S_IFMT) == S_IFDIR)
            strcat(path, "/index.html");
 
        //S_IXUSR：文件所有者具有可执行权限，
        //S_IXGRP：用户组具有可执行权限
        if ((st.st_mode & S_IXUSR) ||
            (st.st_mode & S_IXGRP) ||
            (st.st_mode & S_IXOTH))
            cgi = 1;
 
        if (!cgi)
            //将静态文件返回
            serve_file(client, path);
        else
            execute_cgi(client, path, method, query_string);
    }
    //THHP协议是面向无连接的，所以要关闭
    close(client);
    return NULL;
}
 
void bad_request(int client)
{
    char buf[1024];
 
    /*回应客户端错误的 HTTP 请求 */
    sprintf(buf, "HTTP/1.0 400 BAD REQUEST\r\n");
    send(client, buf, sizeof(buf), 0);
    sprintf(buf, "Content-type: text/html\r\n");
    send(client, buf, sizeof(buf), 0);
    sprintf(buf, "\r\n");
    send(client, buf, sizeof(buf), 0);
    sprintf(buf, "<P>Your browser sent a bad request, ");
    send(client, buf, sizeof(buf), 0);
    sprintf(buf, "such as a POST without a Content-Length.\r\n");
    send(client, buf, sizeof(buf), 0);
}
 
void cat(int client, FILE* resource)
{
    char buf[1024];
 
    /*读取文件中的所有数据写到 socket */
    fgets(buf, sizeof(buf), resource);
    while (!feof(resource))
    {
        send(client, buf, strlen(buf), 0);
        fgets(buf, sizeof(buf), resource);
    }
}
 
void cannot_execute(int client)
{
    char buf[1024];
 
    /* 回应客户端 cgi 无法执行*/
    sprintf(buf, "HTTP/1.0 500 Internal Server Error\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "Content-type: text/html\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "<P>Error prohibited CGI execution.\r\n");
    send(client, buf, strlen(buf), 0);
}
 
void error_die(const char* sc)
{
    /*出错信息处理 */
    perror(sc);
    exit(1);
}
 
void execute_cgi(int client, const char* path, const char* method, const char* query_string)
{
    char buf[1024];
    int cgi_output[2];
    int cgi_input[2];
    pid_t pid;
    int status;
    int i;
    char c;
    int numchars = 1;
    int content_length = -1;
 
    buf[0] = 'A'; buf[1] = '\0';
    if (strcasecmp(method, "GET") == 0)
        /*把所有的 HTTP header 读取并丢弃*/
        while ((numchars > 0) && strcmp("\n", buf))  /* read & discard headers */
            numchars = get_line(client, buf, sizeof(buf));
    else    /* POST */
    {
        /* 对 POST 的 HTTP 请求中找出 content_length */
        numchars = get_line(client, buf, sizeof(buf));
        while ((numchars > 0) && strcmp("\n", buf))
        {
            /*利用 \0 进行分隔 */
            buf[15] = '\0';
            /* HTTP 请求的特点*/
            if (strcasecmp(buf, "Content-Length:") == 0)
                content_length = atoi(&(buf[16]));
            numchars = get_line(client, buf, sizeof(buf));
        }
        /*没有找到 content_length */
        if (content_length == -1) {
            /*错误请求*/
            bad_request(client);
            return;
        }
    }
 
    /* 正确，HTTP 状态码 200 */
    sprintf(buf, "HTTP/1.0 200 OK\r\n");
    send(client, buf, strlen(buf), 0);
 
    /* 建立管道，进程写管道*/
    if (pipe(cgi_output) < 0) {
        /*错误处理*/
        cannot_execute(client);
        return;
    }
    /*建立管道，进程读管道*/
    if (pipe(cgi_input) < 0) {
        /*错误处理*/
        cannot_execute(client);
        return;
    }
 
    if ((pid = fork()) < 0) {
        /*错误处理*/
        cannot_execute(client);
        return;
    }
    if (pid == 0)  /* child: CGI script */
    {
        char meth_env[255];
        char query_env[255];
        char length_env[255];
 
        /* 把 STDOUT 重定向到 cgi_output 的写入端 */
        dup2(cgi_output[1], 1);
        /* 把 STDIN 重定向到 cgi_input 的读取端 */
        dup2(cgi_input[0], 0);
        /* 关闭 cgi_input 的写入端 和 cgi_output 的读取端 */
        close(cgi_output[0]);
        close(cgi_input[1]);
        /*设置 request_method 的环境变量*/
        sprintf(meth_env, "REQUEST_METHOD=%s", method);
        putenv(meth_env);
        if (strcasecmp(method, "GET") == 0) {
            /*设置 query_string 的环境变量*/
            sprintf(query_env, "QUERY_STRING=%s", query_string);
            putenv(query_env);
        }
        else {   /* POST */
            /*设置 content_length 的环境变量*/
            sprintf(length_env, "CONTENT_LENGTH=%d", content_length);
            putenv(length_env);
        }
        /*用 execl 运行 cgi 程序*/
        execl(path, path, NULL);
        exit(0);
    }
    else {    /* parent */
     /* 关闭 cgi_input 的读取端 和 cgi_output 的写入端 */
        close(cgi_output[1]);
        close(cgi_input[0]);
        if (strcasecmp(method, "POST") == 0)
            /*接收 POST 过来的数据*/
            for (i = 0; i < content_length; i++) {
                recv(client, &c, 1, 0);
                /*把 POST 数据写入 cgi_input，现在重定向到 STDIN */
                write(cgi_input[1], &c, 1);
            }
        /*读取 cgi_output 的管道输出到客户端，该管道输入是 STDOUT */
        while (read(cgi_output[0], &c, 1) > 0)
            send(client, &c, 1, 0);
 
        /*关闭管道*/
        close(cgi_output[0]);
        close(cgi_input[1]);
        /*等待子进程*/
        waitpid(pid, &status, 0);
    }
}
 
int get_line(int sock, char* buf, int size)
{
    int i = 0;
    char c = '\0';
    int n;
 
    /*把终止条件统一为 \n 换行符，标准化 buf 数组*/
    while ((i < size - 1) && (c != '\n'))
    {
        /*一次仅接收一个字节*/
        n = recv(sock, &c, 1, 0);
        /* DEBUG printf("%02X\n", c); */
        if (n > 0)
        {
            /*收到 \r 则继续接收下个字节，因为换行符可能是 \r\n */
            if (c == '\r')
            {
                /*使用 MSG_PEEK 标志使下一次读取依然可以得到这次读取的内容，可认为接收窗口不滑动*/
                n = recv(sock, &c, 1, MSG_PEEK);
                /* DEBUG printf("%02X\n", c); */
                /*但如果是换行符则把它吸收掉*/
                if ((n > 0) && (c == '\n'))
                    recv(sock, &c, 1, 0);
                else
                    c = '\n';
            }
            /*存到缓冲区*/
            buf[i] = c;
            i++;
        }
        else
            c = '\n';
    }
    buf[i] = '\0';
 
    /*返回 buf 数组大小*/
    return(i);
}
 
/*成功200*/
void headers(int client, const char* filename)
{
    char buf[1024];
    (void)filename;  /* could use filename to determine file type */
 
    /*正常的 HTTP header */
    strcpy(buf, "HTTP/1.0 200 OK\r\n");
    send(client, buf, strlen(buf), 0);
    /*服务器信息*/
    strcpy(buf, SERVER_STRING);
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "Content-Type: text/html\r\n");
    send(client, buf, strlen(buf), 0);
    strcpy(buf, "\r\n");
    send(client, buf, strlen(buf), 0);
}
 
void not_found(int client)
{
    char buf[1024];
 
    /* 404 页面 */
    sprintf(buf, "HTTP/1.0 404 NOT FOUND\r\n");
    send(client, buf, strlen(buf), 0);
    /*服务器信息*/
    sprintf(buf, SERVER_STRING);
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "Content-Type: text/html\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "<HTML><TITLE>Not Found</TITLE>\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "<BODY><P>The server could not fulfill\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "your request because the resource specified\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "is unavailable or nonexistent.\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "</BODY></HTML>\r\n");
    send(client, buf, strlen(buf), 0);
}
 
void serve_file(int client, const char* filename)
{
    FILE* resource = NULL;
    int numchars = 1;
    char buf[1024];
 
    /*读取并丢弃 header */
    buf[0] = 'A'; buf[1] = '\0';
    while ((numchars > 0) && strcmp("\n", buf))  /* read & discard headers */
        numchars = get_line(client, buf, sizeof(buf));
 
    /*打开 sever 的文件*/
    resource = fopen(filename, "r");
    if (resource == NULL)
        not_found(client);
    else
    {
        /*写 HTTP header */
        headers(client, filename);
        /*复制文件*/
        cat(client, resource);
    }
    fclose(resource);
}
 
int startup(u_short* port)
{
    int httpd = 0;  //定义服务器socket描述符
    struct sockaddr_in name;//定义sockaddr_in型结构体用来绑定服务器端的IP地址和端口
 
    /*建立 服务器端 socket */
    httpd = socket(PF_INET, SOCK_STREAM, 0);//PF_INET 地址类型ipv4-- SOCK_STREAM是socket类型--0是自动选定协议类型 
    if (httpd == -1)
        error_die("socket");//错误判断
    memset(&name, 0, sizeof(name));
    name.sin_family = AF_INET;//地址类型ipv4
    name.sin_port = htons(*port);//端口转化为网络字节序
    name.sin_addr.s_addr = htonl(INADDR_ANY);//本机任意可用ip地址，把本机字节序转化为网络字节序
    if (bind(httpd, (struct sockaddr*)&name, sizeof(name)) < 0)//绑定地址
        error_die("bind");
    /*如果当前指定端口是 0，则动态随机分配一个端口*/
    if (*port == 0)  /* 如果端口号=0，则随机选取可用端口*/
    {
        socklen_t namelen = sizeof(name);
        if (getsockname(httpd, (struct sockaddr*)&name, &namelen) == -1)//
            error_die("getsockname");
        *port = ntohs(name.sin_port);//修改端口号，网络字节序转化成本地字节序
    }
    /*开始监听*/
    if (listen(httpd, 5) < 0)
        error_die("listen");
    /*返回 socket id */
    return(httpd);
} 
 
/*错误处理501函数，仅仅包含get或者post方法，其他方法错误处理*/
void unimplemented(int client)
{
    char buf[1024];
 
    /* HTTP method 不被支持*/
    sprintf(buf, "HTTP/1.0 501 Method Not Implemented\r\n");
    send(client, buf, strlen(buf), 0);
    /*服务器信息*/
    sprintf(buf, SERVER_STRING);
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "Content-Type: text/html\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "<HTML><HEAD><TITLE>Method Not Implemented\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "</TITLE></HEAD>\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "<BODY><P>HTTP request method not supported.\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "</BODY></HTML>\r\n");
    send(client, buf, strlen(buf), 0);
}
 
int main(void)
{
    int server_sock = -1;   //定义服务器socket描述符
    u_short port = 0;       //定义服务器监听端口
    int client_sock = -1;   //定义客户端socket描述符
    struct sockaddr_in client_name; //定义socketddr_in 型结构体
    socklen_t client_name_len = sizeof(client_name);//获取客户端地址长度
    pthread_t newthread;    //定义线程id
 
 
    /*在对应端口建立 httpd 服务*/
    server_sock = startup(&port);//初始化服务器
    printf("httpd running on port %d\n", port);//打印端口号
 
    /*循环创建链接和子线程*/
    while (1)
    {
        /*套接字收到客户端连接请求*/
        client_sock = accept(server_sock, (struct sockaddr*)&client_name, &client_name_len);//阻塞等待客户端建立链接
        if (client_sock == -1)
            error_die("accept");
        /*派生新线程用 accept_request 函数处理新请求*/
        /* 创建子线程处理链接 */
        if (pthread_create(&newthread, NULL, accept_request, (void*)&client_sock) != 0)
            perror("pthread_create");
    }
 
    close(server_sock);
 
    return(0);
}