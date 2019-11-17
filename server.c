#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/errno.h>
#include <sys/poll.h>
#include <sys/epoll.h>
#define SELECT 0
#define POLL 1
#define EPOLL 2

#define STUNO 1
#define PID 2
#define TIME 3
#define STR 4
#define END 5

#define MAXLOG 1024
#define MAXSIZE 1024
#define MAXCONNECTION 1024

#define QUIT_ERROR -1
#define MISSION_COMPLETE -2
#define UNABLE_RW -3

#define CONNECTION_SUCCESS 1
#define CONNECTION_AWAIT -1
#define CONTINUE 2
#define BREAK 1

#define DEBUG_MODE 0     //为1时server会有WRONG_PERCENT概率故意发错，检查client的重连
#define WRONG_PERCENT 25 //出错的概率1~100,25表示百分之二十五

char message[6][10] = {"", "StuNo", "pid", "TIME", "str00000", "end"};
typedef struct
{
    int step; //1-5对应5个收发状态,1为初始化的值
    int flag; //1代表没有执行，0代表执行完成，初始为1
} STATE;
typedef struct
{
    int socket;
    int lenStr;
    unsigned int stuno;
    unsigned int pid;
    int pos;
    char time[20];
    char *str;
    STATE state;
    int n_str;    //收到的str长度
    int poll_pos; //该套接字在poll_fds结构体数组中的下标
} SOCK;

int my_write_tofile(SOCK *fd)
{
    char name[40];
    sprintf(name, "./txt/%d.%d.pid.txt", fd->stuno, fd->pid);
    if (access("./txt/", 0) == -1)                             //不存在文件夹
        mkdir("./txt", S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH); //新建文件夹
    int file = open(name, O_WRONLY | O_CREAT | O_APPEND);
    if (file == -1)
    {
        perror("open");
        return -1;
    }
    char s[50];

    //sprintf(s, "%d\n%d\n%s\n", fd->stuno, fd->pid, fd->time);
    int offset = 0;
    offset += sprintf(s, "%d\n%d\n", fd->stuno, fd->pid);
    int i;
    for (i = 0; i < 19; i++)
    {
        offset += sprintf(s + offset, "%c", fd->time[i]);
    }
    sprintf(s + offset, "\n");
    write(file, s, strlen(s));
    write(file, fd->str, fd->lenStr);
    close(file);

    return 1;
}

//在my_read_noblock函数中使用的read套接字后的错误判断
int read_error_judge(int ret, SOCK *fd, int *connect_num)
{
    if (ret == -1 && (errno != EAGAIN || errno != EWOULDBLOCK))
    {
        printf("1\n");
        perror("read");
        return QUIT_ERROR;
    }
    if (ret == 0)
    {
        if (fd->state.step == END)
        {
            printf("ss%d[%d] 正常退出\n", fd->socket, fd->pos);
            //实现写入文件的操作
            if (my_write_tofile(fd))
                printf("ss%d成功写入文件\n", fd->socket);
            else
                printf("ss%d写入文件失败\n", fd->socket);
            (*connect_num)++; //计数，成功退出的client的数量
            return MISSION_COMPLETE;
        }
        else
        {
            printf("ss%d 异常退出\n", fd->socket);
            return QUIT_ERROR;
            /*to do  实现client端套接字退出后的维护工作*/
        }
    }
    if (errno == EWOULDBLOCK)
        return UNABLE_RW;
}

//寻找一个文件描述符是否在epoll_wait返回的events数组中，可读
int IsInEvents_Readable(int fd, struct epoll_event events[], int ret)
{
    int i;
    for (i = 0; i < ret; i++)
    {
        if ((fd == events[i].data.fd) && (events[i].events & EPOLLIN))
            return 1;
    }
    return 0;
}

//寻找一个文件描述符是否在epoll_wait返回的events数组中，可写
int IsInEvents_Writeable(int fd, struct epoll_event events[], int ret)
{
    int i;
    for (i = 0; i < ret; i++)
    {
        if ((fd == events[i].data.fd) && (events[i].events & EPOLLOUT))
            return 1;
    }
    return 0;
}

