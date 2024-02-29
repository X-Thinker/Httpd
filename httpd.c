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

void* accept_request(void*);   //�������ӣ����߳�
void bad_request(int);      /*404����*/
void cat(int, FILE*);       //�����ļ�����ȡ�ļ����ݣ����͵��ͻ���
void cannot_execute(int);   /*500����������*/
void error_die(const char*);//��������������
void execute_cgi(int, const char*, const char*, const char*);//cgi��������
int get_line(int, char*, int);//�ӻ�������ȡһ��
void headers(int, const char*);//�������ɹ���Ӧ������200
void not_found(int);        /*�������ݲ�����404*/
void serve_file(int, const char*);//�����ļ�����
int startup(u_short*);      //��ʼ��������
void unimplemented(int);    //501����ʵ����get����post��������������������

void* accept_request(void* pclient)
{
    char buf[1024];//������
    int numchars;
    char method[255];
    char url[255];
    char path[512];//·��
    size_t i, j;
    struct stat st;//�ļ�״̬��Ϣ
    int cgi = 0;   //��־�Ƿ����CGI����
    char* query_string = NULL;

    int client = *(int*)pclient;//�������ӵ�socket������

    numchars = get_line(client, buf, sizeof(buf));//��ȡһ��HTTP������

    i = 0; j = 0;
    //��ȡ���еķ���post��get��method
    while (!ISspace(buf[j]) && (i < sizeof(method) - 1))//���ݿո�λ����
    {
        method[i] = buf[j];
        i++; j++;
    }
    method[i] = '\0';

    //tinyhttpdֻʵ����get post ����
    if (strcasecmp(method, "GET") && strcasecmp(method, "POST"))//�������get����post�����ͻ��ӡ������
    {
        unimplemented(client);
        return NULL;
    }

    //cgiΪ��־λ��1��ʾ����CGI����(POST����)
    if (strcasecmp(method, "POST") == 0)
        cgi = 1;

    i = 0;
    //����method����Ŀհ��ַ�
    while (ISspace(buf[j]) && (j < sizeof(buf)))
        j++;
    //��ȡurl
    while (!ISspace(buf[j]) && (i < sizeof(url) - 1) && (j < sizeof(buf)))
    {
        url[i] = buf[j];
        i++; j++;
    }
    url[i] = '\0';

    //�����get������url���ܴ�������
    if (strcasecmp(method, "GET") == 0)
    {
        query_string = url;
        while ((*query_string != '?') && (*query_string != '\0'))
            query_string++;
        if (*query_string == '?')
        {
            //��������Ҫִ��cgi����������
            cgi = 1;
            *query_string = '\0';
            query_string++;
        }
    }
    //���� ����ʼ�� �������


    sprintf(path, "htdocs%s", url);
    //���path��һ��Ŀ¼��Ĭ��������ҳΪindex.html 
    if (path[strlen(path) - 1] == '/')
        strcat(path, "index.html");

    //��������:    int stat(const char *file_name, struct stat *buf);
     //����˵��:    ͨ���ļ���filename��ȡ�ļ���Ϣ����������buf��ָ�Ľṹ��stat��
     //����ֵ:     ִ�гɹ��򷵻�0��ʧ�ܷ���-1������������errno����Ҫinclude <errno.h>��
    if (stat(path, &st) == -1) {
        //���ʵ���ҳ�����ڣ��򲻶ϵĶ�ȡʣ�������ͷ����Ϣ������������404
        while ((numchars > 0) && strcmp("\n", buf))  /* read & discard headers */
            numchars = get_line(client, buf, sizeof(buf));
        not_found(client);
    }
    else
    {
        //���������ҳ��������д���
        //S_IFDIR �ж��Ƿ�ΪĿ¼
        if ((st.st_mode & S_IFMT) == S_IFDIR)
            strcat(path, "/index.html");

        //S_IXUSR���ļ������߾��п�ִ��Ȩ�ޣ�
        //S_IXGRP���û�����п�ִ��Ȩ��
        if ((st.st_mode & S_IXUSR) ||
            (st.st_mode & S_IXGRP) ||
            (st.st_mode & S_IXOTH))
            cgi = 1;

        if (!cgi)
            //����̬�ļ�����
            serve_file(client, path);
        else
            execute_cgi(client, path, method, query_string);
    }
    //THHPЭ�������������ӵģ�����Ҫ�ر�
    close(client);
    return NULL;
}

