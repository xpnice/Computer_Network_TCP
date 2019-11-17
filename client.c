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
#include <time.h>
#include <sys/timeb.h>
#include <sys/errno.h>
#include <sys/epoll.h>
#include <sys/poll.h>

#define MAX_CON 1000
#define SNO 1753495

#define SELECT 0
#define POLL 1
#define EPOLL 2
#define STUNO 0
#define PID 1
#define TIME 2
#define STR 3
#define END 4

#define CONNECTION_SUCCESS 1
#define CONNECTION_AWAIT -1
#define CONTINUE 2
#define BREAK 1

#define QUIT_ERROR -1
#define MISSION_COMPLETE -2
#define UNABLE_RW -3

typedef struct
{
    int step; //0-4对应5个收发状态
    int flag; //1代表可以写入，0代表不可写入
} STATE;
typedef struct
{
    int socket;         //套接字
    int lenStr;         //应该传输字符串长度
    unsigned int stuno; //学号
    unsigned int pid;   //pid
    int pos;
    char time[20]; //时间戳
    char *str;     //字符串
    STATE state;   //收发状态
    int poll_pos;  //用于poll方式定位该描述符在数组中的位置
} SOCK;

char message[5][10] = {"StuNo", "pid", "TIME", "str00000", "end"};

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

int my_read_noblock(SOCK *fd, char *str)
{
    int o = 0;
    if ((fd->state.step + 1) == TIME || (fd->state.step + 1) == STR)
        o = 1;
    int n = read(fd->socket, str, 50);
    if (n == -1)
    {
        if (errno == EWOULDBLOCK)
            return UNABLE_RW;
        perror("read error");
        return QUIT_ERROR;
    }
    if (n == 0)
    {
        perror("server端断开连接\n");
        return QUIT_ERROR;
    }
    if (n != (strlen(message[fd->state.step + 1]) + o))
    {
        printf("server端传入数据长度错误,应传入%d,实际传入%d", strlen(message[fd->state.step + 1]), n);
        return QUIT_ERROR;
    }
    printf("套接字ss%d[%d]收到了%s\n", fd->socket, fd->pos, str);
    int check_return = chech_read(str, fd);
    return check_return;
}
//检验读入是否正确，正确将step++,再将flag置为1，进行下一次的写出。
//因为在中间进程中处理的step,所以对于read以及write中的step会相差1
int chech_read(char *str, SOCK *fd)
{
    if ((fd->state.step + 1) != STR)
    {
        //处理非str传输
        if (strcmp(str, message[fd->state.step + 1]) == 0)
        {
            fd->state.step++;
            fd->state.flag = 1;
            printf("套接字ss%d[%d]转变为状态%s\n", fd->socket, fd->pos, message[fd->state.step]);
        }
        else
        {
            return QUIT_ERROR;
        }
    }
    else
    {
        //处理str传输
        if ((strncmp(str, message[fd->state.step + 1], 3) == 0) && (strlen(str) == 8))
        {

            fd->lenStr = atoi(&str[3]);
            fd->state.step++;
            fd->state.flag = 1;
        }
        else
        {
            return QUIT_ERROR;
        }
    }
}

/*
    POLL方式调用：server读某个client发送的数据
*/
int my_read_POLL(SOCK *fd, struct pollfd poll_fd, char *str)
{
    if (poll_fd.revents & POLLIN)
        return my_read_noblock(fd, str);
    return UNABLE_RW;
}
/*
    EPOLL方式调用：server读某个client发送的数据
*/
int my_read_EPOLL(SOCK *fd, struct epoll_event events[], char *str, int func_ret)
{
    if (IsInEvents_Readable(fd->socket, events, func_ret))
        return my_read_noblock(fd, str);
    return UNABLE_RW;
}
/*
    SELECT方式调用：server读某个client发送的数据
*/
int my_read_SELECT(SOCK *fd, fd_set *my_read, char *str)
{

    if (FD_ISSET(fd->socket, my_read))
        return my_read_noblock(fd, str);

    return UNABLE_RW;
}

void get_my_time(char s[])
{
    time_t timep;
    struct tm *p;
    time(&timep);
    p = gmtime(&timep);
    sprintf(s, "%4d-%02d-%02d %02d:%02d:%02d", (1900 + p->tm_year), (1 + p->tm_mon), (p->tm_mday), (p->tm_hour + 8) % 24, p->tm_min, p->tm_sec);
}