/*
    读工具函数:当有数据可读时调用，目前测试过适用于select/poll/epoll
*/
int my_read_function(SOCK *fd, int *connect_num)
{
    if (fd->state.step == STUNO)
    {
        char buf[4];
        int n = read(fd->socket, buf, 4);
        int ret = read_error_judge(n, fd, connect_num);
        if (ret == QUIT_ERROR)
            return QUIT_ERROR;
        if (ret == MISSION_COMPLETE)
            return MISSION_COMPLETE;

        if (n == 4) //判断是否接收到四字节
        {
            //存入socket结构体
            /*TODO*/
            unsigned int client_num = ntohl(*((int *)buf));
            printf("收到ss%d 发送的学号%d\n", fd->socket, client_num);
            fd->stuno = client_num;
            fd->state.step++;
            fd->state.flag = 1;
        }
        else
        {
            if (ret == UNABLE_RW)
                return UNABLE_RW;
            //收到错误数据，中断连接
            unsigned int client_num = ntohl(*((int *)buf));
            printf("收到错误学号%d，中断与ss%d 的连接\n", client_num, fd->socket);
            /*to do  实现client端套接字退出后的维护工作*/
            return QUIT_ERROR;
        }
        return n;
    }
    if (fd->state.step == PID)
    {
        char buf[4];
        int n = read(fd->socket, buf, 4);
        int ret = read_error_judge(n, fd, connect_num);
        if (ret == QUIT_ERROR)
            return QUIT_ERROR;
        if (ret == MISSION_COMPLETE)
            return MISSION_COMPLETE;

        if (n == 4) //判断是否接收到四字节
        {
            //存入socket结构体
            unsigned int pid_num = ntohl(*((int *)buf));
            printf("收到ss%d 发送的进程号 %d\n", fd->socket, pid_num);
            fd->pid = pid_num;
            fd->state.step++;
            fd->state.flag = 1;
        }
        else
        {
            if (ret == UNABLE_RW)
                return UNABLE_RW;
            //收到错误数据，中断连接
            printf("收到错误进程号，中断与ss%d 的连接\n", fd->socket);
            /*to do  实现client端套接字退出后的维护工作*/
            return QUIT_ERROR;
        }
        return n;
    }
    if (fd->state.step == TIME)
    {
        int n = read(fd->socket, fd->time, 19);
        int ret = read_error_judge(n, fd, connect_num);
        if (ret == QUIT_ERROR)
            return QUIT_ERROR;
        if (ret == MISSION_COMPLETE)
            return MISSION_COMPLETE;

        if (n != 19) //判断是否接收到19字节
        {
            if (ret == UNABLE_RW)
                return UNABLE_RW;
            //收到错误数据，中断连接
            printf("收到错误时间戳，中断与ss%d 的连接\n", fd->socket);
            /*to do  实现client端套接字退出后的维护工作*/
            return QUIT_ERROR;
        }
        printf("收到ss%d 发送的时间戳 %s\n", fd->socket, fd->time);
        fd->state.step++;
        fd->state.flag = 1;
        return n;
    }
    if (fd->state.step == STR)
    {
        if (fd->n_str == 0)
            fd->str = (char *)malloc(fd->lenStr * sizeof(char));
        // printf("%d",fd->n_str);
        int n = read(fd->socket, &fd->str[fd->n_str], fd->lenStr);
        int ret = read_error_judge(n, fd, connect_num);
        if (ret == QUIT_ERROR)
            return QUIT_ERROR;
        if (ret == MISSION_COMPLETE)
            return MISSION_COMPLETE;

        //收到错误数据，中断连接
        //printf("收到ss%d长度字符串%d\n", fd->socket, n);
        if (n > 0)
            fd->n_str += n;
        /*to do  实现client端套接字退出后的维护工作*/
        if (fd->n_str == fd->lenStr)
        {
            printf("收到ss%d发送的正确长度字符串\n", fd->socket);
            fd->state.step++;
            fd->state.flag = 1;
        }
        if (ret == UNABLE_RW)
            return UNABLE_RW;

        return n;
    }
    if (fd->state.step == END)
    {
        char temp[10];
        int n = read(fd->socket, temp, 1);
        int ret = read_error_judge(n, fd, connect_num);
        if (ret == QUIT_ERROR)
            return QUIT_ERROR;
        if (ret == MISSION_COMPLETE)
            return MISSION_COMPLETE;

        if (n > 0)
        {
            printf("ss%d 结束交互后未退出\n", fd->socket);
            //中断与client的连接
            /*to do  实现client端套接字退出后的维护工作*/
            return QUIT_ERROR;
        }
        if (ret == UNABLE_RW)
            return UNABLE_RW;
        return n;
    }
    return 0;
}

/*
    POLL方式调用：server读某个client发送的数据
*/
int my_read_POLL(SOCK *fd, struct pollfd poll_fd, int *connect_num)
{
    if (poll_fd.revents & POLLIN)
    {
        //printf("接下来执行my_read_noblock\n");
        return my_read_function(fd, connect_num);
    }

    return UNABLE_RW;
}
/*
    EPOLL方式调用：server读某个client发送的数据
*/
int my_read_EPOLL(SOCK *fd, struct epoll_event events[], int func_ret, int *connect_num)
{
    if (IsInEvents_Readable(fd->socket, events, func_ret))
        return my_read_function(fd, connect_num);
    return UNABLE_RW;
}
/*
    SELECT方式调用：server读某个client发送的数据
*/
int my_read_SELECT(SOCK *fd, fd_set *my_read, int *connect_num)
{
    if (FD_ISSET(fd->socket, my_read))
        return my_read_function(fd, connect_num);
    return UNABLE_RW;
}

/*
    写工具函数:当有数据可写时调用，目前测试过适用于select/poll/epoll
*/
int my_write_function(SOCK *fd)
{
    //printf("我是my_write_function\n");
    int length = strlen(message[fd->state.step]);
    if ((fd->state.step == STR) && fd->state.flag)
    {
        char s[6];
        sprintf(s, "%d\0", fd->lenStr);
        strcpy(&message[STR][3], s);
        length++;
        int n = write(fd->socket, message[fd->state.step], length);
        if (n == -1)
        {
            perror("write1");
            return QUIT_ERROR;
        }
        printf("ss%d向client端成功发送了%s\n", fd->socket, message[fd->state.step]);
        fd->state.flag = 0;
        return n;
    }
    else if ((fd->state.step == TIME) && fd->state.flag)
    {
        length++;
        int n = write(fd->socket, message[fd->state.step], length);
        if (n == -1)
        {
            perror("write2");
            return QUIT_ERROR;
        }
        printf("ss%d向client端成功发送了%s\n", fd->socket, message[fd->state.step]);
        fd->state.flag = 0;
        return n;
    }
    else if (((fd->state.step == STUNO) && fd->state.flag) || ((fd->state.step == PID) && fd->state.flag) || ((fd->state.step == END) && fd->state.flag))
    {
        int n;
        if (DEBUG_MODE)
        {
            int correct = rand() % (100 / WRONG_PERCENT);
            if (fd->state.step == END)
            {
                if (0 == correct)
                {
                    n = write(fd->socket, "END", length); //模拟错误
                    return QUIT_ERROR;
                }
            }
        }
        n = write(fd->socket, message[fd->state.step], length);
        if (n == -1)
        {
            perror("write3");
            return QUIT_ERROR;
        }
        printf("ss%d向client端成功发送了%s\n", fd->socket, message[fd->state.step]);
        fd->state.flag = 0;
        return n;
    }
    return 0;
}

