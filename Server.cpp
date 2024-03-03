#include "Httpd.h"

int main(int argc, char *argv[])
{
    int port = 8192, thread_num = 4, worklist_max = 16;
    if(argc >= 2)
    {
        port = atoi(argv[1]);
        if(argc == 3)
        {
            std::cout << "参数数量有误，请检查参数数量" << std::endl;
            exit(1);
        }
        if(argc == 4)
            thread_num = atoi(argv[2]), worklist_max = atoi(argv[3]);
    }
    //创建服务器类
    Server server(thread_num, worklist_max);
    //启动服务器
    server.start_up(port);
    return 0;
}