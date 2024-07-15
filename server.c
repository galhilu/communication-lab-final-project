
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


#define LB_PORT 8700

int send_message(int sock,int type,char* header,char* payload,int payload_len);
struct message* get_message(int soc);

struct job{
    int job_id;
    time_t timestamp;
    int capacity;
};

pthread_mutex_t multi_mssg_pending_mutex;
int multi_mssg_pending=0; // 0 for no message and 1 for received message

void multicast_handler(char * multi_addr){
    struct address* address=address_parsing(multi_addr);
    //int multicast_rec = socket(AF_INET, SOCK_DGRAM, 0);              //create multicast
    int multicast_rec;
    if ((multicast_rec = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        perror("socket");
        exit(1);
    }
    struct sockaddr_in multicast_rec_addr,group_addr;
    struct ip_mreq group;
    
   

    multicast_rec_addr.sin_family = AF_INET;
    multicast_rec_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    multicast_rec_addr.sin_port = htons(address->port);
    
    
    //setsockopt(multicast_rec, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    int opt = 1;
    if (setsockopt(multicast_rec, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("setsockopt");
        close(multicast_rec);
        exit(1);
    }
    bind(multicast_rec, (struct sockaddr*)&multicast_rec_addr, sizeof(multicast_rec_addr));
    
    group.imr_multiaddr.s_addr = inet_addr(address->ip);
    group.imr_interface.s_addr = htonl(INADDR_ANY);
    setsockopt(multicast_rec, IPPROTO_IP, IP_ADD_MEMBERSHIP, (char *)&group, sizeof(group));
    //printf("setsockopt:%d\n",res);
    char buffer[1024];
    int addrlen;

    char message_buff[3];
    int res;
    while (1) {

        int nbytes = recvfrom(multicast_rec, message_buff, sizeof(message_buff), 0, (struct sockaddr*)&group_addr, &addrlen);
        if(message_buff[1]-'0'==LB_UPDATE && multi_mssg_pending==0){
            printf("flag up\n");
            pthread_mutex_lock(&multi_mssg_pending_mutex);
            multi_mssg_pending=1;
            pthread_mutex_unlock(&multi_mssg_pending_mutex);
        }

        printf("Received message: %s\n", message_buff);
    }
    return;

}






int main(int argc, char *argv[]){
    if(argc!=4){
        printf("arguments are: LB ip, capacity(one digit), local port (will also use local port+1)\n");
        return -1;
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
    struct ifaddrs *ifaddr, *ifa;
    char ip[INET_ADDRSTRLEN];
    strncpy(ip,get_my_ip(),INET_ADDRSTRLEN);
    printf("server IP is: %s\n", ip);

    int lb_sock= socket(AF_INET, SOCK_STREAM, 0);
    assert (lb_sock != -1);
    struct sockaddr_in server_soc_Addr;                         //LB unicast socket setup
    server_soc_Addr.sin_family = AF_INET;
    server_soc_Addr.sin_port = htons(base_port);
    server_soc_Addr.sin_addr.s_addr = inet_addr(ip);
    res=bind(lb_sock, (struct sockaddr*)&server_soc_Addr , sizeof(server_soc_Addr));
    if(res<0){
        printf("bind server failed\n");
        return 0;
    }
    int opt = 1;
    setsockopt(lb_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    int client_welcome_sock;
    client_welcome_sock=createWelcomeSocket(base_port+1,3);
    printf("bind client socket:%d\n",client_welcome_sock);
    
    struct sockaddr_in lb_soc_Addr;                             
    lb_soc_Addr.sin_family = AF_INET;
    lb_soc_Addr.sin_port = htons(LB_PORT);
    lb_soc_Addr.sin_addr.s_addr = inet_addr(lb_ip);
    //lb_soc_Addr.sin_addr.s_addr = INADDR_ANY; 
    res=connect(lb_sock, (struct sockaddr*)&lb_soc_Addr , sizeof(lb_soc_Addr));     //connect to lb
    if(res<0){
        printf("connection failed\n");
        return 0;
    }
    char client_sock_addr[20];
    strcpy(client_sock_addr,ip);
    strcat(client_sock_addr,":");                                           //make registration message and send to LB
    sprintf(client_sock_addr+strlen(ip)+1,"%d",base_port+1);

    char capacity_c[2];   //is 2 only to stop warnings, will always be 1 char long
    sprintf(capacity_c,"%d",capacity);
    capacity_c[1]='\0';

    send_message(lb_sock,SERVER_UPDATE,&capacity_c[0],client_sock_addr,strlen(client_sock_addr));
    struct message* lb_response_ptr=get_message(lb_sock);       //wait for LB ack
    printf("got LB ack\n");
    if(lb_response_ptr==NULL){
        free(lb_response_ptr);
        close(lb_sock);
        close(client_welcome_sock);
        return 0;
    }
    
    struct message lb_response=*lb_response_ptr;
    free(lb_response_ptr);
    if(lb_response.type!=LB_ACK){
        printf("got invalid message type ,closing server\n");
        close(lb_sock);
        close(client_welcome_sock);
        return 0;
    }

    printf("got valid ack message from LB: %s\n",lb_response.payload);

    if (pthread_mutex_init(&multi_mssg_pending_mutex,NULL)!=0){      
        printf("failed to init multi_mssg_pending_mutex\n");
    }
    
    pthread_t multicast_thread;
    pthread_create(&multicast_thread,NULL,(void*)&multicast_handler,lb_response.payload);

    struct timeval select_timeout;
    select_timeout.tv_sec=1;
    select_timeout.tv_usec=0;
    
    fd_set server_sockets,server_sockets_loop;
    FD_ZERO(&server_sockets);
    FD_SET(lb_sock,&server_sockets);
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
        select(max_fd+1,&server_sockets_loop,NULL,NULL,&select_timeout);       //maby neet to change to max
        select_timeout.tv_sec=1;

        if(FD_ISSET(lb_sock,&server_sockets_loop)){
            struct message* new_message_ptr=get_message(lb_sock);
            if(new_message_ptr!=NULL){
                struct message new_message=*new_message_ptr;
                free(new_message_ptr);
                printf("server got message from LB of type %d\n",new_message.type); 
                switch (new_message.type)
                {
                    case LB_UPDATE:
                        sprintf(capacity_c,"%d",capacity);
                        send_message(lb_sock,SERVER_UPDATE,&capacity_c[0],client_sock_addr,strlen(client_sock_addr));
                        break;
                    case LB_JOB:
                    printf("got job message\n");
                        char job_cap=new_message.header[0];
                        printf("the asked cap is %c\n",job_cap);
                        if(job_cap<'0'||job_cap>'9'){
                            printf("got invalid capacity for job: %c discarding",job_cap);
                        }
                        else{
                            char job_id[2]; //job id
                            job_id[0]=new_message.header[1];
                            job_id[1]='\0';
                            //printf("the id is %c\n",job_id);
                            char job_ans[2];
                            if(job_cap<capacity+'0'){
                                printf("good cap\n");
                                int i;
                                for( i=0;i<5;i++){       //set aside a place for client
                                    if(client_jobs[i].job_id==-1){
                                        printf("found a spot in %d\n",i);
                                        client_jobs[i].job_id=new_message.header[1]-'0';
                                        client_jobs[i].timestamp=time(NULL);
                                        client_jobs[i].capacity=job_cap-'0';
                                        job_ans[0]='1'; //accept
                                        capacity=capacity-(job_cap-'0');
                                        printf("my new capacity is:%d\n",capacity);
                                        break;
                                    }
                                }
                                if(i==5){
                                    printf("too many clients\n");
                                    job_ans[0]='0'; //reject
                                }

                            }else{
                                printf("too high cap\n");
                                job_ans[0]='0'; //reject
                            }
                            job_ans[1]='\0';
                            printf("my answer is %s\n",job_ans);
                            send_message(lb_sock,JOB_ACK,job_id,job_ans,strlen(job_ans));
                        }   
                        break;
                }
            }
            else{
                free(new_message_ptr);
                printf("connection with LB terminated, shuting down...\n");
                //dealocs
                return 0;
            }
        }
        if(FD_ISSET(client_welcome_sock,&server_sockets_loop)){
            printf("got first message from a client!!!\n");
            int new_client=accept(client_welcome_sock,NULL , NULL);             
            struct message* new_message_ptr=get_message(new_client);   //need to add timeout
            if(new_message_ptr!=NULL){
                struct message new_message=*new_message_ptr;
                int new_client_job_id=new_message.header[0]-'0';
                int i;
                if(new_message.type==CLIENT_JOB_MESSAGE){
                    for(i=0;i<5;i++){
                        if (client_jobs[i].job_id==new_client_job_id){
                            clients[i]=new_client;
                            break;
                        }
                    }
                    if(i==5){           //see if client is on the list
                        printf("unregistered client! you shall not pass!\n");
                        close(new_client);
                        continue;
                    }
                    if(client_jobs[i].capacity*10<new_message.payload_len){
                        printf("client's message excceded allocated capacity. bad client! right to jail! %d\n",new_message.payload_len);
                        printf("max len is %d \n",client_jobs[i].capacity*10);
                        close(clients[i]);
                        client_jobs[i].job_id=-1;
                        capacity=capacity+client_jobs[i].capacity;
                        continue;
                    }
                    FD_SET(new_client,&server_sockets); //add to fd set
                    
                    if(max_fd<new_client){              //update max fd
                        max_fd=new_client;
                    }
                    printf("sending back to client\n");
                    send_message(new_client,CLIENT_JOB_MESSAGE,new_message.header,new_message.payload,new_message.payload_len);
                }
                else{
                    printf("got invalid client message type\n");
                }
            }
            free(new_message_ptr);
        }

        for(int j=0;j<5;j++){       //set aside a place for client
            if(clients[j]!=-1){
                if(FD_ISSET(clients[j],&server_sockets_loop)){
                    //printf("got more client messages\n");
                    struct message* new_message_ptr=get_message(clients[j]);
                    if(new_message_ptr!=NULL){
                        struct message new_message=*new_message_ptr;
                        if(new_message.type==CLIENT_JOB_MESSAGE){
                            client_jobs[j].timestamp=time(NULL);
                            if(client_jobs[j].capacity*10<new_message.payload_len){
                                printf("client's message excceded allocated capacity. bad client! right to jail! %d\n",new_message.payload_len);
                                printf("ok len is %d max\n",client_jobs[j].capacity*10);
                                close(clients[j]);
                                client_jobs[j].job_id=-1;
                                capacity=capacity+client_jobs[j].capacity;
                                break;
                            }
                            if(strcmp(new_message.payload,"close")==0){
                                printf("client is done, closing connection\n");
                                close(clients[j]);
                                client_jobs[j].job_id=-1;
                                capacity=capacity+client_jobs[j].capacity;
                                FD_CLR(clients[j],&server_sockets);
                                break;
                            }else{
                                send_message(clients[j],CLIENT_JOB_MESSAGE,new_message.header,new_message.payload,new_message.payload_len);
                            }
                        
                        }
                        else{
                        printf("got invalid client message type\n");
                        }
                    }
                    free(new_message_ptr);
                }
            }
        }

        for(int i=0;i<5;i++){       //deallocate capacity for clients that didnt make contact in 5 secondes
            if (client_jobs[i].job_id!=-1 && time(NULL)-client_jobs[i].timestamp>5){
                client_jobs[i].job_id=-1;
                capacity=capacity+client_jobs[i].capacity;
            }
        }
        if(multi_mssg_pending==1){
            sprintf(capacity_c,"%d",capacity);
            send_message(lb_sock,SERVER_UPDATE,&capacity_c[0],client_sock_addr,strlen(client_sock_addr));
            pthread_mutex_lock(&multi_mssg_pending_mutex);
            multi_mssg_pending=0;
            pthread_mutex_unlock(&multi_mssg_pending_mutex);
            printf("sent update from multi\n");
        }
    }


    return 0;
}