/*
    POLL方式调用：server写某个client发送的数据
*/
int my_write_POLL(SOCK *fd, struct pollfd poll_fd)
{
    if (poll_fd.revents & POLLOUT)
    {
        //printf("my_write_function\n");
        return my_write_function(fd);
    }

    return UNABLE_RW;
}
/*
    EPOLL方式调用：server写某个client发送的数据
*/
int my_write_EPOLL(SOCK *fd, struct epoll_event events[], int func_ret)
{
    if (IsInEvents_Writeable(fd->socket, events, func_ret))
        return my_write_function(fd);
    return UNABLE_RW;
}
/*
    SELECT方式调用：server写某个client发送的数据
*/
int my_write_SELECT(SOCK *fd, fd_set *my_write)
{

    if (FD_ISSET(fd->socket, my_write))
        return my_write_function(fd);
    return UNABLE_RW;
}

int init_SOCK(SOCK *fd, int socket_fd)
{
    srand((unsigned)time(NULL)); //随机数种子
    fd->state.flag = 1;
    fd->state.step = 1;
    fd->socket = socket_fd;
    fd->n_str = 0;
    fd->lenStr = rand() % 67232 + 32768;
    fd->poll_pos = 0;
    fd->str = NULL;
    return 1;
}

SOCK *build_SOCK(int socket_fd)
{
    SOCK *new_SOCK = (SOCK *)malloc(sizeof(SOCK));
    if (NULL != new_SOCK)
    {
        if (init_SOCK(new_SOCK, socket_fd))
        {
            //printf("建立 client socket %d 结构体成功\n", new_SOCK->socket);
            return new_SOCK;
        }
    }
    else
    {
        //printf("错误：建立 client socket %d 结构体失败\n", socket_fd);
        return NULL;
    }
    return NULL;
}

typedef struct
{
    int data[MAXSIZE];
    int front; //队列头
    int rear;  //队列尾
    int size;  //队列大小
} Queue;
void QPush(Queue *q, int item)
{
    if (q->size == MAXSIZE)
    {
        printf("队列已满\n");
        return;
    }
    q->rear++;
    q->rear %= MAXSIZE;
    q->size++;
    q->data[q->rear] = item;
}
Queue *CreateQueue()
{
    Queue *q = (Queue *)malloc(sizeof(Queue));
    if (!q)
    {
        printf("空间不足\n");
        return NULL;
    }
    q->front = -1;
    q->rear = -1;
    q->size = 0;
    int i;
    for (i = 0; i < FD_SETSIZE; i++)
    {
        QPush(q, i); //将套接字数组全体下表加入可用套接字队列
    }
    return q;
}

int QIsEmpty(Queue *q)
{
    return (q->size == 0);
}
int QPop(Queue *q)
{
    int item;
    if (QIsEmpty(q))
    {
        printf("队列为空\n");
        return -1; //空时返回-1
    }
    q->front++;
    q->front %= MAXSIZE;
    q->size--;
    item = q->data[q->front];
    return item;
}

