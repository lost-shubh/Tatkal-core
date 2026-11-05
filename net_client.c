#include "tatkal.h"
#include <errno.h>
#include <netdb.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

static int connect_to(const char *host,const char *port){
    struct addrinfo hints={.ai_socktype=SOCK_STREAM,.ai_family=AF_UNSPEC},*list=NULL,*p;int fd=-1;
    if(getaddrinfo(host,port,&hints,&list))return -1;
    for(p=list;p;p=p->ai_next){fd=socket(p->ai_family,p->ai_socktype,p->ai_protocol);if(fd>=0&&connect(fd,p->ai_addr,p->ai_addrlen)==0)break;if(fd>=0)close(fd),fd=-1;}
    freeaddrinfo(list);return fd;
}

int main(int argc,char **argv){
    if(argc!=7){fprintf(stderr,"Usage: %s HOST PORT REQUEST_ID TRAIN BERTHS PRIORITY:PREFERRED\nExample: %s 127.0.0.1 5050 1001 12034 2 1:-1\n",argv[0],argv[0]);return 1;}
    char *colon=strchr(argv[6],':');if(!colon){fputs("Last argument must be PRIORITY:PREFERRED\n",stderr);return 1;}*colon=0;
    char request[160];int n=snprintf(request,sizeof request,"%s,%s,%s,%s,%s\n",argv[3],argv[4],argv[5],argv[6],colon+1);if(n<0||n>=(int)sizeof request)return 1;
    int fd=connect_to(argv[1],argv[2]);if(fd<0){perror("Cannot connect to Tatkal-Core TCP server");return 1;}
    if(send(fd,request,(size_t)n,MSG_NOSIGNAL)!=n){perror("send");close(fd);return 1;}shutdown(fd,SHUT_WR);
    struct pollfd wait_fd={.fd=fd,.events=POLLIN};if(poll(&wait_fd,1,30000)<=0){fputs("No server result within 30 seconds. Retain the request ID and inspect the WAL.\n",stderr);close(fd);return 1;}
    char response[256];ssize_t got=recv(fd,response,sizeof response-1,0);close(fd);if(got<=0){fputs("Server closed without a result.\n",stderr);return 1;}response[got]=0;fputs(response,stdout);return strncmp(response,"CONFIRMED",9)!=0;
}
