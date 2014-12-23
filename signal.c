/*
 *信号时一种异步事件:信号处理函数和程序的主循环式两条不同的执行路线,信号处理函数需要尽可能快地执行完毕,以确保该信号
 *不被屏蔽.(为了避免一些竞态条件,信号在处理期间,系统不会再次出发它)太久.这里就采用一种常用的解决方案是:把信号的主要
 *处理函数逻辑放到程序的主循环中,当信号处理函数被触发时,它只是简单地通知主循环程序接受到信号,并把信号值传递给主函数.
 *主循环在根据接受到的信号值执行目标信号对应的处理逻辑代码.通常采用管道的方式来将"信号"传递给主循环.主程序采用I/O复
 *用模型来将信号事件和其他事件统一处理.即统一事件源.
*/

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/epoll.h>
#include <pthread.h>
#include <errno.h>
#include <signal.h>

#define MAX_EVENT_NUMBER 1024
static int pipefd[2];

int setnonblocking(int fd){
      int old_option=fcntl(fd,F_GETFL);
      int new_option=old_option | O_NONBLOCK;
      
      fcntl(fd,F_SETFL,new_option);
      return old_option;
}

void addfd(int epollfd,int fd){
      struct epoll_event event;
      event.data.fd=fd;
      event.events =EPOLLIN | EPOLLET;
      epoll_ctl(epollfd,EPOLL_CTL_ADD,fd,&event);
      setnonblocking(fd);
}

void sig_handler(int sig){
    int save_errno=errno;
    int msg=sig;
    send(pipefd[1],(char*)&msg,1,0);
    errno=save_errno;
}


void addsig(int sig){
    struct sigaction sa;
    memset(&sa,'\0',sizeof(sa));
    sa.sa_flags|=SA_RESTART;
    sa.sa_handler=sig_handler;
    sigfillset(&sa.sa_mask);
    assert(sigaction(sig,&sa,NULL)!=-1);
}


int main(int argc,char* argv[]){
     if(argc<=2){
           printf("usage:%s ip_address port_number\n",argv[0]);
           return -1;
     }
     
     const char* ip=argv[1];
     int port=atoi(argv[2]);
     
     struct sockaddr_in server_addr;
     memset(&server_addr,0,sizeof(server_addr));
     server_addr.sin_family=AF_INET;
     inet_pton(AF_INET,ip,&server_addr.sin_addr));
     server_addr.sin_port=htons(port));
     
     int listenfd=socket(AF_INET,SOCK_STREAM,0);
     assert(listenfd!=-1);
     
     int ret=bind(listenfd,(struct sockaddr*)&server_addr,sizeof(server_addr));
     assert(ret!=-1);
     
     ret=listen(listenfd,5);
     assert(ret!=-1);
     
     epoll_event events[MAX_EVENT_NUMBER];
     int epollfd=epoll_create(5);
     assert(epollfd!=-1);
     addfd(epollfd,listenfd);
     
     ret=socketpair(AF_UNIX,SOCK_STREAM,0,pipefd);
     assert(ret!=-1);
     
     setnonblocking(pipefd[1]);
     addfd(epollfd,pipefd[0]);
     
     addsig(SIGHUP);
     addsig(SIGCHLD);
     addsig(SIGTERM);
     addsig(SIGINT);
     bool stop_server=false;
     
     while(!stop_server){
            int number=epoll_wait(epollfd,events,MAX_EVENT_NUMBER,-1);
            if((number<0) && (errno!=EINTR)){
                   printf("epoll failure\n");
                   break;
            }
            
            else{
                  for(int i=0;i<number;i++){
                         int sockfd=events[i].data.fd;
                         if(sockfd==listenfd){
                                struct sockaddr_in client;
                                bzero(&client,sizeof(client));
                                socklen_t len=sizeof(client);
                                
                                int connfd=accept(sockfd,(struct sockaddr*)&client,&len);
                                addfd(epollfd,connfd);
                         }
                         
                         else if(sockfd==pipefd[0] && events[i].events & EPOLLIN){
                              char signals[1024];
                              memset(signals,'\0',sizeof(signals));
                              
                              int ret=recv(sockfd,signals,1024,0);
                              if(ret<0){
                                   continue;
                              }   
                              else if(ret==0){
                                  continue;
                              }       
                              
                              else{
                                  for(int i=0;i<ret;i++){
                                        switch(signals[i])){
                                            case SIGCHLD:
                                            case SIGHUP:
                                                {
                                                    continue;
                                                }
                                                
                                                case SIGTERM:
                                                case SIGINT:
                                                    {
                                                        stop_server=true;
                                                    }
                                        }
                                  }
                              }
                         }
                         
                         else{
                        }
                  }
            }
     }
     printf("close fds\n");
     close(listenfd);
     close(pipefd[1]);
     close(pipefd[0]);
     return 0;
}
