
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <sys/socket.h>
#include <netinet/in.h> 
#include <netdb.h> 
#include <assert.h>
#include <string.h>
#include <pthread.h>
#include <sys/time.h>
#include <arpa/inet.h>
#include "utillitys.h"
#include <ifaddrs.h>


#define BASE_PORT 1200
#define LB_IP "127.0.0.1"             //change to fit for each run
#define LB_PORT 8700

int send_message(int sock,int type,char* header,char* payload,int payload_len);
struct message* get_message(int soc);

struct job{
    int job_id;
    time_t timestamp;
    int capacity;
};

int main(){
    int capacity=5;
    printf("my cap is:%d\n",capacity);

    int res;
    struct ifaddrs *ifaddr, *ifa;
    char ip[INET_ADDRSTRLEN];
    if (getifaddrs(&ifaddr) == -1) {
        perror("getifaddrs");
        exit(EXIT_FAILURE);
    }

    for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
        if (ifa->ifa_addr == NULL)
            continue;

        if (ifa->ifa_addr->sa_family == AF_INET) {
            struct sockaddr_in *addr = (struct sockaddr_in *)ifa->ifa_addr;
            inet_ntop(AF_INET, &addr->sin_addr, ip, sizeof(ip));
        }
    }
    printf("server IP is: %s\n", ip);

    int lb_sock= socket(AF_INET, SOCK_STREAM, 0);
    assert (lb_sock != -1);
    struct sockaddr_in server_soc_Addr;                         //LB unicast socket setup
    server_soc_Addr.sin_family = AF_INET;
    server_soc_Addr.sin_port = htons(BASE_PORT);
    server_soc_Addr.sin_addr.s_addr = inet_addr(ip);
    res=bind(lb_sock, (struct sockaddr*)&server_soc_Addr , sizeof(server_soc_Addr));
    if(res<0){
        printf("bind server failed\n");
        return 0;
    }

    // int client_sock= socket(AF_INET, SOCK_STREAM, 0);
    // assert (client_sock != -1);
    // struct sockaddr_in client_sock_Addr;                         //client socket setup
    // client_sock_Addr.sin_family = AF_INET;
    // client_sock_Addr.sin_port = htons(BASE_PORT+2);
    // client_sock_Addr.sin_addr.s_addr = inet_addr(ip);
    // res=bind(client_sock, (struct sockaddr*)&client_sock_Addr , sizeof(client_sock_Addr));
    int client_welcome_sock;
    client_welcome_sock=createWelcomeSocket(BASE_PORT+2,3);
    printf("bind client socket:%d\n",client_welcome_sock);
    
    struct sockaddr_in lb_soc_Addr;                             
    lb_soc_Addr.sin_family = AF_INET;
    lb_soc_Addr.sin_port = htons(LB_PORT);
    lb_soc_Addr.sin_addr.s_addr = inet_addr(LB_IP);
    //lb_soc_Addr.sin_addr.s_addr = INADDR_ANY; 
    res=connect(lb_sock, (struct sockaddr*)&lb_soc_Addr , sizeof(lb_soc_Addr));     //connect to lb
    if(res<0){
        printf("connection failed\n");
        return 0;
    }
    char client_sock_addr[20];
    strcpy(client_sock_addr,ip);
    strcat(client_sock_addr,":");                                           //make registration message and send to LB
    sprintf(client_sock_addr+strlen(ip)+1,"%d",BASE_PORT+2);

    char capacity_c[2];   //is 2 only to stop warnings, will always be 1 char long
    sprintf(capacity_c,"%d",capacity);

    send_message(lb_sock,SERVER_UPDATE,&capacity_c[0],client_sock_addr,strlen(client_sock_addr));
    struct message* lb_response_ptr=get_message(lb_sock);       //wait for LB ack
    printf("got LB ack\n");
    if(lb_response_ptr==NULL){
        close(lb_sock);
        close(client_welcome_sock);
        return 0;
    }
    
    struct message lb_response=*lb_response_ptr;
    if(lb_response.type!=LB_ACK){
        printf("got invalid message type ,closing server\n");
        close(lb_sock);
        close(client_welcome_sock);
        return 0;
    }
    //create multicast
    printf("got valid ack message from LB: %s",lb_response.payload);

    struct timeval select_timeout;
    select_timeout.tv_sec=1;
    select_timeout.tv_usec=0;
    
    fd_set server_sockets,server_sockets_loop;
    FD_ZERO(&server_sockets);
    FD_SET(lb_sock,&server_sockets);
    //FD_SET(multicast_sock,&server_sockets);
    FD_SET(client_welcome_sock,&server_sockets);
    int max_fd=client_welcome_sock;

    struct job client_jobs[5]; //holds accepted jobs 
    int clients[5]; //holds client fds that are being served 
    for(int i=0;i<5;i++){       //init for pending_jobs
        client_jobs[i].job_id=-1;
        clients[i]=-1;
    }
    
    int running=1;
    while (running)
    { 
        server_sockets_loop=server_sockets;
        printf("%d\n",select(max_fd+1,&server_sockets_loop,NULL,NULL,&select_timeout));       //maby neet to change to max
        select_timeout.tv_sec=1;
        
        if(FD_ISSET(lb_sock,&server_sockets_loop)){
            struct message* new_message_ptr=get_message(lb_sock);
            if(new_message_ptr!=NULL){
                struct message new_message=*new_message_ptr;
                printf("server got message from LB of type %d\n",new_message.type); 
                switch (new_message.type)
                {
                    case LB_UPDATE:
                        sprintf(capacity_c,"%d",capacity);
                        send_message(lb_sock,SERVER_UPDATE,&capacity_c[0],client_sock_addr,strlen(client_sock_addr));
                        break;
                    case LB_JOB:
                        char job_cap=new_message.header[0];
                        if(job_cap<'0'||job_cap>'9'){
                            printf("got bad capacity for job: %c discarding",job_cap);
                        }
                        else{
                            char respons_header=new_message.header[1]; //job id
                            char job_ans;
                            if(job_cap>capacity+'0'){
                                int i;
                                for( i=0;i<5;i++){       //set aside a place for client
                                    if(client_jobs[i].job_id==-1){
                                        client_jobs[i].job_id=new_message.header[1]-'0';
                                        client_jobs[i].timestamp=time(NULL);
                                        job_ans='1'; //accept
                                        capacity=capacity-(job_cap-'0');
                                        break;
                                    }
                                }
                                if(i==5){
                                    job_ans='0'; //reject
                                }

                            }else{
                                job_ans='0'; //reject
                            }
                            send_message(lb_sock,JOB_ACK,&respons_header,client_sock_addr,strlen(client_sock_addr));
                        }   
                        break;
                }
            }
            else{
                printf("connection with LB terminated, shuting down...\n");
                //dealocs
                return 0;
            }
        }
        if(FD_ISSET(client_welcome_sock,&server_sockets_loop)){
            int new_client=accept(client_welcome_sock,NULL , NULL);             
            struct message* new_message_ptr=get_message(new_client);   //need to add timeout
            if(new_message_ptr!=NULL){
                //get first message from client
                //look at pending jobs to verify job id is valid
                //not valid, close connection and continue
                //valid, put fd in clients[index of client_jobs that job id is in]
                //add fd to fd set
                //if new fd< max -> max=new fd
            }
        }
        //if(FD_ISSET(multi sock,&server_sockets_loop))
            // struct message* new_message_ptr=get_message(lb_sock);
            // if(new_message_ptr!=NULL){
            //     struct message new_message=*new_message_ptr;
            //     if(new_message.type==LB_UPDATE){
            //         sprintf(capacity_c,"%d",capacity);
            //         send_message(lb_sock,SERVER_UPDATE,&capacity_c[0],client_sock_addr,strlen(client_sock_addr));
            //     }
            //     else{
            //         printf("got unknown multicast message\n");
            //     }
            // }
            
        for(int j=0;j<5;j++){       //set aside a place for client
            if(clients[j]!=-1){
                if(FD_ISSET(clients[j],&server_sockets_loop)){
                    struct message* new_message_ptr=get_message(clients[j]);
                    if(new_message_ptr!=NULL){
                        struct message new_message=*new_message_ptr;
                        //add handeling for client messages and clean up on connection end
                    }
                }
            }
        }

    }


    return 0;
}