int my_write_fork(SOCK *fd)
{
    if (fd->state.flag == 1)
    {
        if (fd->state.step == STUNO)
        {
            unsigned int num = htonl(SNO);
            int n = write(fd->socket, &num, 4);

            if (n == -1)
            {
                perror("write error");
                return QUIT_ERROR;
            }
            fd->stuno = SNO;
            fd->state.flag = 0;
            printf("ss%d[%d]向server写入%d\n", fd->socket, fd->pos, SNO);
            return n;
        }
        if (fd->state.step == PID)
        {

            unsigned int num = htonl(getpid());
            int n = write(fd->socket, &num, 4);
            if (n == -1)
            {
                perror("write error");
                return QUIT_ERROR;
            }
            fd->pid = getpid();
            fd->state.flag = 0;
            printf("ss%d[%d]向server写入%d\n", fd->socket, fd->pos, getpid());
            return n;
        }
        if (fd->state.step == TIME)
        {
            char s[20];
            get_my_time(s);

            int n = write(fd->socket, s, 19);
            if (n == -1)
            {
                perror("write error");
                return QUIT_ERROR;
            }
            strncpy(fd->time, s, 19);
            fd->state.flag = 0;
            printf("ss%d[%d]向server写入时间戳%s\n", fd->socket, fd->pos, s);
            return n;
        }
        if (fd->state.step == STR)
        {
            // printf("%d开始向server写入字符串\n", fd->socket);
            fd->str = (char *)malloc(sizeof(char) * fd->lenStr);
            int i;
            for (i = 0; i < fd->lenStr; i++)
                fd->str[i] = rand() % 256;
            int n = write(fd->socket, fd->str, fd->lenStr);
            if (n == -1)
            {
                perror("write error");
                return QUIT_ERROR;
            }
            fd->state.flag = 0;
            if (n == fd->lenStr)
                printf("ss%d[%d]向server写入字符串\n", fd->socket, fd->pos);
            else
            {
                printf("ss%d[%d]写入字符串长度为%d,应为%d，错误\n", fd->socket, fd->pos, n, fd->lenStr);
                return QUIT_ERROR;
            }

            printf("ss%d[%d]写入字符串长度为%d,应为%d，正确\n", fd->socket, fd->pos, n, fd->lenStr);
            return n;
        }
        if (fd->state.step == END)
        {
            my_write_tofile(fd);
            return MISSION_COMPLETE;
        }
    }
}
int my_write_noblock(SOCK *fd, int pos)
{

    if (fd->state.flag == 1)
    {
        if (fd->state.step == STUNO)
        {
            unsigned int num = htonl(SNO);
            int n = write(fd->socket, &num, 4);

            if (n == -1)
            {
                perror("write error");
                return QUIT_ERROR;
            }
            fd->stuno = SNO;
            fd->state.flag = 0;
            printf("ss%d[%d]向server写入%d\n", fd->socket, fd->pos, SNO);
            return n;
        }
        if (fd->state.step == PID)
        {

            //unsigned int num = htonl((getpid() << 16) + fd->socket);//真实socket
            unsigned int num = htonl((getpid() << 16) + pos + 3); //虚假socket
            int n = write(fd->socket, &num, 4);
            if (n == -1)
            {
                perror("write error");
                return QUIT_ERROR;
            }
            fd->pid = (getpid() << 16) + pos + 3;
            fd->state.flag = 0;
            return n;
        }
        if (fd->state.step == TIME)
        {
            char s[20];
            get_my_time(s);

            int n = write(fd->socket, s, 19);
            if (n == -1)
            {
                perror("write error");
                return QUIT_ERROR;
            }
            strncpy(fd->time, s, 19);
            fd->state.flag = 0;
            printf("ss%d[%d]向server写入时间戳%s\n", fd->socket, fd->pos, s);
            return n;
        }
        if (fd->state.step == STR)
        {
            // printf("%d开始向server写入字符串\n", fd->socket);
            fd->str = (char *)malloc(sizeof(char) * fd->lenStr);
            int i;
            for (i = 0; i < fd->lenStr; i++)
                fd->str[i] = rand() % 256;
            int n = write(fd->socket, fd->str, fd->lenStr);
            if (n == -1)
            {
                perror("write error");
                return QUIT_ERROR;
            }
            fd->state.flag = 0;
            if (n == fd->lenStr)
                printf("ss%d[%d]向server写入字符串\n", fd->socket, fd->pos);
            else
            {
                printf("ss%d[%d]写入字符串长度为%d,应为%d，错误\n", fd->socket, fd->pos, n, fd->lenStr);
                return QUIT_ERROR;
            }

            printf("ss%d[%d]写入字符串长度为%d,应为%d，正确\n", fd->socket, fd->pos, n, fd->lenStr);
            return n;
        }
        if (fd->state.step == END)
        {
            my_write_tofile(fd);
            return MISSION_COMPLETE;
        }
    }
}

/*
    POLL方式调用：server写某个client发送的数据
*/
int my_write_POLL(SOCK *fd, struct pollfd poll_fd, int pos)
{
    if (poll_fd.revents & POLLOUT)
        return my_write_noblock(fd, pos);

    return UNABLE_RW;
}
/*
    EPOLL方式调用：server写某个client发送的数据
*/
int my_write_EPOLL(SOCK *fd, struct epoll_event events[], int pos, int func_ret)
{
    if (IsInEvents_Writeable(fd->socket, events, func_ret))
        return my_write_noblock(fd, pos);
    return UNABLE_RW;
}
/*
    SELECT方式调用：server写某个client发送的数据
*/
int my_write_SELECT(SOCK *fd, fd_set *my_write, int pos)
{
    if (FD_ISSET(fd->socket, my_write))
        return my_write_noblock(fd, pos);
    return UNABLE_RW;
}

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

