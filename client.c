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
    int step; //0-4��Ӧ5���շ�״̬
    int flag; //1�������д�룬0������д��
} STATE;
typedef struct
{
    int socket;         //�׽���
    int lenStr;         //Ӧ�ô����ַ�������
    unsigned int stuno; //ѧ��
    unsigned int pid;   //pid
    char time[20];      //ʱ���
    char *str;          //�ַ���
    STATE state;        //�շ�״̬
    int poll_pos;       //����poll��ʽ��λ���������������е�λ��
} SOCK;

char message[5][10] = {"StuNo", "pid", "TIME", "str00000", "end"};

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

int my_read_noblock(SOCK *fd, char *str)
{
    //  if (fd->socket < 0)
    //   return -2;
    int o = 0;
    if ((fd->state.step + 1) == TIME || (fd->state.step + 1) == STR)
        o = 1;

    int n = read(fd->socket, str, 50);
    printf("%s\n", str);
    if (n == -1)
    {
        perror("read error");
        return QUIT_ERROR;
    }
    if (n == 0)
    {
        perror("server�˶Ͽ�����\n");
        /**/
        return QUIT_ERROR;
    }
    if (n != (strlen(message[fd->state.step + 1]) + o))
    {
        printf("server�˴������ݳ��ȴ���,Ӧ����%d,ʵ�ʴ���%d", strlen(message[fd->state.step + 1]), n);
        return QUIT_ERROR;
    }
    printf("�׽���ss%d[%d]�յ���%s\n", fd->socket, fd->state.step, str);

    int check_return = chech_read(str, fd);
    return check_return;
}
//��������Ƿ���ȷ����ȷ��step++,�ٽ�flag��Ϊ1��������һ�ε�д����
//��Ϊ���м�����д����step,���Զ���read�Լ�write�е�step�����1
int chech_read(char *str, SOCK *fd)
{
    if ((fd->state.step + 1) != STR)
    {
        //�����str����
        if (strcmp(str, message[fd->state.step + 1]) == 0)
        {
            fd->state.step++;
            fd->state.flag = 1;
            // printf("%s\n", str);
            printf("�׽���ss%d��״̬%dת��Ϊ״̬%d\n", fd->socket, fd->state.step - 1, fd->state.step);
        }
        else
        {
            return QUIT_ERROR;
        }
    }
    else
    {
        //printf("%d\n", strncmp(str, message[fd->state.step + 1], 3));
        //����str����
        if ((strncmp(str, message[fd->state.step + 1], 3) == 0) && (strlen(str) == 8))
        {

            fd->lenStr = atoi(&str[3]);
            fd->state.step++;
            fd->state.flag = 1;
            // printf("%s\n", str);
            printf("�׽���ss%d��ǰ״̬:%d\n", fd->socket, fd->state.step);
        }
        else
        {
            return QUIT_ERROR;
        }
    }
}

/*
    POLL��ʽ���ã�server��ĳ��client���͵�����
*/
int my_read_POLL(SOCK *fd, struct pollfd poll_fd, char *str)
{
    if (poll_fd.revents & POLLIN)
        return my_read_noblock(fd, str);
    return UNABLE_RW;
}
/*
    EPOLL��ʽ���ã�server��ĳ��client���͵�����
*/
int my_read_EPOLL(SOCK *fd, struct epoll_event events[], char *str, int func_ret)
{
    if (IsInEvents_Readable(fd->socket, events, func_ret))
        return my_read_noblock(fd, str);
    return UNABLE_RW;
}
/*
    SELECT��ʽ���ã�server��ĳ��client���͵�����
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
            printf("%d��serverд��%d\n", fd->socket, SNO);
            return n;
        }
        if (fd->state.step == PID)
        {
            if (0)
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
                printf("%d��serverд��%d\n", fd->socket, getpid());
                return n;
            }
            else
            {
                //unsigned int num = htonl((getpid() << 16) + fd->socket);
                unsigned int num = htonl((getpid() << 16) + pos + 3);
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
            printf("ss%d��serverд��ʱ���%s\n", fd->socket, s);
            return n;
        }
        if (fd->state.step == STR)
        {
            // printf("%d��ʼ��serverд���ַ���\n", fd->socket);
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
                printf("ss%d��serverд���ַ���\n", fd->socket);
            else
                return QUIT_ERROR;
            printf("str����ֵ%d,Ӧ�÷���ֵ%d\n", n, fd->lenStr);
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
    POLL��ʽ���ã�serverдĳ��client���͵�����
*/
int my_write_POLL(SOCK *fd, struct pollfd poll_fd, int pos)
{
    if (poll_fd.revents & POLLOUT)
        return my_write_noblock(fd, pos);

    return UNABLE_RW;
}
/*
    EPOLL��ʽ���ã�serverдĳ��client���͵�����
*/
int my_write_EPOLL(SOCK *fd, struct epoll_event events[], int pos, int func_ret)
{
    if (IsInEvents_Writeable(fd->socket, events, func_ret))
        return my_write_noblock(fd, pos);
    return UNABLE_RW;
}
/*
    SELECT��ʽ���ã�serverдĳ��client���͵�����
*/
int my_write_SELECT(SOCK *fd, fd_set *my_write, int pos)
{
    if (FD_ISSET(fd->socket, my_write))
        return my_write_noblock(fd, pos);
    return UNABLE_RW;
}

