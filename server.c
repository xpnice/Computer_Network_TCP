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

#define DEBUG_MODE 0     //Ϊ1ʱserver����WRONG_PERCENT���ʹ��ⷢ�����client������
#define WRONG_PERCENT 25 //����ĸ���1~100,25��ʾ�ٷ�֮��ʮ��

char message[6][10] = {"", "StuNo", "pid", "TIME", "str00000", "end"};
typedef struct
{
    int step; //1-5��Ӧ5���շ�״̬,1Ϊ��ʼ����ֵ
    int flag; //1����û��ִ�У�0����ִ����ɣ���ʼΪ1
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
    int n_str;    //�յ���str����
    int poll_pos; //���׽�����poll_fds�ṹ�������е��±�
} SOCK;

int my_write_tofile(SOCK *fd)
{
    char name[40];
    sprintf(name, "./txt/%d.%d.pid.txt", fd->stuno, fd->pid);
    if (access("./txt/", 0) == -1)                             //�������ļ���
        mkdir("./txt", S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH); //�½��ļ���
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

//��my_read_noblock������ʹ�õ�read�׽��ֺ�Ĵ����ж�
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
            printf("ss%d[%d] �����˳�\n", fd->socket, fd->pos);
            //ʵ��д���ļ��Ĳ���
            if (my_write_tofile(fd))
                printf("ss%d�ɹ�д���ļ�\n", fd->socket);
            else
                printf("ss%dд���ļ�ʧ��\n", fd->socket);
            (*connect_num)++; //�������ɹ��˳���client������
            return MISSION_COMPLETE;
        }
        else
        {
            printf("ss%d �쳣�˳�\n", fd->socket);
            return QUIT_ERROR;
            /*to do  ʵ��client���׽����˳����ά������*/
        }
    }
    if (errno == EWOULDBLOCK)
        return UNABLE_RW;
}

//Ѱ��һ���ļ��������Ƿ���epoll_wait���ص�events�����У��ɶ�
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

//Ѱ��һ���ļ��������Ƿ���epoll_wait���ص�events�����У���д
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
    �����ߺ���:�������ݿɶ�ʱ���ã�Ŀǰ���Թ�������select/poll/epoll
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

        if (n == 4) //�ж��Ƿ���յ����ֽ�
        {
            //����socket�ṹ��
            /*TODO*/
            unsigned int client_num = ntohl(*((int *)buf));
            printf("�յ�ss%d ���͵�ѧ��%d\n", fd->socket, client_num);
            fd->stuno = client_num;
            fd->state.step++;
            fd->state.flag = 1;
        }
        else
        {
            if (ret == UNABLE_RW)
                return UNABLE_RW;
            //�յ��������ݣ��ж�����
            unsigned int client_num = ntohl(*((int *)buf));
            printf("�յ�����ѧ��%d���ж���ss%d ������\n", client_num, fd->socket);
            /*to do  ʵ��client���׽����˳����ά������*/
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

        if (n == 4) //�ж��Ƿ���յ����ֽ�
        {
            //����socket�ṹ��
            unsigned int pid_num = ntohl(*((int *)buf));
            printf("�յ�ss%d ���͵Ľ��̺� %d\n", fd->socket, pid_num);
            fd->pid = pid_num;
            fd->state.step++;
            fd->state.flag = 1;
        }
        else
        {
            if (ret == UNABLE_RW)
                return UNABLE_RW;
            //�յ��������ݣ��ж�����
            printf("�յ�������̺ţ��ж���ss%d ������\n", fd->socket);
            /*to do  ʵ��client���׽����˳����ά������*/
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

        if (n != 19) //�ж��Ƿ���յ�19�ֽ�
        {
            if (ret == UNABLE_RW)
                return UNABLE_RW;
            //�յ��������ݣ��ж�����
            printf("�յ�����ʱ������ж���ss%d ������\n", fd->socket);
            /*to do  ʵ��client���׽����˳����ά������*/
            return QUIT_ERROR;
        }
        printf("�յ�ss%d ���͵�ʱ��� %s\n", fd->socket, fd->time);
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

        //�յ��������ݣ��ж�����
        //printf("�յ�ss%d�����ַ���%d\n", fd->socket, n);
        if (n > 0)
            fd->n_str += n;
        /*to do  ʵ��client���׽����˳����ά������*/
        if (fd->n_str == fd->lenStr)
        {
            printf("�յ�ss%d���͵���ȷ�����ַ���\n", fd->socket);
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
            printf("ss%d ����������δ�˳�\n", fd->socket);
            //�ж���client������
            /*to do  ʵ��client���׽����˳����ά������*/
            return QUIT_ERROR;
        }
        if (ret == UNABLE_RW)
            return UNABLE_RW;
        return n;
    }
    return 0;
}