struct server_conf
{
    struct sockaddr_in server_addr; //server IP & Port
    int block;                      //0nonblock 1 block
    int fork;                       //0nofork   1 fork
    int select;                     //0select 1 poll 2 epoll -1 null
};
void server_conf_argv(int argc, char *argv[], struct server_conf *server)
{
    //可以检查非法的IP，可以检查冲突参数 但是没有检查ip和port后一个参数的数据类型是否合法
    int i;
    char block = 0;
    char fork = 0;
    char select = 0;
    char ip = 0;
    char po = 0;
    //重复参数标志
    if (argc <= 2)
    {
        printf("输入参数过少\n");
        exit(-1);
    }
    server->server_addr.sin_family = AF_INET;                //设置为IP通信
    server->server_addr.sin_addr.s_addr = htons(INADDR_ANY); //服务器IP地址--允许连接到所有本地地址上
    for (i = 1; i < argc; i++)
    {
        if (0 == strcmp("--ip", argv[i]))
        {
            if (1 == ip)
            {
                printf("参数ip输入冲突\n");
                exit(-1);
            }
            ip = 1;
            int temp = inet_addr(argv[++i]);
            if (temp == -1)
            {
                printf("IP地址格式输入错误\n");
                exit(-1);
            }
            server->server_addr.sin_addr.s_addr = temp; //服务器IP地址
            continue;
        }
        if (0 == strcmp("--port", argv[i]))
        {
            if (1 == po)
            {
                printf("参数port输入冲突\n");
                exit(-1);
            }
            po = 1;
            int port = atoi(argv[++i]);
            server->server_addr.sin_port = htons(port); //服务器端口号
            continue;
        }
        if (0 == strcmp("--block", argv[i]))
        {
            if (1 == block)
            {
                printf("参数block输入冲突\n");
                exit(-1);
            }
            block = 1;
            server->block = 1;
            continue;
        }
        if (0 == strcmp("--nonblock", argv[i]))
        {
            if (1 == block)
            {
                printf("参数block输入冲突\n");
                exit(-1);
            }
            block = 1;
            server->block = 0;
            continue;
        }
        if (0 == strcmp("--fork", argv[i]))
        {
            if (1 == fork)
            {
                printf("参数fork输入冲突\n");
                exit(-1);
            }
            fork = 1;
            server->fork = 1;
            continue;
        }
        if (0 == strcmp("--nofork", argv[i]))
        {
            if (1 == fork)
            {
                printf("参数fork输入冲突\n");
                exit(-1);
            }
            fork = 1;
            server->fork = 0;
            continue;
        }
        if (0 == strcmp("--select", argv[i]))
        {
            if (1 == select)
            {
                printf("参数select输入冲突\n");
                exit(-1);
            }
            select = 1;
            server->select = 0;
            continue;
        }
        if (0 == strcmp("--poll", argv[i]))
        {
            if (1 == select)
            {
                printf("参数select输入冲突\n");
                exit(-1);
            }
            select = 1;
            server->select = 1;
            continue;
        }
        if (0 == strcmp("--epoll", argv[i]))
        {
            if (1 == select)
            {
                printf("参数select输入冲突\n");
                exit(-1);
            }
            select = 1;
            server->select = 2;
            continue;
        }
        printf("输入了无效参数%s\n", argv[i]);
        exit(-1);
    }
    if (0 == server->fork) //nofork和block同时出现block无效
        server->block = 0;
    else
        server->select = -1;
}
void print_server_conf(struct server_conf server)
{
    
    system("rm -rf txt");
    printf("---------------------------------------\n");
    printf("程序运行前删除当前目录下的txt文件\n");
    printf("服务器配置信息：\n");
    printf("IP:%s\nPORT:%d\n", inet_ntoa(server.server_addr.sin_addr), ntohs(server.server_addr.sin_port));
    printf(server.block ? "block\n" : "nonblock\n");
    printf(server.fork ? "fork\n" : "nofork\n");
    if (0 == server.select)
        printf("select\n");
    else if (1 == server.select)
        printf("poll\n");
    else if (2 == server.select)
        printf("epoll\n");
}

int no_block(int fd)
{

    int flags;
    if ((flags = fcntl(fd, F_GETFL, NULL)) < 0)
    {

        return -1;
    }
    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1)
    {

        return -1;
    }
}
/*工具函数*/
int handle_socket(int ret, int pos, Queue *Q, SOCK **fd, int *connect_now, int connect_num)
{
    if (QUIT_ERROR == ret || MISSION_COMPLETE == ret)
    {
        close((*fd)->socket);
        if ((*fd)->str)
            free((*fd)->str);
        if (*fd)
            free(*fd);
        *fd = NULL;
        QPush(Q, pos); //队列中加入可用下标
        (*connect_now)--;
        if (QUIT_ERROR == ret)
            printf("退出错误\n");
        printf("当前连接数:%d\n当前完成client的计数:%d\n", *connect_now, connect_num);
        return BREAK;
    }
    return CONTINUE;
}

int handle_socket_EPOLL(int ret, int pos, Queue *Q, SOCK **fd, int *connect_now, int connect_num, int efd)
{
    if (QUIT_ERROR == ret || MISSION_COMPLETE == ret)
    {
        int epoll_ret = epoll_ctl(efd, EPOLL_CTL_DEL, (*fd)->socket, NULL);
        if (epoll_ret == -1)
        {
            perror("epoll_ctl");
            return -1;
        }
        close((*fd)->socket);
        if ((*fd)->str)
            free((*fd)->str);
        if (*fd)
            free(*fd);
        *fd = NULL;
        QPush(Q, pos); //队列中加入可用下标
        (*connect_now)--;
        if (QUIT_ERROR == ret)
            printf("退出错误\n");
        printf("当前连接数:%d\n当前完成client的计数:%d\n", *connect_now, connect_num);
        return BREAK;
    }
    return CONTINUE;
}

/*工具函数*/
int my_new_connection(int server_sockfd, int *connect_now, Queue *Q, SOCK **client_SOCK, struct sockaddr_in client)
{
    int address_size = sizeof(struct sockaddr_in); //sockaddr_in结构体size
    printf("---------------------------------------\n");
    printf("检测到新客户端请求连接\n");
    int new_sockfd_pos = QPop(Q); //获取新建套接字在套接字维护数组中的下标

    int new_socket = accept(server_sockfd, (struct sockaddr *)&client, &address_size);
    if (new_socket == -1)
    {
        perror("call to accept");
        exit(1);
    }
    client_SOCK[new_sockfd_pos] = build_SOCK(new_socket);

    no_block(client_SOCK[new_sockfd_pos]->socket); //对新套接字设置非阻塞模式
    printf("---------------------------------------\n");
    printf("客户端连接成功！\nCLIENT_IP:%s\nCLIENT_PORT:%d\n",
           inet_ntoa(client.sin_addr),
           ntohs(client.sin_port));
    printf("套接字序号:%d   总连接数:%d\n", client_SOCK[new_sockfd_pos]->socket, ++*(connect_now));
    printf("new socket pos %d\n", new_sockfd_pos);
    return CONNECTION_SUCCESS;
}