void bad_request(int client)
{
    char buf[1024];

    /*��Ӧ�ͻ��˴���� HTTP ���� */
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

    /*��ȡ�ļ��е���������д�� socket */
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

    /* ��Ӧ�ͻ��� cgi �޷�ִ��*/
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
    /*������Ϣ���� */
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
        /*�����е� HTTP header ��ȡ������*/
        while ((numchars > 0) && strcmp("\n", buf))  /* read & discard headers */
            numchars = get_line(client, buf, sizeof(buf));
    else    /* POST */
    {
        /* �� POST �� HTTP �������ҳ� content_length */
        numchars = get_line(client, buf, sizeof(buf));
        while ((numchars > 0) && strcmp("\n", buf))
        {
            /*���� \0 ���зָ� */
            buf[15] = '\0';
            /* HTTP ������ص�*/
            if (strcasecmp(buf, "Content-Length:") == 0)
                content_length = atoi(&(buf[16]));
            numchars = get_line(client, buf, sizeof(buf));
        }
        /*û���ҵ� content_length */
        if (content_length == -1) {
            /*��������*/
            bad_request(client);
            return;
        }
    }

    /* ��ȷ��HTTP ״̬�� 200 */
    sprintf(buf, "HTTP/1.0 200 OK\r\n");
    send(client, buf, strlen(buf), 0);

    /* �����ܵ�������д�ܵ�*/
    if (pipe(cgi_output) < 0) {
        /*������*/
        cannot_execute(client);
        return;
    }
    /*�����ܵ������̶��ܵ�*/
    if (pipe(cgi_input) < 0) {
        /*������*/
        cannot_execute(client);
        return;
    }

    if ((pid = fork()) < 0) {
        /*������*/
        cannot_execute(client);
        return;
    }
    if (pid == 0)  /* child: CGI script */
    {
        char meth_env[255];
        char query_env[255];
        char length_env[255];

        /* �� STDOUT �ض��� cgi_output ��д��� */
        dup2(cgi_output[1], 1);
        /* �� STDIN �ض��� cgi_input �Ķ�ȡ�� */
        dup2(cgi_input[0], 0);
        /* �ر� cgi_input ��д��� �� cgi_output �Ķ�ȡ�� */
        close(cgi_output[0]);
        close(cgi_input[1]);
        /*���� request_method �Ļ�������*/
        sprintf(meth_env, "REQUEST_METHOD=%s", method);
        putenv(meth_env);
        if (strcasecmp(method, "GET") == 0) {
            /*���� query_string �Ļ�������*/
            sprintf(query_env, "QUERY_STRING=%s", query_string);
            sprintf(query_env, "QUERY_STRING=%s", query_string);
            putenv(query_env);
        }
        else {   /* POST */
            /*���� content_length �Ļ�������*/
            sprintf(length_env, "CONTENT_LENGTH=%d", content_length);
            putenv(length_env);
        }
        /*�� execl ���� cgi ����*/
        execl(path, path, NULL);
        exit(0);
    }
    else {    /* parent */
        /* �ر� cgi_input �Ķ�ȡ�� �� cgi_output ��д��� */
        close(cgi_output[1]);
        close(cgi_input[0]);
        if (strcasecmp(method, "POST") == 0)
            /*���� POST ����������*/
            for (i = 0; i < content_length; i++) {
                recv(client, &c, 1, 0);
                /*�� POST ����д�� cgi_input�������ض��� STDIN */
                write(cgi_input[1], &c, 1);
            }
        /*��ȡ cgi_output �Ĺܵ�������ͻ��ˣ��ùܵ������� STDOUT */
        while (read(cgi_output[0], &c, 1) > 0)
            send(client, &c, 1, 0);

        /*�رչܵ�*/
        close(cgi_output[0]);
        close(cgi_input[1]);
        /*�ȴ��ӽ���*/
        waitpid(pid, &status, 0);
    }
}

int get_line(int sock, char* buf, int size)
{
    int i = 0;
    char c = '\0';
    int n;

    /*����ֹ����ͳһΪ \n ���з�����׼�� buf ����*/
    while ((i < size - 1) && (c != '\n'))
    {
        /*һ�ν�����һ���ֽ�*/
        n = recv(sock, &c, 1, 0);
        /* DEBUG printf("%02X\n", c); */
        if (n > 0)
        {
            /*�յ� \r ����������¸��ֽڣ���Ϊ���з������� \r\n */
            if (c == '\r')
            {
                /*ʹ�� MSG_PEEK ��־ʹ��һ�ζ�ȡ��Ȼ���Եõ���ζ�ȡ�����ݣ�����Ϊ���մ��ڲ�����*/
                n = recv(sock, &c, 1, MSG_PEEK);
                /* DEBUG printf("%02X\n", c); */
                /*������ǻ��з���������յ�*/
                if ((n > 0) && (c == '\n'))
                    recv(sock, &c, 1, 0);
                else
                    c = '\n';
            }
            /*�浽������*/
            buf[i] = c;
            i++;
        }
        else
            c = '\n';
    }
    buf[i] = '\0';

    /*���� buf �����С*/
    return(i);
}

