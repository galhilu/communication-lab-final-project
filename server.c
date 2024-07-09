
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



void server(int start_data[]){
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
    
    int max_capacity=start_data[0];
    int capacity=max_capacity;
    printf("my cap is:%d\n",max_capacity);


    int lb_sock= socket(AF_INET, SOCK_STREAM, 0);
    assert (lb_sock != -1);
    struct sockaddr_in server_soc_Addr;                         //LB unicast socket setup
    server_soc_Addr.sin_family = AF_INET;
    server_soc_Addr.sin_port = htons(BASE_PORT);
    server_soc_Addr.sin_addr.s_addr = inet_addr(ip);
    res=bind(lb_sock, (struct sockaddr*)&server_soc_Addr , sizeof(server_soc_Addr));
    printf("bind server to LB:%d\n",res);

    // int client_sock= socket(AF_INET, SOCK_STREAM, 0);
    // assert (client_sock != -1);
    // struct sockaddr_in client_sock_Addr;                         //client socket setup
    // client_sock_Addr.sin_family = AF_INET;
    // client_sock_Addr.sin_port = htons(BASE_PORT+2);
    // client_sock_Addr.sin_addr.s_addr = inet_addr(ip);
    // res=bind(client_sock, (struct sockaddr*)&client_sock_Addr , sizeof(client_sock_Addr));
    int client_sock;
    client_sock=createWelcomeSocket(BASE_PORT+2,3);
    printf("bind client socket:%d\n",client_sock);

    struct sockaddr_in lb_soc_Addr;                             
    lb_soc_Addr.sin_family = AF_INET;
    lb_soc_Addr.sin_port = htons(LB_PORT);
    lb_soc_Addr.sin_addr.s_addr = inet_addr(LB_IP);
    //lb_soc_Addr.sin_addr.s_addr = INADDR_ANY; 
    res=connect(lb_sock, (struct sockaddr*)&lb_soc_Addr , sizeof(lb_soc_Addr));     //connect to lb
    
    printf("connect LB:%d\n",res);
    //sleep(1);
    char client_sock_addr[20];
    strcpy(client_sock_addr,ip);
    strcat(client_sock_addr,":");                                           //make registration message and send to LB
    sprintf(client_sock_addr+strlen(ip)+1,"%d",BASE_PORT+2);
    char temp[2];   //is 2 only to stop warnings, will always be 1 char long
    sprintf(temp,"%d",capacity);
    printf("temp:%s\n",temp);
    printf("temp:%d\n",temp[0]);
    printf("temp:%d\n",temp[1]);
    send_message(lb_sock,SERVER_UPDATE,&temp[0],client_sock_addr,strlen(client_sock_addr));
    printf("connect LB:%d\n",res);
    struct message* lb_response_ptr=get_message(lb_sock);
    printf("got LB ack\n");
    if(lb_response_ptr==NULL){
        close(lb_sock);
        //multycast
        close(client_sock);
        return;
    }
    //create multicast
    struct message lb_response=*lb_response_ptr;
    if(lb_response.type!=LB_ACK){
        printf("got invalid message type ,closing server\n");
        close(lb_sock);
        //multycast
        close(client_sock);
        return;
    }
    printf("got valid ack message from LB: %s",lb_response.payload);
    fd_set server_sockets,server_sockets_loop;
    int running=1;
    struct timeval select_timeout;
    select_timeout.tv_sec=1;
    select_timeout.tv_usec=0;
    FD_ZERO(&server_sockets);
    FD_SET(lb_sock,&server_sockets);
    //FD_SET(multicast_sock,&server_sockets);
    FD_SET(client_sock,&server_sockets);


    while (running)
    { 
            server_sockets_loop=server_sockets;
            printf("%d\n",select(client_sock+1,&server_sockets,NULL,NULL,&select_timeout));       //maby neet to change to max
            select_timeout.tv_sec=1;
            if(FD_ISSET(lb_sock,&server_sockets)){
                struct message* new_message_ptr=get_message(lb_sock);
                if(new_message_ptr!=NULL){
                    struct message new_message=*new_message_ptr;
                    printf("server got message from LB of type %d\n",new_message.type); 
                    switch (new_message.type)
                    {
                        case LB_UPDATE:
                        break;
                        case LB_JOB:
                        break;
                    }
                }
            }
            if(FD_ISSET(client_sock,&server_sockets)){
                struct message* curr_message_ptr=get_message(client_sock);
            }
            //if(FD_ISSET(multi sock,&server_sockets))

            

    }


    return;
}


int main() {
    int server_num=2;
    int up_times[]={0,100};
    assert (sizeof(up_times)/sizeof(int) == server_num);
    int capacitys[]={2,5};
    assert (sizeof(capacitys)/sizeof(int) == server_num);
    time_t event_timer=time(NULL);
    int i=0;
    while(1){
        if (time(NULL)-event_timer>up_times[i]){
            pthread_t server_thread;
            int start_data[]={capacitys[i],i*3};        //i*3 for three ports for each server: BASE_PORT+i*3= LB sock, BASE_PORT+i*3+1=multiycast sock, BASE_PORT+i*3+2=client welcome sock
            pthread_create(&server_thread,NULL,(void*)&server,(void*) &start_data);
            i++;
            if (i==server_num){
                break;
            }
        }
    }
    //wait for all threads to finish
    printf("all servers down");
    return 0;

}