int my_new_connection_SELECT(int server_sockfd, fd_set *rest, int *connect_now, Queue *Q, SOCK **client_SOCK, struct sockaddr_in client)
{
    if (FD_ISSET(server_sockfd, rest) && ((*connect_now) <= MAXCONNECTION)) //新客户端连接
        return my_new_connection(server_sockfd, connect_now, Q, client_SOCK, client);
    else
        return CONNECTION_AWAIT;
}

int my_new_connection_POLL(int server_sockfd, struct pollfd poll_fd[], int *connect_now, Queue *Q, SOCK **client_SOCK, struct sockaddr_in client, int *now_use_pos)
{
    if ((poll_fd[0].revents & POLLIN) && ((*connect_now) <= MAXCONNECTION)) //新客户端连接
    {
        //初始化新建立连接的pollfd结构体
        int address_size = sizeof(struct sockaddr_in); //sockaddr_in结构体size
        printf("---------------------------------------\n");
        printf("检测到新客户端请求连接\n");
        int new_sockfd_pos = QPop(Q); //获取新建套接字在套接字维护数组中的下标
        int new_socket = accept(server_sockfd, (struct sockaddr *)&client, &address_size);
        if (new_socket == -1)
        {
            perror("call to accept");
            exit(1);
        }
        client_SOCK[new_sockfd_pos] = build_SOCK(new_socket);
        no_block(client_SOCK[new_sockfd_pos]->socket); //对新套接字设置非阻塞模式
        printf("---------------------------------------\n");
        printf("客户端连接成功！\nCLIENT_IP:%s\nCLIENT_PORT:%d\n",
               inet_ntoa(client.sin_addr),
               ntohs(client.sin_port));
        printf("套接字序号:%d   总连接数:%d\n", client_SOCK[new_sockfd_pos]->socket, ++*(connect_now));
        poll_fd[*now_use_pos].fd = new_socket;
        poll_fd[*now_use_pos].events = POLLIN | POLLOUT;
        client_SOCK[new_sockfd_pos]->poll_pos = *now_use_pos;
        (*now_use_pos)++;

        //printf("new socket pos %d\n", new_sockfd_pos);
    }
    else
        return CONNECTION_AWAIT;
}
int my_new_connection_EPOLL(int server_sockfd, struct epoll_event epoll_fds[], int func_ret, int *connect_now, Queue *Q, SOCK **client_SOCK, struct sockaddr_in client, int efd)
{
    if (IsInEvents_Readable(server_sockfd, epoll_fds, func_ret) && ((*connect_now) <= MAXCONNECTION)) //新客户端连接
    //if ((*connect_now) <= MAXCONNECTION)
    {

        int address_size = sizeof(struct sockaddr_in); //sockaddr_in结构体size
        printf("---------------------------------------\n");
        printf("检测到新客户端请求连接\n");
        int new_sockfd_pos = QPop(Q); //获取新建套接字在套接字维护数组中的下标
        int new_socket = accept(server_sockfd, (struct sockaddr *)&client, &address_size);
        if (new_socket == -1)
        {
            perror("call to accept");
            exit(1);
        }
        client_SOCK[new_sockfd_pos] = build_SOCK(new_socket);
        no_block(client_SOCK[new_sockfd_pos]->socket); //对新套接字设置非阻塞模式
        printf("---------------------------------------\n");
        printf("客户端连接成功！\nCLIENT_IP:%s\nCLIENT_PORT:%d\n",
               inet_ntoa(client.sin_addr),
               ntohs(client.sin_port));
        printf("套接字序号:%d   总连接数:%d\n", client_SOCK[new_sockfd_pos]->socket, ++*(connect_now));

        //加入监听
        struct epoll_event epoll_temp;
        epoll_temp.data.fd = new_socket;
        epoll_temp.events = EPOLLIN | EPOLLOUT;
        int epoll_ret = epoll_ctl(efd, EPOLL_CTL_ADD, client_SOCK[new_sockfd_pos]->socket, &epoll_temp);
        if (epoll_ret == -1)
        {
            perror("epoll_ctl");
            return -1;
        }
        return CONNECTION_SUCCESS;
    }
    else
        return CONNECTION_AWAIT;
}

void client_SOCK_init(SOCK **client_SOCK)
{
    int i;
    for (i = 0; i < FD_SETSIZE; i++)
    {
        client_SOCK[i] = NULL;
    }
}
void server_tcp_init(struct server_conf server, int *server_sockfd)
{
    int reuse0 = 1;                                   //端口复用
    *server_sockfd = socket(AF_INET, SOCK_STREAM, 0); //IPV4 TCP协议

    if (*server_sockfd == -1) //申请失败
    {
        perror("call to socket");
        exit(1);
    }

    if (server.block == 0)
        no_block(*server_sockfd); //nonblock设置非阻塞

    if (setsockopt(*server_sockfd, SOL_SOCKET, SO_REUSEADDR, (char *)&reuse0, sizeof(reuse0)) == -1)
    {
        perror("reuse");
        exit(1);
    }

    if (bind(*server_sockfd, (struct sockaddr *)&server.server_addr, sizeof(server.server_addr)) == -1)
    {
        perror("call to bind");
        exit(1);
    }

    if (listen(*server_sockfd, MAXLOG) == -1) //server_sockfd监听套接字，监听队列长度MAXLOG(1024)
    {
        perror("call to listen");
        exit(1);
    }
}

