#include "tatkal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>

#define TCP_CLIENTS 64
#define TCP_LINE 256

typedef struct { int fd; char line[TCP_LINE]; size_t used; uint64_t last_activity_ms; } TcpClient;
struct TcpIngress { int epoll_fd, listen_fd; TcpClient clients[TCP_CLIENTS]; };

static int nonblocking(int fd){int flags=fcntl(fd,F_GETFL,0);return flags<0||fcntl(fd,F_SETFL,flags|O_NONBLOCK);}
static TcpClient *client_for(TcpIngress *tcp,int fd){for(int i=0;i<TCP_CLIENTS;i++)if(tcp->clients[i].fd==fd)return &tcp->clients[i];return NULL;}
static void tcp_reply_close(TcpIngress *tcp,TcpClient *client,const char *message){(void)send(client->fd,message,strlen(message),MSG_NOSIGNAL);epoll_ctl(tcp->epoll_fd,EPOLL_CTL_DEL,client->fd,NULL);close(client->fd);client->fd=-1;client->used=0;client->last_activity_ms=0;}
static void record_ingress_rejection(Metrics *m){pthread_mutex_lock(&m->lock);m->submitted++;m->rejected++;pthread_mutex_unlock(&m->lock);}

int ingress_tcp_start(const char *bind_address,int port,TcpIngress **out){
    if(!out||port<1||port>65535){errno=EINVAL;return -1;}
    TcpIngress *tcp=calloc(1,sizeof *tcp);if(!tcp)return -1;
    tcp->epoll_fd=tcp->listen_fd=-1;
    for(int i=0;i<TCP_CLIENTS;i++)tcp->clients[i].fd=-1;
    tcp->listen_fd=socket(AF_INET,SOCK_STREAM,0);if(tcp->listen_fd<0)goto failed;
    int one=1;(void)setsockopt(tcp->listen_fd,SOL_SOCKET,SO_REUSEADDR,&one,sizeof one);
    struct sockaddr_in address={.sin_family=AF_INET,.sin_port=htons((uint16_t)port)};
    if(!bind_address||!*bind_address)bind_address="127.0.0.1";
    if(inet_pton(AF_INET,bind_address,&address.sin_addr)!=1){errno=EINVAL;goto failed;}
    if(bind(tcp->listen_fd,(struct sockaddr *)&address,sizeof address)||listen(tcp->listen_fd,32)||nonblocking(tcp->listen_fd))goto failed;
    tcp->epoll_fd=epoll_create1(EPOLL_CLOEXEC);if(tcp->epoll_fd<0)goto failed;
    struct epoll_event event={.events=EPOLLIN,.data.fd=tcp->listen_fd};if(epoll_ctl(tcp->epoll_fd,EPOLL_CTL_ADD,tcp->listen_fd,&event))goto failed;
    *out=tcp;return 0;
failed: if(tcp->epoll_fd>=0)close(tcp->epoll_fd);if(tcp->listen_fd>=0)close(tcp->listen_fd);free(tcp);return -1;
}

static void accept_clients(TcpIngress *tcp){
    for(;;){int fd=accept(tcp->listen_fd,NULL,NULL);if(fd<0){if(errno==EAGAIN||errno==EWOULDBLOCK)return;return;}if(nonblocking(fd)){close(fd);continue;}TcpClient *slot=NULL;for(int i=0;i<TCP_CLIENTS;i++)if(tcp->clients[i].fd<0){slot=&tcp->clients[i];break;}if(!slot){(void)send(fd,"REJECTED reason=too many clients\n",33,MSG_NOSIGNAL);close(fd);continue;}slot->fd=fd;slot->used=0;slot->last_activity_ms=now_ms();struct epoll_event event={.events=EPOLLIN|EPOLLRDHUP,.data.fd=fd};if(epoll_ctl(tcp->epoll_fd,EPOLL_CTL_ADD,fd,&event)){close(fd);slot->fd=-1;}}
}

static int parse_request(char *line,BookingRequest *request){
    unsigned long long id;int train,berths,priority,preferred;char extra;
    if(sscanf(line," %llu , %d , %d , %d , %d %c",&id,&train,&berths,&priority,&preferred,&extra)!=5||!id||train<1||train>99999||berths<1||berths>MAX_REQUEST_BERTHS||priority<PRIORITY_GENERAL||priority>PRIORITY_HIGH||preferred < -1||preferred>=MAX_BERTHS)return -1;
    *request=(BookingRequest){.id=(uint64_t)id,.train_id=train,.berth_count=berths,.priority=(Priority)priority,.preferred_berth=preferred,.arrived_ms=now_ms(),.wants_reply=2};return 0;
}