int my_write_tofile(SOCK *fd)
{
    char name[30];
    sprintf(name, "%d.%d.pid.txt", fd->stuno, fd->pid);
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

    return 0;
}

int init_SOCK(SOCK *fd, int socket_fd) //��ʼ���ṹ��
{
    fd->state.flag = 0;
    fd->state.step = -1;
    fd->socket = socket_fd;
    fd->stuno = 0;
    fd->pid = 0;
    fd->lenStr = 0;
    fd->str = NULL;
    return 1;
}

struct client_conf
{
    struct sockaddr_in client_addr; //client IP(ȱʡȫ��) & Port
    int block;                      //ȱʡ0nonblock 1 block
    int fork;                       //ȱʡ0nofork   1 fork
    int select;                     //ȱʡ0select 1 poll 2 epoll -1 null
    int num;                        //ȱʡ100
};
void client_conf_argv(int argc, char *argv[], struct client_conf *client)
{
    //���Լ��Ƿ���IP ����û�м��ip��port��һ�����������������Ƿ�Ϸ�
    int i;
    if (argc <= 2)
    {
        printf("�����������\n");
        exit(-1);
    }
    client->client_addr.sin_family = AF_INET;                //����ΪIPͨ��
    client->client_addr.sin_addr.s_addr = htons(INADDR_ANY); //������IP��ַ--�������ӵ����б��ص�ַ��
    client->num = 100;
    for (i = 1; i < argc; i++)
    {
        if (0 == strcmp("--ip", argv[i]))
        {
            int temp = inet_addr(argv[++i]);
            if (temp == -1)
            {
                printf("IP��ַ��ʽ�������\n");
                exit(-1);
            }
            client->client_addr.sin_addr.s_addr = temp; //������IP��ַ
            continue;
        }
        if (0 == strcmp("--port", argv[i]))
        {
            int port = atoi(argv[++i]);
            client->client_addr.sin_port = htons(port); //�������˿ں�
            continue;
        }
        if (0 == strcmp("--num", argv[i]))
        {
            int num = atoi(argv[++i]);
            if (num < 1 || num > 1000)
            {
                printf("���������Ϸ�\n");
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

        printf("��������Ч����%s\n", argv[i]);
        exit(-1);
    }
    if (0 == client->fork) //nofork��blockͬʱ����block��Ч
        client->block = 0;
    else
        client->select = -1;
}
void print_client_conf(struct client_conf client)
{
    printf("------------------------------\n");
    printf("client��������Ϣ��\n");
    printf("�ͻ���IP:%s\n�ͻ���PORT:%d\n", inet_ntoa(client.client_addr.sin_addr), ntohs(client.client_addr.sin_port));
    printf(client.block ? "block\n" : "nonblock\n");
    printf(client.fork ? "fork\n" : "nofork\n");
    printf("������:%d\n", client.num);
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
    struct timeval tempval; //select�ȴ�ʱ��
    tempval.tv_sec = 1;     //select�ȴ�����
    tempval.tv_usec = 0;    //select�ȴ�������
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
            printf("���ӳ�ʱ\n");
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
        /*connectǰsocket���÷�����ģʽ*/
        int flags = fcntl(socket_new, F_GETFL, 0);
        fcntl(socket_new, F_SETFL, flags | O_NONBLOCK);
        /*���׽��ְ󶨵��������������ַ��*/
        int connect_return = connect(socket_new, (struct sockaddr *)&client.client_addr, sizeof(struct sockaddr));
        if (my_check_nonblock_connection(connect_return, socket_new) == CONNECTION_SUCCESS) //����������¼���Ƿ�������ӳɹ�����ֹδ���Ӽ���
        {
            client_SOCK[*create] = build_SOCK(socket_new); //������ʱָ��ΪNULL��Ҫ��̬����
            printf("��%d�����ӳɹ�!�׽���Ϊ:%d\n", ++(*create), socket_new);
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
        /*connectǰsocket���÷�����ģʽ*/
        int flags = fcntl(socket_new, F_GETFL, 0);
        fcntl(socket_new, F_SETFL, flags | O_NONBLOCK);
        /*���׽��ְ󶨵��������������ַ��*/
        int connect_return = connect(socket_new, (struct sockaddr *)&client.client_addr, sizeof(struct sockaddr));
        if (my_check_nonblock_connection(connect_return, socket_new) == CONNECTION_SUCCESS) //����������¼���Ƿ�������ӳɹ�����ֹδ���Ӽ���
        {
            client_SOCK[*create] = build_SOCK(socket_new); //������ʱָ��ΪNULL��Ҫ��̬����
            //�������
            struct epoll_event epoll_temp;
            epoll_temp.data.fd = socket_new;
            epoll_temp.events = EPOLLIN | EPOLLOUT;
            int epoll_ret = epoll_ctl(efd, EPOLL_CTL_ADD, client_SOCK[*create]->socket, &epoll_temp);
            if (epoll_ret == -1)
            {
                
                perror("epoll_ctl");
                printf("����my_new_connection_EPOLL��\n");
                return -1;
            }
            printf("��%d�����ӳɹ�!�׽���Ϊ:%d\n", ++(*create), socket_new);
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
    printf("�׽���%d����������������\n", client_SOCK[pos]->socket);
    //usleep(1000); //��ʱ�ȴ�ϵͳclose�׽���
    int socket_new;
    if ((socket_new = socket(PF_INET, SOCK_STREAM, 0)) < 0)
    {

        perror("socket");
        return 1;
    }
    /*connectǰsocket���÷�����ģʽ*/
    client_SOCK[pos]->socket = socket_new; //����ʱ�ṹ��ֻ��strû�ж�̬���룬����Ҫ��������
    int flags = fcntl(socket_new, F_GETFL, 0);
    fcntl(socket_new, F_SETFL, flags | O_NONBLOCK);
    /*���׽��ְ󶨵��������������ַ��*/
    int connect_return = connect(socket_new, (struct sockaddr *)&client.client_addr, sizeof(struct sockaddr));
    if (my_check_nonblock_connection(connect_return, socket_new) == CONNECTION_SUCCESS) //����������¼���Ƿ�������ӳɹ�����ֹδ���Ӽ���
    {
        client_SOCK[pos] = build_SOCK(socket_new); //������ʱָ��ΪNULL��Ҫ��̬����
        printf("�����ɹ�!�׽���Ϊ:%d,αװ�׽���Ϊ%d\n", socket_new, pos + 3);
        return CONNECTION_SUCCESS;
    }
    /*δ���ӳɹ�*/
    close(socket_new);
    return QUIT_ERROR;
}

int my_reconnect_EPOLL(int pos, SOCK **client_SOCK, struct client_conf client, int efd)
{
    printf("�׽���%d����������������\n", client_SOCK[pos]->socket);
    //usleep(1000); //��ʱ�ȴ�ϵͳclose�׽���
    int socket_new;
    if ((socket_new = socket(PF_INET, SOCK_STREAM, 0)) < 0)
    {

        perror("socket");
        return 1;
    }
    /*connectǰsocket���÷�����ģʽ*/
    client_SOCK[pos]->socket = socket_new; //����ʱ�ṹ��ֻ��strû�ж�̬���룬����Ҫ��������
    int flags = fcntl(socket_new, F_GETFL, 0);
    fcntl(socket_new, F_SETFL, flags | O_NONBLOCK);
    /*���׽��ְ󶨵��������������ַ��*/
    int connect_return = connect(socket_new, (struct sockaddr *)&client.client_addr, sizeof(struct sockaddr));
    if (my_check_nonblock_connection(connect_return, socket_new) == CONNECTION_SUCCESS) //����������¼���Ƿ�������ӳɹ�����ֹδ���Ӽ���
    {
        client_SOCK[pos] = build_SOCK(socket_new); //������ʱָ��ΪNULL��Ҫ��̬����
        printf("�����ɹ�!�׽���Ϊ:%d,αװ�׽���Ϊ%d\n", socket_new, pos + 3);
        //�������
        struct epoll_event epoll_temp;
        epoll_temp.data.fd = socket_new;
        epoll_temp.events = EPOLLIN | EPOLLOUT;
        int epoll_ret = epoll_ctl(efd, EPOLL_CTL_ADD, client_SOCK[pos]->socket, &epoll_temp);
        if (epoll_ret == -1)
        {
            perror("epoll_ctl");
            printf("����my_reconnect_EPOLL��\n");
            return -1;
        }
        return CONNECTION_SUCCESS;
    }
    /*δ���ӳɹ�*/
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
        free(client_SOCK[pos]->str); //���������ģ������strǰ����str��δ����
    if (status == QUIT_ERROR)
    {
        printf("�׽���ss%d�쳣�˳���������\n", client_SOCK[pos]->socket);
        //close(client_SOCK[pos]->socket);
        my_reconnect(pos, client_SOCK, client);
    }
    else if (status == MISSION_COMPLETE)
    {

        printf("�׽���ss%d����˳�\n", client_SOCK[pos]->socket);
        (*finished_num)++;
        printf("��ǰ�����:%d\n", *finished_num);
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
        free(client_SOCK[pos]->str); //���������ģ������strǰ����str��δ����
    if (status == QUIT_ERROR)
    {
        printf("�׽���ss%d�쳣�˳���������\n", client_SOCK[pos]->socket);
        //close(client_SOCK[pos]->socket);
        my_reconnect(pos, client_SOCK, client);
    }
    else if (status == MISSION_COMPLETE)
    {

        printf("�׽���ss%d����˳�\n", client_SOCK[pos]->socket);
        (*finished_num)++;
        printf("��ǰ�����:%d\n", *finished_num);
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
        free(client_SOCK[pos]->str); //���������ģ������strǰ����str��δ����
    if (status == QUIT_ERROR)
    {
        printf("�׽���ss%d�쳣�˳���������\n", client_SOCK[pos]->socket);
        close(client_SOCK[pos]->socket);
        my_reconnect_EPOLL(pos, client_SOCK, client,efd);
    }
    else if (status == MISSION_COMPLETE)
    {

        printf("�׽���ss%d����˳�\n", client_SOCK[pos]->socket);
        (*finished_num)++;
        printf("��ǰ�����:%d\n", *finished_num);
        if (client.num == *finished_num)
            *quit = 1;
        int epoll_ret = epoll_ctl(efd, EPOLL_CTL_DEL, client_SOCK[pos]->socket, NULL);
        if (epoll_ret == -1)
        {
            perror("epoll_ctl");
            printf("����my_disconnect_EPOLL��\n");
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
    memset(poll_fds, 0, sizeof(poll_fds)); //�������
    int i;
    for (i = 0; i < client.num; i++) //������ʹ���׽�������
    {
        if (client_SOCK[i]) //����׽������ڱ�ʹ��
        {
            poll_fds[*use_now].fd = client_SOCK[i]->socket; //�����׽��ּ�������
            poll_fds[*use_now].events = POLLIN | POLLOUT;   //���ù�ע��/д�¼�
            client_SOCK[i]->poll_pos = *use_now;
            (*use_now)++;
        }
    }
}

int EPOLLFD_init(int efd, struct client_conf client, SOCK **client_SOCK)
{
    struct epoll_event epoll_temp;
    int i;
    for (i = 0; i < client.num; i++) //������ʹ���׽�������
    {
        if (client_SOCK[i]) //����׽������ڱ�ʹ��
        {
            epoll_temp.data.fd = client_SOCK[i]->socket;
            epoll_temp.events = EPOLLIN | EPOLLOUT;
            //��������¼�
            int epoll_ret = epoll_ctl(efd, EPOLL_CTL_ADD, client_SOCK[i]->socket, &epoll_temp);
            if (epoll_ret == -1)
            {
                perror("epoll_ctl");
                printf("����!\n");
                return -1;
            }
        }
    }
}

int EPOLLFD_quit(int efd, struct client_conf client, SOCK **client_SOCK)
{
    struct epoll_event epoll_temp;
    int i;
    for (i = 0; i < client.num; i++) //������ʹ���׽�������
    {
        if (client_SOCK[i]) //����׽������ڱ�ʹ��
        {
            epoll_temp.data.fd = client_SOCK[i]->socket;
            epoll_temp.events = EPOLLIN | EPOLLOUT;
            //��������¼�
            int epoll_ret = epoll_ctl(efd, EPOLL_CTL_ADD, client_SOCK[i]->socket, &epoll_temp);
            if (epoll_ret == -1)
            {
                perror("epoll_ctl");
                printf("����!\n");
                return -1;
            }
        }
    }
}

void client_SOCK_init(SOCK **client_SOCK, struct client_conf client)
{
    int k = 0; //ѭ����־
    for (k = 0; k < client.num; k++)
    {
        client_SOCK[k] = NULL;
    }
}
int main(int argc, char *argv[])
{
    /*�ͻ��˻�������*/
    struct client_conf client;                                         //client���ݽṹ
    memset(&client, 0, sizeof(client));                                //client��������Ϣ����
    client_conf_argv(argc, argv, &client);                             //����main������������client
    print_client_conf(client);                                         //��ʾclient������Ϣ
    SOCK **client_SOCK = (SOCK **)malloc(client.num * sizeof(SOCK *)); //�ͻ���SOCKָ������,���1000������
    client_SOCK_init(client_SOCK, client);                             //�ͻ���SOCKָ�������ʼ��

    /*��������*/
    int k = 0; //ѭ����־

    /*����ά������*/
    char reply[200];      //���ݴ��͵Ļ�����
    int create = 0;       //�Ѿ��������ӵļ�������������Ӱ�죬���100�������е�ĳһ���Ͽ������������׽��ֱ�ɾ���������ǰ��creat�Ǵ���ʵ����������
    int quit = 0;         //���н�������ɱ�־
    int finished_num = 0; //������Ӽ���

    /*select�ñ���*/
    fd_set west;                         //����������
    fd_set rest;                         //д��������
    struct timeval tv;                   //select�ȴ�ʱ�� NULL����
    long start_mt = longgetSystemTime(); //��ʼʱ�䣬���뾫��

    /*poll�ñ���*/
    struct pollfd poll_fds[MAX_CON];

    /*epoll�ñ���*/
    struct epoll_event epoll_fds[MAX_CON];
    int efd;
    efd = epoll_create1(0);
    if (efd == -1)
    {
        perror("epoll_create");
        exit(0);
    }

    while (1)
    {
        if (client.select == SELECT)
        {
            my_new_connection(&create, &start_mt, client_SOCK, client);
            //printf("1");
            if (quit)
                break;
            tv.tv_sec = 0;
            tv.tv_usec = 0;
            FD_init(&rest, &west, client, client_SOCK);
            int select_ret = select(FD_SETSIZE, &rest, &west, NULL, &tv);
            if (select_ret == -1)
            {
                perror("select");
                return -1;
            }
            else
                for (k = 0; k < client.num; k++)
                {
                    if (client_SOCK[k])
                    {
                        memset(reply, 0, 200);
                        int read_return = my_read_SELECT(client_SOCK[k], &rest, reply);
                        my_disconnect_SELECT(client, client_SOCK, &finished_num, &quit, &west, &rest, read_return, k);
                        int write_return = my_write_SELECT(client_SOCK[k], &west, k);
                        my_disconnect_SELECT(client, client_SOCK, &finished_num, &quit, &west, &rest, write_return, k);
                    }
                }
        }
        else if (client.select == POLL)
        {
            my_new_connection(&create, &start_mt, client_SOCK, client);
            //printf("1");
            if (quit)
                break;
            int now_use_pos = 0;
            POLLFD_init(poll_fds, client, client_SOCK, &now_use_pos);
            int nfds = MAX_CON; //��ǰ��Ҫ�������׽�������
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
        else if (client.select == EPOLL)
        {
            my_new_connection_EPOLL(&create, &start_mt, client_SOCK, client, efd);
            //printf("1");
            if (quit)
                break;
            int epoll_return = epoll_wait(efd, epoll_fds, MAX_CON, 0);
            for (k = 0; k < client.num; k++)
            {
                if (client_SOCK[k]) //����ʹ�õ��׽���
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

    for (k = 0; k < client.num; k++)
    {
        //close(client_SOCK[k]->socket);
        //free(client_SOCK[k]->str);
        //free(client_SOCK[k]);
    }
    return 0;
}