int init_SOCK(SOCK *fd, int socket_fd) //初始化结构体
{
    fd->state.flag = 0;
    fd->state.step = -1;
    fd->socket = socket_fd;
    fd->stuno = 0;
    fd->pid = 0;
    fd->lenStr = 0;
    fd->str = NULL;
    fd->pos = -1;
    return 1;
}

struct client_conf
{
    struct sockaddr_in client_addr; //client IP(缺省全零) & Port
    int block;                      //缺省0nonblock 1 block
    int fork;                       //缺省0nofork   1 fork
    int select;                     //缺省0select 1 poll 2 epoll -1 null
    int num;                        //缺省100
};
void client_conf_argv(int argc, char *argv[], struct client_conf *client)
{
    //可以检查非法的IP 但是没有检查ip和port后一个参数的数据类型是否合法
    int i;
    if (argc <= 2)
    {
        printf("输入参数过少\n");
        exit(-1);
    }
    client->client_addr.sin_family = AF_INET;                //设置为IP通信
    client->client_addr.sin_addr.s_addr = htons(INADDR_ANY); //服务器IP地址--允许连接到所有本地地址上
    client->num = 100;
    for (i = 1; i < argc; i++)
    {
        if (0 == strcmp("--ip", argv[i]))
        {
            int temp = inet_addr(argv[++i]);
            if (temp == -1)
            {
                printf("IP地址格式输入错误\n");
                exit(-1);
            }
            client->client_addr.sin_addr.s_addr = temp; //服务器IP地址
            continue;
        }
        if (0 == strcmp("--port", argv[i]))
        {
            int port = atoi(argv[++i]);
            client->client_addr.sin_port = htons(port); //服务器端口号
            continue;
        }
        if (0 == strcmp("--num", argv[i]))
        {
            int num = atoi(argv[++i]);
            if (num < 1 || num > 1000)
            {
                printf("连接数不合法\n");
                exit(-1);
            }
            client->num = num;
            continue;
        }
        if (0 == strcmp("--block", argv[i]))
        {
            client->block = 1;
            continue;
        }
        if (0 == strcmp("--nonblock", argv[i]))
        {
            client->block = 0;
            continue;
        }
        if (0 == strcmp("--fork", argv[i]))
        {
            client->fork = 1;
            continue;
        }
        if (0 == strcmp("--nofork", argv[i]))
        {
            client->fork = 0;
            continue;
        }
        if (0 == strcmp("--select", argv[i]))
        {
            client->select = 0;
            continue;
        }
        if (0 == strcmp("--poll", argv[i]))
        {
            client->select = 1;
            continue;
        }
        if (0 == strcmp("--epoll", argv[i]))
        {
            client->select = 2;
            continue;
        }

        printf("输入了无效参数%s\n", argv[i]);
        exit(-1);
    }
    if (0 == client->fork) //nofork和block同时出现block无效
        client->block = 0;
    else
        client->select = -1;
}
void print_client_conf(struct client_conf client)
{
    printf("------------------------------\n");
    printf("client端配置信息：\n");
    printf("客户端IP:%s\n客户端PORT:%d\n", inet_ntoa(client.client_addr.sin_addr), ntohs(client.client_addr.sin_port));
    printf(client.block ? "block\n" : "nonblock\n");
    printf(client.fork ? "fork\n" : "nofork\n");
    printf("连接数:%d\n", client.num);
    if (0 == client.select)
        printf("select\n");
    else if (1 == client.select)
        printf("poll\n");
    else if (2 == client.select)
        printf("epoll\n");
}
SOCK *build_SOCK(int socket_fd)
{
    SOCK *new_SOCK = (SOCK *)malloc(sizeof(SOCK));
    if (NULL != new_SOCK)
    {
        if (init_SOCK(new_SOCK, socket_fd))
        {
            return new_SOCK;
        }
    }
    else
    {
        return NULL;
    }
    return NULL;
}
long longgetSystemTime()
{
    struct timeb t;
    ftime(&t);
    return 1000 * t.time + t.millitm;
}
int my_check_nonblock_connection(int connect_return, int socket_new)
{
    fd_set test;
    struct timeval tempval; //select等待时间
    tempval.tv_sec = 1;     //select等待秒数
    tempval.tv_usec = 0;    //select等待毫秒数
    if (connect_return < 0)
    {
        if (errno == EINPROGRESS)
        {
            FD_ZERO(&test);
            FD_SET(socket_new, &test);
            if ((select(FD_SETSIZE, NULL, &test, NULL, &tempval) > 0))
            {
                if (FD_ISSET(socket_new, &test))
                {
                    int error;
                    socklen_t len = sizeof(int);
                    getsockopt(socket_new, SOL_SOCKET, SO_ERROR, &error, &len);
                    if (error == 0)
                    {
                        return CONNECTION_SUCCESS;
                    }
                    return QUIT_ERROR;
                }
                return QUIT_ERROR;
            }
            printf("连接超时\n");
            fflush(stdout);
            return QUIT_ERROR;
        }
        return QUIT_ERROR;
    }
    return QUIT_ERROR;
}
int my_new_connection(int *create, long *start_mt, SOCK **client_SOCK, struct client_conf client)
{
    long now_mt = longgetSystemTime();
    if (*create < client.num && ((now_mt - *start_mt) >= ((rand() % 200) + 70)))
    {
        *start_mt = now_mt;
        int socket_new;
        if ((socket_new = socket(PF_INET, SOCK_STREAM, 0)) < 0)
        {
            perror("socket");
            return 1;
        }
        /*connect前socket设置非阻塞模式*/
        int flags = fcntl(socket_new, F_GETFL, 0);
        fcntl(socket_new, F_SETFL, flags | O_NONBLOCK);
        /*将套接字绑定到服务器的网络地址上*/
        int connect_return = connect(socket_new, (struct sockaddr *)&client.client_addr, sizeof(struct sockaddr));
        if (my_check_nonblock_connection(connect_return, socket_new) == CONNECTION_SUCCESS) //非阻塞情况下检查是否真的连接成功，防止未连接计数
        {
            client_SOCK[*create] = build_SOCK(socket_new); //刚连接时指针为NULL需要动态申请
            client_SOCK[*create]->pos = *create + 3;
            printf("*********************************************\n");
            printf("第%d个连接成功!套接字为:%d[%d]\n", ++(*create), socket_new, client_SOCK[*create]->pos);
        }
        else
        {
            close(socket_new);
            return QUIT_ERROR;
        }
    }
    return CONTINUE;
}