/*
    POLL��ʽ���ã�server��ĳ��client���͵�����
*/
int my_read_POLL(SOCK *fd, struct pollfd poll_fd, int *connect_num)
{
    if (poll_fd.revents & POLLIN)
    {
        //printf("������ִ��my_read_noblock\n");
        return my_read_function(fd, connect_num);
    }

    return UNABLE_RW;
}
/*
    EPOLL��ʽ���ã�server��ĳ��client���͵�����
*/
int my_read_EPOLL(SOCK *fd, struct epoll_event events[], int func_ret, int *connect_num)
{
    if (IsInEvents_Readable(fd->socket, events, func_ret))
        return my_read_function(fd, connect_num);
    return UNABLE_RW;
}
/*
    SELECT��ʽ���ã�server��ĳ��client���͵�����
*/
int my_read_SELECT(SOCK *fd, fd_set *my_read, int *connect_num)
{
    if (FD_ISSET(fd->socket, my_read))
        return my_read_function(fd, connect_num);
    return UNABLE_RW;
}

/*
    д���ߺ���:�������ݿ�дʱ���ã�Ŀǰ���Թ�������select/poll/epoll
*/
int my_write_function(SOCK *fd)
{
    //printf("����my_write_function\n");
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
        printf("ss%d��client�˳ɹ�������%s\n", fd->socket, message[fd->state.step]);
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
        printf("ss%d��client�˳ɹ�������%s\n", fd->socket, message[fd->state.step]);
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
                    n = write(fd->socket, "END", length); //ģ�����
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
        printf("ss%d��client�˳ɹ�������%s\n", fd->socket, message[fd->state.step]);
        fd->state.flag = 0;
        return n;
    }
    return 0;
}

/*
    POLL��ʽ���ã�serverдĳ��client���͵�����
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
    EPOLL��ʽ���ã�serverдĳ��client���͵�����
*/
int my_write_EPOLL(SOCK *fd, struct epoll_event events[], int func_ret)
{
    if (IsInEvents_Writeable(fd->socket, events, func_ret))
        return my_write_function(fd);
    return UNABLE_RW;
}
/*
    SELECT��ʽ���ã�serverдĳ��client���͵�����
*/
int my_write_SELECT(SOCK *fd, fd_set *my_write)
{

    if (FD_ISSET(fd->socket, my_write))
        return my_write_function(fd);
    return UNABLE_RW;
}

int init_SOCK(SOCK *fd, int socket_fd)
{
    srand((unsigned)time(NULL)); //���������
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
            //printf("���� client socket %d �ṹ��ɹ�\n", new_SOCK->socket);
            return new_SOCK;
        }
    }
    else
    {
        //printf("���󣺽��� client socket %d �ṹ��ʧ��\n", socket_fd);
        return NULL;
    }
    return NULL;
}

