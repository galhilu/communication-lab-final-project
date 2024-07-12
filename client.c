#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <sys/socket.h>
#include <netinet/in.h> 
#include <assert.h>
#include <string.h>
#include <pthread.h>
#include <sys/time.h>
#include "utillitys.h"
#include <ifaddrs.h>


#define BASE_PORT 1100
#define LB_IP "127.0.0.1"             //change to fit for each run
#define LB_PORT 6500
#define MESSAGE_COUNT_TO_SERVER 2

int send_message(int sock, int type, char* header, char* payload, int payload_len);
void splitIpPort(const char *input, char *ip_address, int *port);
struct message* get_message(int soc);




void main(){
    int res;

    // get IP for self
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
    printf("client IP is: %s\n", ip);

    //create socket for client-LB connection and bind it
    int lb_sock = socket(AF_INET, SOCK_STREAM, 0);
    assert(lb_sock != -1);
    struct sockaddr_in soc_Addr;                         //LB socket setup
    soc_Addr.sin_family = AF_INET;
    soc_Addr.sin_port = htons(BASE_PORT);
    soc_Addr.sin_addr.s_addr = inet_addr(ip);
    res=bind(lb_sock, (struct sockaddr*)&soc_Addr , sizeof(soc_Addr));
    printf("bind client to LB socket:%d\n",res);

    //connect client to LB
    struct sockaddr_in lb_soc_Addr;                             
    lb_soc_Addr.sin_family = AF_INET;
    lb_soc_Addr.sin_port = htons(LB_PORT);
    lb_soc_Addr.sin_addr.s_addr = inet_addr(LB_IP);
    res=connect(lb_sock, (struct sockaddr*)&lb_soc_Addr , sizeof(lb_soc_Addr));
    
    //creating message to send to LB
    int random_job_weight = (rand()%10)+1;
    printf("my capacity is: %d\n",random_job_weight);
    char header_c[2];
    sprintf(header_c,"%d",random_job_weight);
    char pyload[]="me client! you give me server";
    send_message(lb_sock, CLIENT_REQ, header_c, pyload, strlen(pyload));

    //receive message from LB
    struct message* lb_response_ptr = get_message(lb_sock);
    if(lb_response_ptr == NULL){
        close(lb_sock);
        return;
    }
    struct message lb_response = *lb_response_ptr;
    if (lb_response.type != CLIENT_REQ_ACK){
        printf("got invalid message type ,closing client\n");
        close(lb_sock);
        return;
    }
    printf("got valid ack message from LB: %s\n", lb_response.payload);

    //parse data from lb_response
    char job_id [2];
    job_id [0]= lb_response.header[0];
    job_id [1]='\0';
    char server_ip[17];
    int port;
    int i;
    for(i=0;i<strlen(lb_response.payload);i++){
        if(lb_response.payload[i]==':'){
            strncpy(server_ip,lb_response.payload,i);
            server_ip[i]='\0';
            char port_c[6];
            strcpy(port_c,lb_response.payload+i+1);
            port_c[5]='\0';
            port=atoi(port_c);
            break;
        }
    }
    if(i==strlen(lb_response.payload)){
        printf("i got invalid addres parsing. deallocating and going to cry in the corner...\n");
        close(lb_sock);
        return;
    }
    printf("closing connection to LB\n");
    close(lb_sock);

    //connect to server
    int server_socket= socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in server_soc_Addr;
    server_soc_Addr.sin_family = AF_INET;
    server_soc_Addr.sin_port = htons(port);
    server_soc_Addr.sin_addr.s_addr = inet_addr(server_ip);
    res = connect(server_socket, (struct sockaddr*)&server_soc_Addr, sizeof(server_soc_Addr));
    printf("connect to server: %d\n", res);

    //send and receive 2 messages from server
    int message_count = 0;
    char message[]="whasup?";
    printf("my message is:%s\n",message);
    for(int j=0;j<MESSAGE_COUNT_TO_SERVER;j++){
        send_message(server_socket,CLIENT_JOB_MESSAGE,job_id,message,strlen(message));
        struct message* reply_ptr = get_message(lb_sock);
        if(reply_ptr == NULL){
            close(server_socket);
            free(reply_ptr);
            return;
        }
        struct message reply=*reply_ptr;
        free(reply_ptr);
        if(strcmp(reply.payload,message)==0){
            printf("i have mail! yay me!\n");
            sleep(1);
        }else{
            printf("i got wrong reply. committing seppuku\n");
            close(server_socket);
            return;
        }
    }
    char end_message[]="close";
    send_message(server_socket,CLIENT_JOB_MESSAGE,job_id,end_message,strlen(end_message));
    printf("i got my messages, now i can die happy\n");
    sleep(3);
    close(server_socket);
    return;
}