int my_new_connection_EPOLL(int *create, long *start_mt, SOCK **client_SOCK, struct client_conf client, int efd)
{
    long now_mt = longgetSystemTime();
    if (*create < client.num && ((now_mt - *start_mt) >= ((rand() % 200) + 70)))
    {
        *start_mt = now_mt;
        int socket_new;
        if ((socket_new = socket(PF_INET, SOCK_STREAM, 0)) < 0)
        {
            perror("socket");
            return 1;
        }
        /*connect前socket设置非阻塞模式*/
        int flags = fcntl(socket_new, F_GETFL, 0);
        fcntl(socket_new, F_SETFL, flags | O_NONBLOCK);
        /*将套接字绑定到服务器的网络地址上*/
        int connect_return = connect(socket_new, (struct sockaddr *)&client.client_addr, sizeof(struct sockaddr));
        if (my_check_nonblock_connection(connect_return, socket_new) == CONNECTION_SUCCESS) //非阻塞情况下检查是否真的连接成功，防止未连接计数
        {
            client_SOCK[*create] = build_SOCK(socket_new); //刚连接时指针为NULL需要动态申请
            client_SOCK[*create]->pos = *create + 3;
            //加入监听
            struct epoll_event epoll_temp;
            epoll_temp.data.fd = socket_new;
            epoll_temp.events = EPOLLIN | EPOLLOUT;
            int epoll_ret = epoll_ctl(efd, EPOLL_CTL_ADD, client_SOCK[*create]->socket, &epoll_temp);
            if (epoll_ret == -1)
            {

                perror("epoll_ctl");
                printf("我在my_new_connection_EPOLL里\n");
                return -1;
            }
            printf("*********************************************\n");
            ;
            printf("第%d个连接成功!套接字为:%d[%d]\n", ++(*create), socket_new, client_SOCK[*create]->pos);
        }
        else
        {
            close(socket_new);
            return QUIT_ERROR;
        }
    }
    return CONTINUE;
}

int my_reconnect(int pos, SOCK **client_SOCK, struct client_conf client)
{
    printf("套接字%d[%d]正在重连…………\n", client_SOCK[pos]->socket, client_SOCK[pos]->pos);
    //usleep(1000); //延时等待系统close套接字
    int socket_new;
    if ((socket_new = socket(PF_INET, SOCK_STREAM, 0)) < 0)
    {

        perror("socket");
        return 1;
    }
    /*connect前socket设置非阻塞模式*/
    client_SOCK[pos]->socket = socket_new; //重连时结构体只有str没有动态申请，不需要重新申请
    int flags = fcntl(socket_new, F_GETFL, 0);
    fcntl(socket_new, F_SETFL, flags | O_NONBLOCK);
    /*将套接字绑定到服务器的网络地址上*/
    int connect_return = connect(socket_new, (struct sockaddr *)&client.client_addr, sizeof(struct sockaddr));
    if (my_check_nonblock_connection(connect_return, socket_new) == CONNECTION_SUCCESS) //非阻塞情况下检查是否真的连接成功，防止未连接计数
    {
        client_SOCK[pos] = build_SOCK(socket_new); //刚连接时指针为NULL需要动态申请
        client_SOCK[pos]->pos = pos + 3;
        printf("*********************************************\n");
        ;
        printf("重连成功!套接字为:%d,伪装套接字为%d\n", socket_new, client_SOCK[pos]->pos);
        return CONNECTION_SUCCESS;
    }
    /*未连接成功*/
    close(socket_new);
    return QUIT_ERROR;
}