void FD_init(fd_set *rest, fd_set *west, int server_sockfd, SOCK **client_SOCK)
{
    FD_ZERO(rest);               //清空读操作符
    FD_ZERO(west);               //清空写操作符
    FD_SET(server_sockfd, rest); //把监听套接字放入读操作符
    FD_SET(server_sockfd, west); //把监听套接字放入写操作符
    int i;
    for (i = 0; i < FD_SETSIZE; i++) //遍历已使用套接字数组
    {
        if (client_SOCK[i] != NULL) //如果套接字正在被使用
        {
            FD_SET(client_SOCK[i]->socket, rest); //将该套接字加入读操作符
            FD_SET(client_SOCK[i]->socket, west); //将该套接字加入写操作符
        }
    }
}

void POLLFD_init(struct pollfd poll_fds[], int server_sockfd, SOCK **client_SOCK, int *use_now)
{
    memset(poll_fds, 0, sizeof(struct pollfd) * MAXCONNECTION); //清空数组
    poll_fds[0].fd = server_sockfd;                             //把监听套接字放入数组
    poll_fds[0].events = POLLIN;                                //设置关注读事件
    (*use_now)++;
    int i;
    for (i = 0; i < MAXCONNECTION; i++) //遍历已使用套接字数组
    {
        if (client_SOCK[i] != NULL) //如果套接字正在被使用
        {
            poll_fds[*use_now].fd = client_SOCK[i]->socket; //将该套接字加入数组
            poll_fds[*use_now].events = POLLIN | POLLOUT;   //设置关注读/写事件
            client_SOCK[i]->poll_pos = *use_now;
            (*use_now)++;
        }
    }
}

int EPOLLFD_init(int efd, int server_sockfd, SOCK **client_SOCK)
{
    struct epoll_event epoll_temp;

    epoll_temp.data.fd = server_sockfd; //监听socket
    epoll_temp.events = EPOLLIN;
    //将监听socket置入
    int epoll_ret = epoll_ctl(efd, EPOLL_CTL_ADD, server_sockfd, &epoll_temp);
    if (epoll_ret == -1)
    {
        perror("epoll_ctl");
        return -1;
    }
}

void wait2connect(time_t *start_time, int connect_now)
{
    if (0 == connect_now) //当没有客户端连接时
    {
        if (time(NULL) - *start_time >= 5) //每五秒输出一次
        {
            *start_time = time(NULL);
            printf("正在等待客户端连接…………\n");
        }
    }
}

void select_nonblock(int server_sockfd)
{
    /*客户端基本变量*/
    SOCK *client_SOCK[FD_SETSIZE]; //客户端SOCK指针数组,最多1000个连接
    client_SOCK_init(client_SOCK); //客户端SOCK指针数组初始化
    struct sockaddr_in client;     //client的网络地址结构体

    /*垃圾变量*/
    int i = 0;                          //循环标志
    int func_return;                    //read write函数返回值
    time_t start_time = time(NULL) - 5; //开始时间

    /*维护变量*/
    Queue *Q;             //可用套接字下标队列
    Q = CreateQueue();    //记录套接字数组sockfd_inuse中所有可用的角标
    int finished_num = 0; //连接成功个数
    int connect_now = 0;  //连接个数

    /*select变量*/
    fd_set rest, west;      //读写操作符
    struct timeval tempval; //select等待时间
    tempval.tv_sec = 0;     //select等待秒数
    tempval.tv_usec = 0;    //select等待毫秒数

    while (1)
    {
        FD_init(&rest, &west, server_sockfd, client_SOCK);
        wait2connect(&start_time, connect_now);
        int select_return = select(FD_SETSIZE, &rest, &west, NULL, &tempval); //监听套接的可读和可写条件
        if (select_return < 0)
        {
            printf("select error\n");
            return;
        }
        else
        {
            my_new_connection_SELECT(server_sockfd, &rest, &connect_now, Q, client_SOCK, client);
            for (i = 0; i < FD_SETSIZE; i++)
            {
                if (client_SOCK[i])
                {
                    func_return = my_read_SELECT(client_SOCK[i], &rest, &finished_num);
                    if (BREAK == handle_socket(func_return, i, Q, &client_SOCK[i], &connect_now, finished_num))
                        break;
                    func_return = my_write_SELECT(client_SOCK[i], &west);
                    if (BREAK == handle_socket(func_return, i, Q, &client_SOCK[i], &connect_now, finished_num))
                        break;
                }
            }
        }
    }
}