typedef struct
{
    int data[MAXSIZE];
    int front; //����ͷ
    int rear;  //����β
    int size;  //���д�С
} Queue;
void QPush(Queue *q, int item)
{
    if (q->size == MAXSIZE)
    {
        printf("��������\n");
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
        printf("�ռ䲻��\n");
        return NULL;
    }
    q->front = -1;
    q->rear = -1;
    q->size = 0;
    int i;
    for (i = 0; i < FD_SETSIZE; i++)
    {
        QPush(q, i); //���׽�������ȫ���±��������׽��ֶ���
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
        printf("����Ϊ��\n");
        return -1; //��ʱ����-1
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
    //���Լ��Ƿ���IP�����Լ���ͻ���� ����û�м��ip��port��һ�����������������Ƿ�Ϸ�
    int i;
    char block = 0;
    char fork = 0;
    char select = 0;
    char ip = 0;
    char po = 0;
    //�ظ�������־
    if (argc <= 2)
    {
        printf("�����������\n");
        exit(-1);
    }
    server->server_addr.sin_family = AF_INET;                //����ΪIPͨ��
    server->server_addr.sin_addr.s_addr = htons(INADDR_ANY); //������IP��ַ--�������ӵ����б��ص�ַ��
    for (i = 1; i < argc; i++)
    {
        if (0 == strcmp("--ip", argv[i]))
        {
            if (1 == ip)
            {
                printf("����ip�����ͻ\n");
                exit(-1);
            }
            ip = 1;
            int temp = inet_addr(argv[++i]);
            if (temp == -1)
            {
                printf("IP��ַ��ʽ�������\n");
                exit(-1);
            }
            server->server_addr.sin_addr.s_addr = temp; //������IP��ַ
            continue;
        }
        if (0 == strcmp("--port", argv[i]))
        {
            if (1 == po)
            {
                printf("����port�����ͻ\n");
                exit(-1);
            }
            po = 1;
            int port = atoi(argv[++i]);
            server->server_addr.sin_port = htons(port); //�������˿ں�
            continue;
        }
        if (0 == strcmp("--block", argv[i]))
        {
            if (1 == block)
            {
                printf("����block�����ͻ\n");
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
                printf("����block�����ͻ\n");
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
                printf("����fork�����ͻ\n");
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
                printf("����fork�����ͻ\n");
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
                printf("����select�����ͻ\n");
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
                printf("����select�����ͻ\n");
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
                printf("����select�����ͻ\n");
                exit(-1);
            }
            select = 1;
            server->select = 2;
            continue;
        }
        printf("��������Ч����%s\n", argv[i]);
        exit(-1);
    }
    if (0 == server->fork) //nofork��blockͬʱ����block��Ч
        server->block = 0;
    else
        server->select = -1;
}
void print_server_conf(struct server_conf server)
{
    
    system("rm -rf txt");
    printf("---------------------------------------\n");
    printf("��������ǰɾ����ǰĿ¼�µ�txt�ļ�\n");
    printf("������������Ϣ��\n");
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
/*���ߺ���*/
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
        QPush(Q, pos); //�����м�������±�
        (*connect_now)--;
        if (QUIT_ERROR == ret)
            printf("�˳�����\n");
        printf("��ǰ������:%d\n��ǰ���client�ļ���:%d\n", *connect_now, connect_num);
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
        QPush(Q, pos); //�����м�������±�
        (*connect_now)--;
        if (QUIT_ERROR == ret)
            printf("�˳�����\n");
        printf("��ǰ������:%d\n��ǰ���client�ļ���:%d\n", *connect_now, connect_num);
        return BREAK;
    }
    return CONTINUE;
}

/*���ߺ���*/
int my_new_connection(int server_sockfd, int *connect_now, Queue *Q, SOCK **client_SOCK, struct sockaddr_in client)
{
    int address_size = sizeof(struct sockaddr_in); //sockaddr_in�ṹ��size
    printf("---------------------------------------\n");
    printf("��⵽�¿ͻ�����������\n");
    int new_sockfd_pos = QPop(Q); //��ȡ�½��׽������׽���ά�������е��±�

    int new_socket = accept(server_sockfd, (struct sockaddr *)&client, &address_size);
    if (new_socket == -1)
    {
        perror("call to accept");
        exit(1);
    }
    client_SOCK[new_sockfd_pos] = build_SOCK(new_socket);

    no_block(client_SOCK[new_sockfd_pos]->socket); //�����׽������÷�����ģʽ
    printf("---------------------------------------\n");
    printf("�ͻ������ӳɹ���\nCLIENT_IP:%s\nCLIENT_PORT:%d\n",
           inet_ntoa(client.sin_addr),
           ntohs(client.sin_port));
    printf("�׽������:%d   ��������:%d\n", client_SOCK[new_sockfd_pos]->socket, ++*(connect_now));
    printf("new socket pos %d\n", new_sockfd_pos);
    return CONNECTION_SUCCESS;
}