int my_reconnect_EPOLL(int pos, SOCK **client_SOCK, struct client_conf client, int efd)
{
    printf("套接字%d[%d]正在重连…………\n", client_SOCK[pos]->socket);
    //usleep(1000); //延时等待系统close套接字
    int socket_new;
    if ((socket_new = socket(PF_INET, SOCK_STREAM, 0)) < 0)
    {

        perror("socket");
        return 1;
    }
    /*connect前socket设置非阻塞模式*/
    client_SOCK[pos]->socket = socket_new; //重连时结构体只有str没有动态申请，不需要重新申请
    int flags = fcntl(socket_new, F_GETFL, 0);
    fcntl(socket_new, F_SETFL, flags | O_NONBLOCK);
    /*将套接字绑定到服务器的网络地址上*/
    int connect_return = connect(socket_new, (struct sockaddr *)&client.client_addr, sizeof(struct sockaddr));
    if (my_check_nonblock_connection(connect_return, socket_new) == CONNECTION_SUCCESS) //非阻塞情况下检查是否真的连接成功，防止未连接计数
    {
        client_SOCK[pos] = build_SOCK(socket_new); //刚连接时指针为NULL需要动态申请
        client_SOCK[pos]->pos = pos + 3;
        printf("*********************************************\n");
        ;
        printf("重连成功!套接字为:%d,伪装套接字为%d\n", socket_new, client_SOCK[pos]->pos);
        //加入监听
        struct epoll_event epoll_temp;
        epoll_temp.data.fd = socket_new;
        epoll_temp.events = EPOLLIN | EPOLLOUT;
        int epoll_ret = epoll_ctl(efd, EPOLL_CTL_ADD, client_SOCK[pos]->socket, &epoll_temp);
        if (epoll_ret == -1)
        {
            perror("epoll_ctl");
            printf("我在my_reconnect_EPOLL里\n");
            return -1;
        }
        return CONNECTION_SUCCESS;
    }
    /*未连接成功*/
    close(socket_new);
    return QUIT_ERROR;
}

int my_disconnect_SELECT(struct client_conf client, SOCK **client_SOCK, int *finished_num, int *quit, fd_set *west, fd_set *rest, int status, int pos)
{
    if (status != QUIT_ERROR && status != MISSION_COMPLETE)
        return -1;
    FD_CLR(client_SOCK[pos]->socket, west);
    FD_CLR(client_SOCK[pos]->socket, rest);
    close(client_SOCK[pos]->socket);
    if (client_SOCK[pos]->str)
        free(client_SOCK[pos]->str); //对于重连的，如果在str前错误，str尚未申请
    if (status == QUIT_ERROR)
    {
        printf("套接字ss%d[%d]异常退出重新连接\n", client_SOCK[pos]->socket, client_SOCK[pos]->pos);
        printf("*********************************************\n");
        ;
        //close(client_SOCK[pos]->socket);
        my_reconnect(pos, client_SOCK, client);
    }
    else if (status == MISSION_COMPLETE)
    {

        printf("套接字ss%d[%d]完成退出\n", client_SOCK[pos]->socket, client_SOCK[pos]->pos);
        (*finished_num)++;
        printf("当前交互完成数:%d\n", *finished_num);

        if (client.num == *finished_num)
            *quit = 1;
        free(client_SOCK[pos]);
        client_SOCK[pos] = NULL;
    }
}

int my_disconnect_POLL(struct client_conf client, SOCK **client_SOCK, int *finished_num, int *quit, int status, int pos)
{
    if (status != QUIT_ERROR && status != MISSION_COMPLETE)
        return -1;
    close(client_SOCK[pos]->socket);
    if (client_SOCK[pos]->str)
        free(client_SOCK[pos]->str); //对于重连的，如果在str前错误，str尚未申请
    if (status == QUIT_ERROR)
    {
        printf("套接字ss%d[%d]异常退出重新连接\n", client_SOCK[pos]->socket, client_SOCK[pos]->pos);

        //close(client_SOCK[pos]->socket);
        my_reconnect(pos, client_SOCK, client);
    }
    else if (status == MISSION_COMPLETE)
    {

        printf("套接字ss%d[%d]完成退出\n", client_SOCK[pos]->socket, client_SOCK[pos]->pos);
        (*finished_num)++;
        printf("当前完成数:%d\n", *finished_num);

        if (client.num == *finished_num)
            *quit = 1;
        free(client_SOCK[pos]);
        client_SOCK[pos] = NULL;
    }
}