/*�ɹ�200*/
void headers(int client, const char* filename)
{
    char buf[1024];
    (void)filename;  /* could use filename to determine file type */

    /*������ HTTP header */
    strcpy(buf, "HTTP/1.0 200 OK\r\n");
    send(client, buf, strlen(buf), 0);
    /*��������Ϣ*/
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

    /* 404 ҳ�� */
    sprintf(buf, "HTTP/1.0 404 NOT FOUND\r\n");
    send(client, buf, strlen(buf), 0);
    /*��������Ϣ*/
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

    /*��ȡ������ header */
    buf[0] = 'A'; buf[1] = '\0';
    while ((numchars > 0) && strcmp("\n", buf))  /* read & discard headers */
        numchars = get_line(client, buf, sizeof(buf));

    /*�� sever ���ļ�*/
    resource = fopen(filename, "r");
    if (resource == NULL)
        not_found(client);
    else
    {
        /*д HTTP header */
        headers(client, filename);
        /*�����ļ�*/
        cat(client, resource);
    }
    fclose(resource);
}

int startup(u_short* port)
{
    int httpd = 0;  //���������socket������
    struct sockaddr_in name;//����sockaddr_in�ͽṹ�������󶨷������˵�IP��ַ�Ͷ˿�

    /*���� �������� socket */
    httpd = socket(PF_INET, SOCK_STREAM, 0);//PF_INET ��ַ����ipv4-- SOCK_STREAM��socket����--0���Զ�ѡ��Э������ 
    if (httpd == -1)
        error_die("socket");//�����ж�
    memset(&name, 0, sizeof(name));
    name.sin_family = AF_INET;//��ַ����ipv4
    name.sin_port = htons(*port);//�˿�ת��Ϊ�����ֽ���
    name.sin_addr.s_addr = htonl(INADDR_ANY);//�����������ip��ַ���ѱ����ֽ���ת��Ϊ�����ֽ���
    if (bind(httpd, (struct sockaddr*)&name, sizeof(name)) < 0)//�󶨵�ַ
        error_die("bind");
    /*�����ǰָ���˿��� 0����̬�������һ���˿�*/
    if (*port == 0)  /* ����˿ں�=0�������ѡȡ���ö˿�*/
    {
        socklen_t namelen = sizeof(name);
        if (getsockname(httpd, (struct sockaddr*)&name, &namelen) == -1)//
            error_die("getsockname");
        *port = ntohs(name.sin_port);//�޸Ķ˿ںţ������ֽ���ת���ɱ����ֽ���
    }
    /*��ʼ����*/
    if (listen(httpd, 5) < 0)
        error_die("listen");
    /*���� socket id */
    return(httpd);
}

/*������501��������������get����post��������������������*/
void unimplemented(int client)
{
    char buf[1024];

    /* HTTP method ����֧��*/
    sprintf(buf, "HTTP/1.0 501 Method Not Implemented\r\n");
    send(client, buf, strlen(buf), 0);
    /*��������Ϣ*/
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
    int server_sock = -1;   //���������socket������
    u_short port = 0;       //��������������˿�
    int client_sock = -1;   //����ͻ���socket������
    struct sockaddr_in client_name; //����socketddr_in �ͽṹ��
    socklen_t client_name_len = sizeof(client_name);//��ȡ�ͻ��˵�ַ����
    pthread_t newthread;    //�����߳�id


    /*�ڶ�Ӧ�˿ڽ��� httpd ����*/
    server_sock = startup(&port);//��ʼ��������
    printf("httpd running on port %d\n", port);//��ӡ�˿ں�

    /*ѭ���������Ӻ����߳�*/
    while (1)
    {
        /*�׽����յ��ͻ�����������*/
        client_sock = accept(server_sock, (struct sockaddr*)&client_name, &client_name_len);//�����ȴ��ͻ��˽�������
        if (client_sock == -1)
            error_die("accept");
        /*�������߳��� accept_request ��������������*/
        /* �������̴߳������� */
        if (pthread_create(&newthread, NULL, accept_request, (void*)&client_sock) != 0)
            perror("pthread_create");
    }

    close(server_sock);

    return(0);
}