void poll_nonblock(int server_sockfd)
{
    /*客户端基本变量*/
    SOCK *client_SOCK[FD_SETSIZE]; //客户端SOCK指针数组,最多1000个连接
    client_SOCK_init(client_SOCK); //客户端SOCK指针数组初始化
    struct sockaddr_in client;     //client的网络地址结构体

    /*垃圾变量*/
    int i = 0;                          //循环标志
    int func_return;                    //read write函数返回值
    time_t start_time = time(NULL) - 5; //开始时间

    /*维护变量*/
    Queue *Q;             //可用套接字下标队列
    Q = CreateQueue();    //记录套接字数组sockfd_inuse中所有可用的角标
    int finished_num = 0; //连接成功个数
    int connect_now = 0;  //连接个数

    /*poll的结构体数组*/
    struct pollfd poll_fds[MAXCONNECTION];

    while (1)
    {

        int now_use_pos = 0;
        POLLFD_init(poll_fds, server_sockfd, client_SOCK, &now_use_pos);
        wait2connect(&start_time, connect_now);
        int nfds = connect_now + 1; //当前需要监听的套接字数量
        int poll_return = poll(poll_fds, nfds, 0);
        if (poll_return < 0)
        {
            printf("poll error\n");
            exit(1);
        }
        else
        {
            my_new_connection_POLL(server_sockfd, poll_fds, &connect_now, Q, client_SOCK, client, &now_use_pos);
            for (i = 0; i < MAXCONNECTION; i++)
            {
                if (client_SOCK[i])
                {
                    //if(poll_fds)
                    int pos = client_SOCK[i]->poll_pos;
                    //printf("我是poll_pos%d\n",pos);
                    func_return = my_write_POLL(client_SOCK[i], poll_fds[pos]);
                    if (BREAK == handle_socket(func_return, i, Q, &client_SOCK[i], &connect_now, finished_num))
                        break;
                    func_return = my_read_POLL(client_SOCK[i], poll_fds[pos], &finished_num);
                    if (BREAK == handle_socket(func_return, i, Q, &client_SOCK[i], &connect_now, finished_num))
                        break;
                    //printf("****\n");
                }
            }
        }
    }
}

void epoll_nonblock(int server_sockfd)
{
    SOCK *client_SOCK[FD_SETSIZE]; //客户端SOCK指针数组,最多1000个连接
    client_SOCK_init(client_SOCK); //客户端SOCK指针数组初始化
    struct sockaddr_in client;     //client的网络地址结构体

    /*垃圾变量*/
    int i = 0;                          //循环标志
    int func_return;                    //read write函数返回值
    time_t start_time = time(NULL) - 5; //开始时间

    /*维护变量*/
    Queue *Q;             //可用套接字下标队列
    Q = CreateQueue();    //记录套接字数组sockfd_inuse中所有可用的角标
    int finished_num = 0; //连接成功个数
    int connect_now = 0;  //连接个数

    /*epoll变量*/
    struct epoll_event epoll_fds[MAXCONNECTION];
    int efd;
    efd = epoll_create1(0);
    if (efd == -1)
    {
        perror("epoll_create");
        exit(0);
        //return;
    }
    EPOLLFD_init(efd, server_sockfd, client_SOCK);

    while (1)
    {
        wait2connect(&start_time, connect_now);
        int epoll_return = epoll_wait(efd, epoll_fds, MAXCONNECTION, 0);

        my_new_connection_EPOLL(server_sockfd, epoll_fds, epoll_return, &connect_now, Q, client_SOCK, client, efd);
        for (i = 0; i < MAXCONNECTION; i++)
        {
            if (client_SOCK[i]) //正在使用的套接字
            {
                func_return = my_read_EPOLL(client_SOCK[i], epoll_fds, epoll_return, &finished_num);
                if (BREAK == handle_socket_EPOLL(func_return, i, Q, &client_SOCK[i], &connect_now, finished_num, efd))
                    break;
                func_return = my_write_EPOLL(client_SOCK[i], epoll_fds, epoll_return);
                if (BREAK == handle_socket_EPOLL(func_return, i, Q, &client_SOCK[i], &connect_now, finished_num, efd))
                    break;
            }
        }
    }
    close(efd);
}

