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


#define BASE_PORT 1200
#define LB_IP "127.0.0.1"             //change to fit for each run
#define LB_PORT 6500

int send_message(int sock, int type, char* header, char* payload, int payload_len);
struct message* get_message(int soc);


/*
connect to LB - connect()
send message to LB - client request
receive client request ack message - has server to connect to 
close connection to LB
connect to said server 
send message to server and receive 2-3 times
disconnect

*/



void client(int start_data[]){
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
    struct sockaddr_in lb_soc_Addr;                         //LB unicast socket setup
    lb_soc_Addr.sin_family = AF_INET;
    lb_soc_Addr.sin_port = htons(BASE_PORT);
    lb_soc_Addr.sin_addr.s_addr = inet_addr(ip);
    res=bind(lb_sock, (struct sockaddr*)&lb_soc_Addr , sizeof(lb_soc_Addr));
    printf("bind client to LB socket:%d\n",res);

    //create socket for client-server connection and bind it
    int server_sock;
    server_sock=createWelcomeSocket(BASE_PORT+2,3);
    printf("bind client to server socket:%d\n",server_sock);


    //connect client to LB
    struct sockaddr_in lb_soc_Addr;                             
    lb_soc_Addr.sin_family = AF_INET;
    lb_soc_Addr.sin_port = htons(LB_PORT);
    lb_soc_Addr.sin_addr.s_addr = inet_addr(LB_IP);
    //lb_soc_Addr.sin_addr.s_addr = INADDR_ANY; 
    res=connect(lb_sock, (struct sockaddr*)&lb_soc_Addr , sizeof(lb_soc_Addr));
    
    //creating message to send to LB
    printf("connecting to LB: %d\n", res);
    int random_job_weight = (rand()%10)+1;
    char temp_header[1];
    sprintf(temp_header,"%d",random_job_weight);
    send_message(lb_sock, CLIENT_REQ, temp_header, "", 0);

    //receive message from LB
    struct message* lb_response_ptr = get_message(lb_sock);
    printf("got LB ack");
    if(lb_response_ptr == NULL){
        close(lb_sock);
        //multicast
        clost(server_sock);
        return;
    }
    struct message lb_response = *lb_response_ptr;
    if (lb_response.type != CLIENT_REQ_ACK){
        printf("got invalid message type ,closing client\n");
        close(lb_sock);
        //multicast

        close(server_sock);
        return;
    }
    printf("got valid ack message from LB: %s", lb_response.payload);

    //parse data from lb_response
    char job_id = lb_response.header[0];
    char server_ip_addr[15] = lb_response.payload[];

    //close connection to LB
    printf("closing connection to LB");
    close(lb_sock);

    //connect to server




    




    sleep(1);
    return;
}

int main(){

}



    
}