int my_disconnect_EPOLL(struct client_conf client, SOCK **client_SOCK, int *finished_num, int *quit, int status, int pos, int efd)
{
    if (status != QUIT_ERROR && status != MISSION_COMPLETE)
        return -1;

    if (client_SOCK[pos]->str)
        free(client_SOCK[pos]->str); //对于重连的，如果在str前错误，str尚未申请
    if (status == QUIT_ERROR)
    {
        printf("套接字ss%d[%d]异常退出重新连接\n", client_SOCK[pos]->socket, client_SOCK[pos]->pos);
        printf("*********************************************\n");
        ;
        close(client_SOCK[pos]->socket);
        my_reconnect_EPOLL(pos, client_SOCK, client, efd);
    }
    else if (status == MISSION_COMPLETE)
    {

        printf("套接字ss%d[%d]完成退出\n", client_SOCK[pos]->socket, client_SOCK[pos]->pos);
        (*finished_num)++;
        printf("当前完成数:%d\n", *finished_num);

        if (client.num == *finished_num)
            *quit = 1;
        int epoll_ret = epoll_ctl(efd, EPOLL_CTL_DEL, client_SOCK[pos]->socket, NULL);
        if (epoll_ret == -1)
        {
            perror("epoll_ctl");
            printf("我在my_disconnect_EPOLL里\n");
            return -1;
        }
        close(client_SOCK[pos]->socket);
        free(client_SOCK[pos]);
        client_SOCK[pos] = NULL;
    }
}

int FD_init(fd_set *rest, fd_set *west, struct client_conf client, SOCK **client_SOCK)
{
    int k = 0;
    FD_ZERO(rest);
    FD_ZERO(west);
    for (k = 0; k < client.num; k++)
    {
        if (client_SOCK[k])
        {
            FD_SET(client_SOCK[k]->socket, rest);
            FD_SET(client_SOCK[k]->socket, west);
        }
    }
}

void POLLFD_init(struct pollfd poll_fds[], struct client_conf client, SOCK **client_SOCK, int *use_now)
{
    memset(poll_fds, 0, sizeof(poll_fds)); //清空数组
    int i;
    for (i = 0; i < client.num; i++) //遍历已使用套接字数组
    {
        if (client_SOCK[i]) //如果套接字正在被使用
        {
            poll_fds[*use_now].fd = client_SOCK[i]->socket; //将该套接字加入数组
            poll_fds[*use_now].events = POLLIN | POLLOUT;   //设置关注读/写事件
            client_SOCK[i]->poll_pos = *use_now;
            (*use_now)++;
        }
    }
}

int EPOLLFD_init(int efd, struct client_conf client, SOCK **client_SOCK)
{
    struct epoll_event epoll_temp;
    int i;
    for (i = 0; i < client.num; i++) //遍历已使用套接字数组
    {
        if (client_SOCK[i]) //如果套接字正在被使用
        {
            epoll_temp.data.fd = client_SOCK[i]->socket;
            epoll_temp.events = EPOLLIN | EPOLLOUT;
            //加入监听事件
            int epoll_ret = epoll_ctl(efd, EPOLL_CTL_ADD, client_SOCK[i]->socket, &epoll_temp);
            if (epoll_ret == -1)
            {
                perror("epoll_ctl");
                printf("错了!\n");
                return -1;
            }
        }
    }
}

int EPOLLFD_quit(int efd, struct client_conf client, SOCK **client_SOCK)
{
    struct epoll_event epoll_temp;
    int i;
    for (i = 0; i < client.num; i++) //遍历已使用套接字数组
    {
        if (client_SOCK[i]) //如果套接字正在被使用
        {
            epoll_temp.data.fd = client_SOCK[i]->socket;
            epoll_temp.events = EPOLLIN | EPOLLOUT;
            //加入监听事件
            int epoll_ret = epoll_ctl(efd, EPOLL_CTL_ADD, client_SOCK[i]->socket, &epoll_temp);
            if (epoll_ret == -1)
            {
                perror("epoll_ctl");
                printf("错了!\n");
                return -1;
            }
        }
    }
}