static void read_client(TcpIngress *tcp,TcpClient *client,TaskQueue *q,Metrics *m,BookingSystem *booking,Wal *wal){
    for(;;){ssize_t n=recv(client->fd,client->line+client->used,sizeof client->line-1-client->used,0);if(n==0){tcp_reply_close(tcp,client,"REJECTED reason=incomplete request\n");return;}if(n<0){if(errno==EAGAIN||errno==EWOULDBLOCK)return;tcp_reply_close(tcp,client,"REJECTED reason=read failure\n");return;}client->last_activity_ms=now_ms();client->used+=(size_t)n;client->line[client->used]=0;char *end=strchr(client->line,'\n');if(!end){if(client->used==sizeof client->line-1){record_ingress_rejection(m);tcp_reply_close(tcp,client,"REJECTED reason=request too long\n");}continue;}*end=0;
        unsigned long long id;BookingResult result;
        int availability_train;
        if(sscanf(client->line,"AVAIL,%d",&availability_train)==1){
            char response[96]; int available=booking_available_count(booking,availability_train);
            snprintf(response,sizeof response,"AVAIL train=%d seats=%d coaches=3 berths_per_coach=40\n",availability_train,available);
            tcp_reply_close(tcp,client,response); return;
        }
        if(sscanf(client->line,"STATUS,%llu",&id)==1){if(booking_query_status(booking,(uint64_t)id,&result)==0){char response[192];int used=snprintf(response,sizeof response,"STATUS CONFIRMED request=%llu train=%d berths=",id,result.train_id);for(int i=0;i<result.berth_count;i++)used+=snprintf(response+used,sizeof response-(size_t)used,"%s%d",i?",":"",result.berth_ids[i]);snprintf(response+used,sizeof response-(size_t)used,"\n");tcp_reply_close(tcp,client,response);}else tcp_reply_close(tcp,client,"STATUS NOT_FOUND\n");return;}
        if(sscanf(client->line,"CANCEL,%llu",&id)==1){result=booking_cancel(booking,wal,(uint64_t)id);if(result.status==REQUEST_CANCELLED)tcp_reply_close(tcp,client,"CANCELLED\n");else tcp_reply_close(tcp,client,"CANCEL REJECTED\n");return;}
        BookingRequest request;if(parse_request(client->line,&request)){record_ingress_rejection(m);tcp_reply_close(tcp,client,"REJECTED reason=invalid format; use id,train,berths,priority,preferred_berth\n");return;}request.reply_fd=client->fd;if(queue_try_push(q,request,m)){record_ingress_rejection(m);tcp_reply_close(tcp,client,"REJECTED reason=server queue full\n");return;}epoll_ctl(tcp->epoll_fd,EPOLL_CTL_DEL,client->fd,NULL);client->fd=-1;client->used=0;return;}
}

int ingress_tcp_poll(TcpIngress *tcp,TaskQueue *q,Metrics *m,BookingSystem *booking,Wal *wal,int timeout_ms){
    if(!tcp)return 0;
    struct epoll_event events[16];
    int ready=epoll_wait(tcp->epoll_fd,events,16,timeout_ms);
    if(ready<0)return errno==EINTR?0:-1;
    uint64_t now=now_ms();for(int i=0;i<TCP_CLIENTS;i++)if(tcp->clients[i].fd>=0&&now-tcp->clients[i].last_activity_ms>10000)tcp_reply_close(tcp,&tcp->clients[i],"REJECTED reason=idle timeout\n");
    for(int i=0;i<ready;i++){if(events[i].data.fd==tcp->listen_fd)accept_clients(tcp);else{TcpClient *client=client_for(tcp,events[i].data.fd);if(!client)continue;if(events[i].events&EPOLLIN)read_client(tcp,client,q,m,booking,wal);else if(events[i].events&(EPOLLERR|EPOLLHUP|EPOLLRDHUP))tcp_reply_close(tcp,client,"REJECTED reason=client disconnected\n");}}
    return 0;
}

void ingress_tcp_stop(TcpIngress *tcp){if(!tcp)return;for(int i=0;i<TCP_CLIENTS;i++)if(tcp->clients[i].fd>=0)close(tcp->clients[i].fd);if(tcp->listen_fd>=0)close(tcp->listen_fd);if(tcp->epoll_fd>=0)close(tcp->epoll_fd);free(tcp);}

int ingress_load_file(const char *path, TaskQueue *q, Metrics *m) {
    FILE *f = fopen(path, "r");
    if (!f) {
        perror("failed to open ingress file");
        return -1;
    }

    int count = 0;
    char line[256]; int rejected=0;
    while (fgets(line,sizeof line,f)) {
        if(line[0]=='#'||line[0]=='\n'||line[0]=='\r') continue;
        unsigned long long id; int train_id,berth_count,priority,preferred_berth;
        char extra;
        if(sscanf(line," %llu , %d , %d , %d , %d %c",&id,&train_id,&berth_count,&priority,&preferred_berth,&extra)!=5) { rejected++; continue; }
        BookingRequest r = {
            .id = (uint64_t)id,
            .train_id = train_id,
            .berth_count = berth_count,
            .priority = (Priority)priority,
            .preferred_berth = preferred_berth,
            .arrived_ms = now_ms()
        };
        if(queue_push(q,r,m)==0) count++; else rejected++;
    }

    fclose(f);
    printf("Ingress: accepted=%d rejected=%d file=%s\n", count,rejected,path);
    return count;
}
