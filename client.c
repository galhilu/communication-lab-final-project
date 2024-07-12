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


#define LB_PORT 6500
#define MESSAGE_COUNT_TO_SERVER 10

int send_message(int sock, int type, char* header, char* payload, int payload_len);
void splitIpPort(const char *input, char *ip_address, int *port);
struct message* get_message(int soc);




void main(int argc, char *argv[]){
    if(argc!=4){
        printf("arguments are: LB ip, capacity(one digit), local port\n");
        return;
    }
    char lb_ip[16];
    strncpy(lb_ip,argv[1],15);
    lb_ip[15]=
    printf("lb_ip%s\n",lb_ip);
    int capacity=atoi(argv[2]);
    int base_port=atoi(argv[3]);
    printf("cap:%d\n",capacity);
    printf("base_port%d\n",base_port);
    int res;

    
    char ip[INET_ADDRSTRLEN];               // get ip
    strncpy(ip,get_my_ip(),INET_ADDRSTRLEN);
    printf("client IP is: %s\n", ip);

    
    int lb_sock = socket(AF_INET, SOCK_STREAM, 0);
    assert(lb_sock != -1);
    struct sockaddr_in soc_Addr;                         //LB socket setup
    soc_Addr.sin_family = AF_INET;
    soc_Addr.sin_port = htons(base_port);
    soc_Addr.sin_addr.s_addr = inet_addr(ip);
    res=bind(lb_sock, (struct sockaddr*)&soc_Addr , sizeof(soc_Addr));
    printf("bind client to LB socket:%d\n",res);

   
    struct sockaddr_in lb_soc_Addr;                             
    lb_soc_Addr.sin_family = AF_INET;
    lb_soc_Addr.sin_port = htons(LB_PORT);               //connect to LB
    lb_soc_Addr.sin_addr.s_addr = inet_addr(lb_ip);
    res=connect(lb_sock, (struct sockaddr*)&lb_soc_Addr , sizeof(lb_soc_Addr));
    
    
    printf("my capacity is: %d\n",capacity);        //creating message to send to LB
    char header_c[2];
    sprintf(header_c,"%d",capacity);
    char pyload[]="me client! you give me server";
    send_message(lb_sock, CLIENT_REQ, header_c, pyload, strlen(pyload));

    
    struct message* lb_response_ptr = get_message(lb_sock); //receive message from LB
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

    
    char job_id [2];                    //parse data from lb_response
    job_id [0]= lb_response.header[0];
    job_id [1]='\0';

    struct address* address=address_parsing(lb_response.payload);
    printf("closing connection to LB\n");
    close(lb_sock);

    
    int server_socket= socket(AF_INET, SOCK_STREAM, 0);             //connect to server
    struct sockaddr_in server_soc_Addr;
    server_soc_Addr.sin_family = AF_INET;
    server_soc_Addr.sin_port = htons(address->port);
    server_soc_Addr.sin_addr.s_addr = inet_addr(address->ip);
    res = connect(server_socket, (struct sockaddr*)&server_soc_Addr, sizeof(server_soc_Addr));
    printf("connect to server: %d\n", res);
    free(address);

    int message_count = 0;
    char message[]="whasup?";
    printf("my message is:%s\n",message);
    for(int j=0;j<MESSAGE_COUNT_TO_SERVER;j++){                         //send messgae to server MESSAGE_COUNT_TO_SERVER times and close
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