void client_SOCK_init(SOCK **client_SOCK, struct client_conf client)
{
    int k = 0; //循环标志
    for (k = 0; k < client.num; k++)
    {
        client_SOCK[k] = NULL;
    }
}
void select_nonblock(struct client_conf client)
{
    SOCK **client_SOCK = (SOCK **)malloc(client.num * sizeof(SOCK *)); //客户端SOCK指针数组,最多1000个连接
    client_SOCK_init(client_SOCK, client);                             //客户端SOCK指针数组初始化
    int k = 0;                                                         //循环标志
    char reply[200];                                                   //数据传送的缓冲区
    int create = 0;                                                    //已经创建连接的计数，不受重连影响，因而100个连接中的某一个断开，则在他的套接字被删除后和重连前，creat是大于实际连接数的
    int quit = 0;                                                      //所有交互已完成标志
    int finished_num = 0;                                              //完成连接计数
    fd_set west;                                                       //读操作符集
    fd_set rest;                                                       //写操作符集
    struct timeval tv;                                                 //select等待时间 NULL阻塞
    long start_mt = longgetSystemTime();                               //开始时间，毫秒精度
    while (1)
    {

        my_new_connection(&create, &start_mt, client_SOCK, client); //监听到并建立新的连接
        if (quit)
            break;
        tv.tv_sec = 0;  //select等待秒
        tv.tv_usec = 0; //select等待毫秒
        FD_init(&rest, &west, client, client_SOCK);
        int select_ret = select(FD_SETSIZE, &rest, &west, NULL, &tv);
        if (select_ret == -1)
        {
            perror("select");
            exit(-1);
        }
        else
            for (k = 0; k < client.num; k++)
            {
                if (client_SOCK[k])
                {
                    memset(reply, 0, 200);
                    int read_return = my_read_SELECT(client_SOCK[k], &rest, reply);                                 //从服务器读
                    my_disconnect_SELECT(client, client_SOCK, &finished_num, &quit, &west, &rest, read_return, k);  //处理读的返回值
                    int write_return = my_write_SELECT(client_SOCK[k], &west, k);                                   //从服务器写
                    my_disconnect_SELECT(client, client_SOCK, &finished_num, &quit, &west, &rest, write_return, k); //处理写的返回值
                }
            }
    }
}
void poll_nonblock(struct client_conf client)
{
    SOCK **client_SOCK = (SOCK **)malloc(client.num * sizeof(SOCK *)); //客户端SOCK指针数组,最多1000个连接
    client_SOCK_init(client_SOCK, client);                             //客户端SOCK指针数组初始化
    int k = 0;                                                         //循环标志
    char reply[200];                                                   //数据传送的缓冲区
    int create = 0;                                                    //已经创建连接的计数，不受重连影响，因而100个连接中的某一个断开，则在他的套接字被删除后和重连前，creat是大于实际连接数的
    int quit = 0;                                                      //所有交互已完成标志
    int finished_num = 0;                                              //完成连接计数
    struct pollfd poll_fds[MAX_CON];                                   //poll结构体数组
    long start_mt = longgetSystemTime();                               //开始时间，毫秒精度
    while (1)
    {
        my_new_connection(&create, &start_mt, client_SOCK, client);
        if (quit)
            break;
        int now_use_pos = 0;
        POLLFD_init(poll_fds, client, client_SOCK, &now_use_pos);
        int nfds = MAX_CON; //当前需要监听的套接字数量
        int poll_return = poll(poll_fds, nfds, 0);
        if (poll_return < 0)
        {
            printf("poll error\n");
            exit(1);
        }
        else
        {
            for (k = 0; k < client.num; k++)
            {
                if (client_SOCK[k])
                {
                    memset(reply, 0, 200);
                    int pos = client_SOCK[k]->poll_pos;
                    int read_return = my_read_POLL(client_SOCK[k], poll_fds[pos], reply);
                    my_disconnect_POLL(client, client_SOCK, &finished_num, &quit, read_return, k);
                    int write_return = my_write_POLL(client_SOCK[k], poll_fds[pos], k);
                    my_disconnect_POLL(client, client_SOCK, &finished_num, &quit, write_return, k);
                }
            }
        }
    }
}
void epoll_nonblock(struct client_conf client)
{
    struct epoll_event epoll_fds[MAX_CON];                             //epoll结构体数组
    int efd;                                                           //epoll建立的文件描述符
    SOCK **client_SOCK = (SOCK **)malloc(client.num * sizeof(SOCK *)); //客户端SOCK指针数组,最多1000个连接
    client_SOCK_init(client_SOCK, client);                             //客户端SOCK指针数组初始化
    int k = 0;                                                         //循环标志
    char reply[200];                                                   //数据传送的缓冲区
    int create = 0;                                                    //已经创建连接的计数，不受重连影响，因而100个连接中的某一个断开，则在他的套接字被删除后和重连前，creat是大于实际连接数的
    int quit = 0;                                                      //所有交互已完成标志
    int finished_num = 0;                                              //完成连接计数
    long start_mt = longgetSystemTime();                               //开始时间，毫秒精度
    efd = epoll_create1(0);
    if (efd == -1)
    {
        perror("epoll_create");
        exit(0);
    }
    while (1)
    {
        my_new_connection_EPOLL(&create, &start_mt, client_SOCK, client, efd);
        //printf("1");
        if (quit)
            break;
        int epoll_return = epoll_wait(efd, epoll_fds, MAX_CON, 0);
        for (k = 0; k < client.num; k++)
        {
            if (client_SOCK[k]) //正在使用的套接字
            {
                memset(reply, 0, 200);
                int read_return = my_read_EPOLL(client_SOCK[k], epoll_fds, reply, epoll_return);
                my_disconnect_EPOLL(client, client_SOCK, &finished_num, &quit, read_return, k, efd);
                int write_return = my_write_EPOLL(client_SOCK[k], epoll_fds, k, epoll_return);
                my_disconnect_EPOLL(client, client_SOCK, &finished_num, &quit, write_return, k, efd);
            }
        }
    }
}