void fork_block(int listen_sock)
{

    //SOCK *client_SOCK[FD_SETSIZE]; //客户端SOCK指针数组,最多1000个连接
    //client_SOCK_init(client_SOCK); //客户端SOCK指针数组初始化
    struct sockaddr_in client; //client的网络地址结构体
    socklen_t len = sizeof(client);
    int pid = getpid();

    int finished_num = 0; //连接成功个数
    int connect_now = 0;  //连接个数

    int status; //waitpid的状态判断
    char buf[1024];
    while (1)
    {
        printf("阻塞状态等待连接\n");
        int new_sock = accept(listen_sock, (struct sockaddr *)&client, &len);
        //printf("%d",new_sock);
        if (new_sock < 0)
        {
            perror("----accept----fail\n");
            close(listen_sock);
            exit(-5);
        }
        else
        {
            int id;
            id = fork();
            if (id < 0)
            {
                perror("fork");
                return;
            }
            if (id == 0)
            {
                SOCK *fd;
                fd = build_SOCK(new_sock);
                printf("成功连接到client端，进行阻塞状态的信息收发,子进程pid为%d\n", getpid());
                int func_return;
                while (1)
                {

                    func_return = my_write_function(fd);
                    printf("%d", func_return);
                    fflush(stdout);
                    //printf("%d", fd->state.step);
                    if (func_return == QUIT_ERROR)
                    {
                        /*当出现错误时，调用kill -9关闭子进程*/
                        printf("write过程出现错误，使用kill -9 杀死子进程\n");
                        char s[20];
                        sprintf(s, "kill -9 %d", getpid());
                        system(s);
                    }

                    func_return = my_read_function(fd, &finished_num);

                    if (func_return == QUIT_ERROR)
                    {
                        /*当出现错误时，调用kill -9关闭子进程*/
                        close(fd->socket);
                        free(fd->str);
                        free(fd);
                        printf("read过程出现错误，使用kill -9 杀死子进程\n");
                        char s[20];
                        sprintf(s, "kill -9 %d", getpid());
                        system(s);
                    }
                    if (func_return == MISSION_COMPLETE)
                    {
                        /*当完成交互任务后，调用kill -7关闭子进程*/
                        close(fd->socket);
                        free(fd->str);
                        free(fd);
                        printf("完成信息交互任务，用kill -7 结束子进程\n");
                        char s[20];
                        sprintf(s, "kill -7 %d", getpid());
                        system(s);
                    }
                }
            }
            else if (id > 0)
            {
                //建立一个新的连接
                connect_now++;
                //阻塞判断子进程是否完全退出
                waitpid(-1, &status, 0);
                //判断子进程退出状态
                if (WIFSIGNALED(status))
                {
                    //子进程错误退出，无动作执行
                    if (WTERMSIG(status) == 9)
                    {
                        ;
                    }
                    //子进程正常退出，connect_now--
                    if (WTERMSIG(status) == 7)
                    {
                        finished_num++;
                        connect_now--;
                    }
                }
            }
        }
    }

    return;
}
void fork_nonblock(int listen_sock)
{
    no_block(listen_sock);     //设置为非阻塞
    struct sockaddr_in client; //client的网络地址结构体
    socklen_t len = sizeof(client);
    int pid = getpid();

    int finished_num = 0; //连接成功个数
    int connect_now = 0;  //连接个数

    int status; //waitpid的状态判断
    char buf[1024];
    while (1)
    {
        //printf("阻塞状态等待连接\n");
        int new_sock = accept(listen_sock, (struct sockaddr *)&client, &len);
        //printf("%d",new_sock);
        if (new_sock < 0)
        {
            if (errno != EWOULDBLOCK)
            {
                perror("----accept----fail\n");
                close(listen_sock);
                exit(-5);
            }
            continue;
        }
        else
        {
            int id;
            no_block(new_sock); //套接字申请成功后置非阻塞
            id = fork();
            if (id < 0)
            {
                perror("fork");
                return;
            }
            if (id == 0)
            {
                SOCK *fd;
                fd = build_SOCK(new_sock);
                printf("成功连接到client端，进行阻塞状态的信息收发,子进程pid为%d\n", getpid());
                int func_return;
                while (1)
                {
                    func_return = my_write_function(fd);
                    // printf("%d", func_return);
                    //fflush(stdout);
                    //printf("%d", fd->state.step);
                    if (func_return == QUIT_ERROR)
                    {
                        /*当出现错误时，调用kill -9关闭子进程*/
                        printf("write过程出现错误，使用kill -9 杀死子进程\n");
                        char s[20];
                        sprintf(s, "kill -9 %d", getpid());
                        system(s);
                    }
                    //printf("1\n");
                    func_return = my_read_function(fd, &finished_num);
                    if (UNABLE_RW == func_return)
                        continue;
                    if (QUIT_ERROR == func_return)
                    {
                        /*当出现错误时，调用kill -9关闭子进程*/
                        close(fd->socket);
                        free(fd->str);
                        free(fd);
                        printf("read过程出现错误，使用kill -9 杀死子进程\n");
                        char s[20];
                        sprintf(s, "kill -9 %d", getpid());
                        system(s);
                    }
                    if (MISSION_COMPLETE == func_return)
                    {
                        /*当完成交互任务后，调用kill -7关闭子进程*/
                        close(fd->socket);
                        free(fd->str);
                        free(fd);
                        printf("完成信息交互任务，用kill -7 结束子进程\n");
                        char s[20];
                        sprintf(s, "kill -7 %d", getpid());
                        system(s);
                    }
                }
            }
            else if (id > 0)
            {
                //建立一个新的连接
                connect_now++;
                //阻塞判断子进程是否完全退出
                waitpid(-1, &status, 0);
                //判断子进程退出状态
                if (WIFSIGNALED(status))
                {
                    //子进程错误退出，无动作执行
                    if (WTERMSIG(status) == 9)
                    {
                        ;
                    }
                    //子进程正常退出，connect_now--
                    if (WTERMSIG(status) == 7)
                    {
                        finished_num++;
                        connect_now--;
                    }
                }
            }
        }
    }

    return;
}
int main(int argc, char *argv[])
{

    /*服务器变量*/
    struct server_conf server;          //server数据结构SS
    int server_sockfd;                  //监听套接字
    memset(&server, 0, sizeof(server)); //server端配置信息清零

    /*服务器配置*/
    server_conf_argv(argc, argv, &server);   //根据main函数参数配置server
    print_server_conf(server);               //显示server配置信息
    server_tcp_init(server, &server_sockfd); //socket+nonblock?+reuse+bind+listen

    /*多client端处理*/
    if (!server.fork)
    {
        if (server.select == SELECT)
            select_nonblock(server_sockfd); //select非阻塞模式
        else if (server.select == POLL)
            poll_nonblock(server_sockfd); //poll非阻塞模式
        else if (server.select == EPOLL)
            epoll_nonblock(server_sockfd); //epoll非阻塞模式
    }
    else
    {
        if (server.block)
            fork_block(server_sockfd);
        else
            fork_nonblock(server_sockfd);
    }
    close(server_sockfd);
    return 0;
}