int my_new_connection_SELECT(int server_sockfd, fd_set *rest, int *connect_now, Queue *Q, SOCK **client_SOCK, struct sockaddr_in client)
{
    if (FD_ISSET(server_sockfd, rest) && ((*connect_now) <= MAXCONNECTION)) //�¿ͻ�������
        return my_new_connection(server_sockfd, connect_now, Q, client_SOCK, client);
    else
        return CONNECTION_AWAIT;
}

int my_new_connection_POLL(int server_sockfd, struct pollfd poll_fd[], int *connect_now, Queue *Q, SOCK **client_SOCK, struct sockaddr_in client, int *now_use_pos)
{
    if ((poll_fd[0].revents & POLLIN) && ((*connect_now) <= MAXCONNECTION)) //�¿ͻ�������
    {
        //��ʼ���½������ӵ�pollfd�ṹ��
        int address_size = sizeof(struct sockaddr_in); //sockaddr_in�ṹ��size
        printf("---------------------------------------\n");
        printf("��⵽�¿ͻ�����������\n");
        int new_sockfd_pos = QPop(Q); //��ȡ�½��׽������׽���ά�������е��±�
        int new_socket = accept(server_sockfd, (struct sockaddr *)&client, &address_size);
        if (new_socket == -1)
        {
            perror("call to accept");
            exit(1);
        }
        client_SOCK[new_sockfd_pos] = build_SOCK(new_socket);
        no_block(client_SOCK[new_sockfd_pos]->socket); //�����׽������÷�����ģʽ
        printf("---------------------------------------\n");
        printf("�ͻ������ӳɹ���\nCLIENT_IP:%s\nCLIENT_PORT:%d\n",
               inet_ntoa(client.sin_addr),
               ntohs(client.sin_port));
        printf("�׽������:%d   ��������:%d\n", client_SOCK[new_sockfd_pos]->socket, ++*(connect_now));
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
    if (IsInEvents_Readable(server_sockfd, epoll_fds, func_ret) && ((*connect_now) <= MAXCONNECTION)) //�¿ͻ�������
    //if ((*connect_now) <= MAXCONNECTION)
    {

        int address_size = sizeof(struct sockaddr_in); //sockaddr_in�ṹ��size
        printf("---------------------------------------\n");
        printf("��⵽�¿ͻ�����������\n");
        int new_sockfd_pos = QPop(Q); //��ȡ�½��׽������׽���ά�������е��±�
        int new_socket = accept(server_sockfd, (struct sockaddr *)&client, &address_size);
        if (new_socket == -1)
        {
            perror("call to accept");
            exit(1);
        }
        client_SOCK[new_sockfd_pos] = build_SOCK(new_socket);
        no_block(client_SOCK[new_sockfd_pos]->socket); //�����׽������÷�����ģʽ
        printf("---------------------------------------\n");
        printf("�ͻ������ӳɹ���\nCLIENT_IP:%s\nCLIENT_PORT:%d\n",
               inet_ntoa(client.sin_addr),
               ntohs(client.sin_port));
        printf("�׽������:%d   ��������:%d\n", client_SOCK[new_sockfd_pos]->socket, ++*(connect_now));

        //�������
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
    int reuse0 = 1;                                   //�˿ڸ���
    *server_sockfd = socket(AF_INET, SOCK_STREAM, 0); //IPV4 TCPЭ��

    if (*server_sockfd == -1) //����ʧ��
    {
        perror("call to socket");
        exit(1);
    }

    if (server.block == 0)
        no_block(*server_sockfd); //nonblock���÷�����

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

    if (listen(*server_sockfd, MAXLOG) == -1) //server_sockfd�����׽��֣��������г���MAXLOG(1024)
    {
        perror("call to listen");
        exit(1);
    }
}

void FD_init(fd_set *rest, fd_set *west, int server_sockfd, SOCK **client_SOCK)
{
    FD_ZERO(rest);               //��ն�������
    FD_ZERO(west);               //���д������
    FD_SET(server_sockfd, rest); //�Ѽ����׽��ַ����������
    FD_SET(server_sockfd, west); //�Ѽ����׽��ַ���д������
    int i;
    for (i = 0; i < FD_SETSIZE; i++) //������ʹ���׽�������
    {
        if (client_SOCK[i] != NULL) //����׽������ڱ�ʹ��
        {
            FD_SET(client_SOCK[i]->socket, rest); //�����׽��ּ����������
            FD_SET(client_SOCK[i]->socket, west); //�����׽��ּ���д������
        }
    }
}

void POLLFD_init(struct pollfd poll_fds[], int server_sockfd, SOCK **client_SOCK, int *use_now)
{
    memset(poll_fds, 0, sizeof(struct pollfd) * MAXCONNECTION); //�������
    poll_fds[0].fd = server_sockfd;                             //�Ѽ����׽��ַ�������
    poll_fds[0].events = POLLIN;                                //���ù�ע���¼�
    (*use_now)++;
    int i;
    for (i = 0; i < MAXCONNECTION; i++) //������ʹ���׽�������
    {
        if (client_SOCK[i] != NULL) //����׽������ڱ�ʹ��
        {
            poll_fds[*use_now].fd = client_SOCK[i]->socket; //�����׽��ּ�������
            poll_fds[*use_now].events = POLLIN | POLLOUT;   //���ù�ע��/д�¼�
            client_SOCK[i]->poll_pos = *use_now;
            (*use_now)++;
        }
    }
}

int EPOLLFD_init(int efd, int server_sockfd, SOCK **client_SOCK)
{
    struct epoll_event epoll_temp;

    epoll_temp.data.fd = server_sockfd; //����socket
    epoll_temp.events = EPOLLIN;
    //������socket����
    int epoll_ret = epoll_ctl(efd, EPOLL_CTL_ADD, server_sockfd, &epoll_temp);
    if (epoll_ret == -1)
    {
        perror("epoll_ctl");
        return -1;
    }
}

void wait2connect(time_t *start_time, int connect_now)
{
    if (0 == connect_now) //��û�пͻ�������ʱ
    {
        if (time(NULL) - *start_time >= 5) //ÿ�������һ��
        {
            *start_time = time(NULL);
            printf("���ڵȴ��ͻ������ӡ�������\n");
        }
    }
}

void select_nonblock(int server_sockfd)
{
    /*�ͻ��˻�������*/
    SOCK *client_SOCK[FD_SETSIZE]; //�ͻ���SOCKָ������,���1000������
    client_SOCK_init(client_SOCK); //�ͻ���SOCKָ�������ʼ��
    struct sockaddr_in client;     //client�������ַ�ṹ��

    /*��������*/
    int i = 0;                          //ѭ����־
    int func_return;                    //read write��������ֵ
    time_t start_time = time(NULL) - 5; //��ʼʱ��

    /*ά������*/
    Queue *Q;             //�����׽����±����
    Q = CreateQueue();    //��¼�׽�������sockfd_inuse�����п��õĽǱ�
    int finished_num = 0; //���ӳɹ�����
    int connect_now = 0;  //���Ӹ���

    /*select����*/
    fd_set rest, west;      //��д������
    struct timeval tempval; //select�ȴ�ʱ��
    tempval.tv_sec = 0;     //select�ȴ�����
    tempval.tv_usec = 0;    //select�ȴ�������

    while (1)
    {
        FD_init(&rest, &west, server_sockfd, client_SOCK);
        wait2connect(&start_time, connect_now);
        int select_return = select(FD_SETSIZE, &rest, &west, NULL, &tempval); //�����׽ӵĿɶ��Ϳ�д����
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
    /*�ͻ��˻�������*/
    SOCK *client_SOCK[FD_SETSIZE]; //�ͻ���SOCKָ������,���1000������
    client_SOCK_init(client_SOCK); //�ͻ���SOCKָ�������ʼ��
    struct sockaddr_in client;     //client�������ַ�ṹ��

    /*��������*/
    int i = 0;                          //ѭ����־
    int func_return;                    //read write��������ֵ
    time_t start_time = time(NULL) - 5; //��ʼʱ��

    /*ά������*/
    Queue *Q;             //�����׽����±����
    Q = CreateQueue();    //��¼�׽�������sockfd_inuse�����п��õĽǱ�
    int finished_num = 0; //���ӳɹ�����
    int connect_now = 0;  //���Ӹ���

    /*poll�Ľṹ������*/
    struct pollfd poll_fds[MAXCONNECTION];

    while (1)
    {

        int now_use_pos = 0;
        POLLFD_init(poll_fds, server_sockfd, client_SOCK, &now_use_pos);
        wait2connect(&start_time, connect_now);
        int nfds = connect_now + 1; //��ǰ��Ҫ�������׽�������
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
                    //printf("����poll_pos%d\n",pos);
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
    SOCK *client_SOCK[FD_SETSIZE]; //�ͻ���SOCKָ������,���1000������
    client_SOCK_init(client_SOCK); //�ͻ���SOCKָ�������ʼ��
    struct sockaddr_in client;     //client�������ַ�ṹ��

    /*��������*/
    int i = 0;                          //ѭ����־
    int func_return;                    //read write��������ֵ
    time_t start_time = time(NULL) - 5; //��ʼʱ��

    /*ά������*/
    Queue *Q;             //�����׽����±����
    Q = CreateQueue();    //��¼�׽�������sockfd_inuse�����п��õĽǱ�
    int finished_num = 0; //���ӳɹ�����
    int connect_now = 0;  //���Ӹ���

    /*epoll����*/
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
            if (client_SOCK[i]) //����ʹ�õ��׽���
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

    //SOCK *client_SOCK[FD_SETSIZE]; //�ͻ���SOCKָ������,���1000������
    //client_SOCK_init(client_SOCK); //�ͻ���SOCKָ�������ʼ��
    struct sockaddr_in client; //client�������ַ�ṹ��
    socklen_t len = sizeof(client);
    int pid = getpid();

    int finished_num = 0; //���ӳɹ�����
    int connect_now = 0;  //���Ӹ���

    int status; //waitpid��״̬�ж�
    char buf[1024];
    while (1)
    {
        printf("����״̬�ȴ�����\n");
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
                printf("�ɹ����ӵ�client�ˣ���������״̬����Ϣ�շ�,�ӽ���pidΪ%d\n", getpid());
                int func_return;
                while (1)
                {

                    func_return = my_write_function(fd);
                    printf("%d", func_return);
                    fflush(stdout);
                    //printf("%d", fd->state.step);
                    if (func_return == QUIT_ERROR)
                    {
                        /*�����ִ���ʱ������kill -9�ر��ӽ���*/
                        printf("write���̳��ִ���ʹ��kill -9 ɱ���ӽ���\n");
                        char s[20];
                        sprintf(s, "kill -9 %d", getpid());
                        system(s);
                    }

                    func_return = my_read_function(fd, &finished_num);

                    if (func_return == QUIT_ERROR)
                    {
                        /*�����ִ���ʱ������kill -9�ر��ӽ���*/
                        close(fd->socket);
                        free(fd->str);
                        free(fd);
                        printf("read���̳��ִ���ʹ��kill -9 ɱ���ӽ���\n");
                        char s[20];
                        sprintf(s, "kill -9 %d", getpid());
                        system(s);
                    }
                    if (func_return == MISSION_COMPLETE)
                    {
                        /*����ɽ�������󣬵���kill -7�ر��ӽ���*/
                        close(fd->socket);
                        free(fd->str);
                        free(fd);
                        printf("�����Ϣ����������kill -7 �����ӽ���\n");
                        char s[20];
                        sprintf(s, "kill -7 %d", getpid());
                        system(s);
                    }
                }
            }
            else if (id > 0)
            {
                //����һ���µ�����
                connect_now++;
                //�����ж��ӽ����Ƿ���ȫ�˳�
                waitpid(-1, &status, 0);
                //�ж��ӽ����˳�״̬
                if (WIFSIGNALED(status))
                {
                    //�ӽ��̴����˳����޶���ִ��
                    if (WTERMSIG(status) == 9)
                    {
                        ;
                    }
                    //�ӽ��������˳���connect_now--
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
    no_block(listen_sock);     //����Ϊ������
    struct sockaddr_in client; //client�������ַ�ṹ��
    socklen_t len = sizeof(client);
    int pid = getpid();

    int finished_num = 0; //���ӳɹ�����
    int connect_now = 0;  //���Ӹ���

    int status; //waitpid��״̬�ж�
    char buf[1024];
    while (1)
    {
        //printf("����״̬�ȴ�����\n");
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
            no_block(new_sock); //�׽�������ɹ����÷�����
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
                printf("�ɹ����ӵ�client�ˣ���������״̬����Ϣ�շ�,�ӽ���pidΪ%d\n", getpid());
                int func_return;
                while (1)
                {
                    func_return = my_write_function(fd);
                    // printf("%d", func_return);
                    //fflush(stdout);
                    //printf("%d", fd->state.step);
                    if (func_return == QUIT_ERROR)
                    {
                        /*�����ִ���ʱ������kill -9�ر��ӽ���*/
                        printf("write���̳��ִ���ʹ��kill -9 ɱ���ӽ���\n");
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
                        /*�����ִ���ʱ������kill -9�ر��ӽ���*/
                        close(fd->socket);
                        free(fd->str);
                        free(fd);
                        printf("read���̳��ִ���ʹ��kill -9 ɱ���ӽ���\n");
                        char s[20];
                        sprintf(s, "kill -9 %d", getpid());
                        system(s);
                    }
                    if (MISSION_COMPLETE == func_return)
                    {
                        /*����ɽ�������󣬵���kill -7�ر��ӽ���*/
                        close(fd->socket);
                        free(fd->str);
                        free(fd);
                        printf("�����Ϣ����������kill -7 �����ӽ���\n");
                        char s[20];
                        sprintf(s, "kill -7 %d", getpid());
                        system(s);
                    }
                }
            }
            else if (id > 0)
            {
                //����һ���µ�����
                connect_now++;
                //�����ж��ӽ����Ƿ���ȫ�˳�
                waitpid(-1, &status, 0);
                //�ж��ӽ����˳�״̬
                if (WIFSIGNALED(status))
                {
                    //�ӽ��̴����˳����޶���ִ��
                    if (WTERMSIG(status) == 9)
                    {
                        ;
                    }
                    //�ӽ��������˳���connect_now--
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

    /*����������*/
    struct server_conf server;          //server���ݽṹSS
    int server_sockfd;                  //�����׽���
    memset(&server, 0, sizeof(server)); //server��������Ϣ����

    /*����������*/
    server_conf_argv(argc, argv, &server);   //����main������������server
    print_server_conf(server);               //��ʾserver������Ϣ
    server_tcp_init(server, &server_sockfd); //socket+nonblock?+reuse+bind+listen

    /*��client�˴���*/
    if (!server.fork)
    {
        if (server.select == SELECT)
            select_nonblock(server_sockfd); //select������ģʽ
        else if (server.select == POLL)
            poll_nonblock(server_sockfd); //poll������ģʽ
        else if (server.select == EPOLL)
            epoll_nonblock(server_sockfd); //epoll������ģʽ
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