void fork_block(struct client_conf client)
{

    //SOCK *client_SOCK[FD_SETSIZE]; //客户端SOCK指针数组,最多1000个连接
    //client_SOCK_init(client_SOCK); //客户端SOCK指针数组初始化

    int finished_num = 0; //连接成功个数
    int connect_now = 0;  //连接个数
    int new_sock;

    char str[50];
    int status; //waitpid的状态判断
    //char buf[1024];
    while (1)
    {
        if (finished_num != client.num)
        {
            if ((new_sock = socket(PF_INET, SOCK_STREAM, 0)) < 0)
            {
                perror("socket");
                return;
            }
            int connect_return = connect(new_sock, (struct sockaddr *)&client.client_addr, sizeof(struct sockaddr));
            if (connect_return < 0)
            {
                perror("conncet");
                return;
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
                    printf("成功连接到server端，进行阻塞状态的信息收发,子进程pid为%d\n", getpid());
                    int func_return;
                    while (1)
                    {
                        memset(str, '\0', 50);
                        func_return = my_read_noblock(fd, str);
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

                        func_return = my_write_fork(fd);
                        //printf("%d", fd->state.step);
                        if (func_return == QUIT_ERROR)
                        {
                            /*当出现错误时，调用kill -9关闭子进程*/
                            printf("write过程出现错误，使用kill -9 杀死子进程\n");
                            char s[20];
                            sprintf(s, "kill -9 %d", getpid());
                            system(s);
                        }
                        if (func_return == MISSION_COMPLETE)
                        {
                            printf("1\n");
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
        else
            break;
    }
    return;
}
void fork_nonblock(struct client_conf client)
{

    //SOCK *client_SOCK[FD_SETSIZE]; //客户端SOCK指针数组,最多1000个连接
    //client_SOCK_init(client_SOCK); //客户端SOCK指针数组初始化

    int finished_num = 0; //连接成功个数
    int connect_now = 0;  //连接个数
    int new_sock;

    char str[50];
    int status; //waitpid的状态判断
    //char buf[1024];
    while (1)
    {
        if (finished_num != client.num)
        {
            if ((new_sock = socket(PF_INET, SOCK_STREAM, 0)) < 0)
            {
                perror("socket");
                return;
            }
            int connect_return = connect(new_sock, (struct sockaddr *)&client.client_addr, sizeof(struct sockaddr));
            if (connect_return < 0)
            {
                perror("conncet");
                if (errno == EWOULDBLOCK)
                    continue;
                return;
            }
            else
            {

                int id;
                int flags = fcntl(new_sock, F_GETFL, 0);
                fcntl(new_sock, F_SETFL, flags | O_NONBLOCK);
                //设置非阻塞
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
                    printf("成功连接到server端，进行非阻塞状态的信息收发,子进程pid为%d\n", getpid());
                    int func_return;
                    while (1)
                    {
                        memset(str, '\0', 50);
                        func_return = my_read_noblock(fd, str);
                        if (func_return == UNABLE_RW)
                            continue;
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

                        func_return = my_write_fork(fd);
                        //printf("%d", fd->state.step);
                        if (func_return == QUIT_ERROR)
                        {
                            /*当出现错误时，调用kill -9关闭子进程*/
                            printf("write过程出现错误，使用kill -9 杀死子进程\n");
                            char s[20];
                            sprintf(s, "kill -9 %d", getpid());
                            system(s);
                        }
                        if (func_return == MISSION_COMPLETE)
                        {
                            //printf("1\n");
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
        else
            break;
    }
    return;
}
int main(int argc, char *argv[])
{

    /*客户端基本变量*/
    struct client_conf client;             //client数据结构
    memset(&client, 0, sizeof(client));    //client端配置信息清零
    client_conf_argv(argc, argv, &client); //根据main函数参数配置client
    print_client_conf(client);             //显示client配置信息S

    if (!client.fork)
    {
        if (client.select == SELECT)
            select_nonblock(client);
        else if (client.select == POLL)
            poll_nonblock(client);
        else if (client.select == EPOLL)
            epoll_nonblock(client);
    }
    else
    {
        if (client.block)
            fork_block(client);
        else
            fork_nonblock(client);
    }

    return 0;
}