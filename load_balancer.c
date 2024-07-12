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

//----------constants and global vars-----------------------------
//adresses
#define CLIENT_PORT 6500
#define SERVER_PORT 8700
#define MULTY_IP    "224.0.0.1"

// load balancer states
#define START 1
#define LISTENING 2

// server connection states
#define UPDATED 1
#define WAITING_FOR_UPDATE 2
#define LAST_CHANCE 3

/*----------------------------------------------------------------------------
                messages
LB job
    description: from a client connection thread to a server, attempting to assign a job
    header: type[1]capacity[1]job id[1]
    payload:

LB update
    description: sent from LB in multicast, asking for updates from all servers
    header: type[1]
    payload: 

server update
    description: sent from a server to LB, as registration message and after LB asked for an update
    header: type[1]capacity[1]
    payload: client welcome socket addrss[20](max)
LB ack
    description: LB to server, confirming registration and sending multicast addres to listen to
    header: type[1]
    payload: multicast address[15](max)

client req
    description: from client to LB to ask for a server to do a job
    header: type[1]capacity[1]
    payload:

job ack
    description: from server to LB to accept or reject a client job
    header: type[1] job id[1]
    payload: answer[1](1(accept)/0(rejected)

client req ack
    description: from LB to client, sent to tell client where server is at for the job asked
    
    header:type[1] job id[1]
    payload: client welcome socket addrss[20](max)

client job message
    description: from client to server and vice versa, telling server to do a job
    header: type[1] job id[1]
    payload: job[capacity*10](max)
----------------------------------------------------------------------------*/

//constants
#define MAX_SERVERS 5
#define MAX_CLIENT_RETRY 3
#define JOB_CONF_LEN 10
#define FIRST_MSSG_TIMEOUT 3

//scructs


struct server_data{
    int capacity;       //-1 indicates no server data is stored
    int comm_soc;
    char client_sock[20];   //<ip>:<port>
    int id;
    int updated;        
};
struct server_registry{
    struct server_data servers[MAX_SERVERS];
    int servers_num;
};
struct job_confirmation{
    int req_id;       //0 indicates data is stored
    int server_id;
    char client_sock[20];   //<ip>:<port>
    int answer;       //1 for accept and 0 for reject
    time_t time;        
};





//global vars
struct server_registry registerd_servers;   //struct for saving registerd servers info


struct job_confirmation job_confirmation_list[JOB_CONF_LEN]; //job confirmation list, a job confirmation that was not collected for 3 seconds will be trashed by main thread
int job_id=1;   //id to identify job reqs, can go up to 127
int server_id=1;

pthread_mutex_t registerd_servers_mutex[MAX_SERVERS];
pthread_mutex_t job_confirmation_list_mutex;
pthread_mutex_t job_id_mutex;
pthread_mutex_t server_id_mutex;
pthread_mutex_t servers_num_mutex;
//-------------------------------------------------------------------------

void server_connection(void* soc){
    int com_sock=*(int*)soc;
    printf("in server connection\n");
    struct message* register_mssg_ptr=get_message(com_sock);
    if(register_mssg_ptr==NULL){
        free(register_mssg_ptr);
        pthread_mutex_lock(&servers_num_mutex);
        registerd_servers.servers_num--;
        pthread_mutex_unlock(&servers_num_mutex);
        close(com_sock);
        return;
    }
    struct message register_mssg=*register_mssg_ptr;
    free(register_mssg_ptr);
    if(register_mssg.type!=SERVER_UPDATE){
        printf("got invalid register message of type %d,closing connection\n",register_mssg.type);
        close(com_sock);
        return;
    }
    char lb_ack_payload[15];
    strcpy(lb_ack_payload,MULTY_IP);
    send_message(com_sock,LB_ACK,"",lb_ack_payload,sizeof(lb_ack_payload));             //send LB ack to server with multicast addr
    
    struct server_data server;
    server.comm_soc=com_sock;
    
    server.capacity=register_mssg.header[0]-'0';
    printf("server cap is:%d\n",server.capacity);
    
    server.updated=1;
    pthread_mutex_lock(&server_id_mutex);
    server.id=server_id;
    server_id++;
    pthread_mutex_unlock(&server_id_mutex);
    strcpy(server.client_sock,register_mssg.payload);
    printf("server client_sock is:%s\n",server.client_sock);
    int i;
    for(i=0;i<MAX_SERVERS;i++){
        pthread_mutex_lock(&registerd_servers_mutex[i]);
        if(registerd_servers.servers[i].id==0){
            registerd_servers.servers[i]=server;
            pthread_mutex_unlock(&registerd_servers_mutex[i]);
            break;
        }
        pthread_mutex_unlock(&registerd_servers_mutex[i]);
    }
    
    if (i==MAX_SERVERS){             //all spots are taken, reject server
        close(com_sock);
        printf("server was rejected, max servers reached\n");
        return;
    }
    int server_index=i;
    time_t state_timer;
    int state=UPDATED;
    int running=1;
    int res;
    struct timeval tv;
    tv.tv_sec = 1;
    tv.tv_usec = 0;
    fd_set server_rec,server_rec_loop;
    FD_ZERO(&server_rec);
    FD_SET(server.comm_soc,&server_rec);
    //FD_ZERO(&server_rec);
    printf("server of id %d was registered\n",server.id);
    while (running)
    {   server_rec_loop=server_rec;
        
        
        select(server.comm_soc+1,&server_rec_loop,NULL,NULL,&tv);
        //printf("melm %d\n",FD_ISSET(com_sock,&server_rec));
        tv.tv_sec=1;
        if(FD_ISSET(com_sock,&server_rec_loop)){
            printf("LB got message from server %d\n",server.id); 
            struct message* new_message_ptr=get_message(com_sock);
            if(new_message_ptr!=NULL){
                struct message new_message=*new_message_ptr;
                free(new_message_ptr);
                //printf("message type %d\n",new_message.type); 
                switch (new_message.type)
                {
                case SERVER_UPDATE:
                    pthread_mutex_lock(&registerd_servers_mutex[server_index]);
                    registerd_servers.servers[server_index].capacity=(int)new_message.header[0]-'0';
                    registerd_servers.servers[server_index].updated=1;
                    strcpy(registerd_servers.servers[server_index].client_sock,new_message.payload);
                    pthread_mutex_unlock(&registerd_servers_mutex[server_index]);
                    printf("got update message from server %d\n",server.id);
                    state=UPDATED;
                    break;
                
                case JOB_ACK:
                    printf("got job ack!!\n");
                    pthread_mutex_lock(&job_confirmation_list_mutex);
                    for(i=0;i<JOB_CONF_LEN;i++){
                        if(job_confirmation_list[i].req_id==0){
                            job_confirmation_list[i].req_id=new_message.header[0]-'0';
                            job_confirmation_list[i].server_id=server.id;
                            job_confirmation_list[i].answer=new_message.payload[0]-'0';
                            job_confirmation_list[i].time=time(NULL);
                            pthread_mutex_lock(&registerd_servers_mutex[server_index]);
                            strcpy(job_confirmation_list[i].client_sock,registerd_servers.servers[server_index].client_sock);
                            pthread_mutex_unlock(&registerd_servers_mutex[server_index]);
                            printf("i put it in id %d\n",i);
                            printf("the answer is %d\n",new_message.payload[0]-'0');
                            break;
                        }
                    }
                    if(i==JOB_CONF_LEN){
                        printf("job confirmation list is full, a conformation was discarded\n");
                    }
                    pthread_mutex_unlock(&job_confirmation_list_mutex);
                    break;

                default:
                    printf("got unknown message, type:%d\n",new_message.type);
                    break;
                }
            }else
            {   free(new_message_ptr);
                printf("lol its Null\n");
                running=0;
            }
        }

        switch (state)
        {
        case UPDATED:
            if(registerd_servers.servers[server_index].updated==0){
                printf("server %d is going to WAITING_FOR_UPDATE \n",server.id);
                state=WAITING_FOR_UPDATE;
                state_timer=time(NULL);
            }
            break;

        case WAITING_FOR_UPDATE:
            if(registerd_servers.servers[server_index].updated==0 &&time(NULL)-state_timer>5){
                char message_payload[]="send me an update";
                res=send_message(com_sock,LB_UPDATE,"",message_payload,strlen(message_payload));
                if (res==-1){
                    printf("failed to send LB update from server thread\n");
                }
                printf("server %d is going to LAST_CHANCE \n",server.id);
                state=LAST_CHANCE;
                state_timer=time(NULL);

            }
            break;

        case LAST_CHANCE:
            if(registerd_servers.servers[server_index].updated==0 &&time(NULL)-state_timer>5){
                printf("server %d missed LAST_CHANCE closing connection \n",server.id);
                running=0;
            }
            break;
        
        }
            
    }
    pthread_mutex_lock(&registerd_servers_mutex[server_index]);
    registerd_servers.servers[server_index].capacity=-1; //negative capacity to indicate no server registered in that spot
    registerd_servers.servers[server_index].updated=0;
    registerd_servers.servers[server_index].id=0;
    pthread_mutex_unlock(&registerd_servers_mutex[server_index]);
    pthread_mutex_lock(&servers_num_mutex);
    registerd_servers.servers_num--;
    pthread_mutex_unlock(&servers_num_mutex);
    close(server.comm_soc);
    printf("server %d is dead \n",server.id);
    pthread_exit(0);
    return;
};

int not_tried(int servers_tried[],int id){      //used in client_connection for readabilty
    int i;
    for(i=0;i<MAX_CLIENT_RETRY;i++){
        if(servers_tried[i]==id){
            return 0;   //false
        }
    }
    return 1; //true
}

void client_connection( void* soc){          
    int com_sock=*(int*)soc;
    struct message* register_mssg_ptr=get_message(com_sock);
    if(register_mssg_ptr==NULL){
        free(register_mssg_ptr);
        close(com_sock);
        return;
    }
    
    struct message register_mssg=*register_mssg_ptr;
    free(register_mssg_ptr);
    if (register_mssg.type!=(char)CLIENT_REQ){
        printf("got invalid type\n");
        close(com_sock);
        return;
    }
    int capacity=register_mssg.header[0]-'0';
    printf("got capacity of:%d\n",capacity); 
    if (capacity<=0){
        printf("got invalid capacity\n");
        close(com_sock);
        return;
    }
    int server_cap=0;
    int server_index=-1;
    int i,j;
    pthread_mutex_lock(&job_id_mutex);
    int my_job_id=job_id;
    job_id++;
    pthread_mutex_unlock(&job_id_mutex);
    time_t timer;
    int servers_tried[MAX_CLIENT_RETRY];
    for(j=0;j<MAX_CLIENT_RETRY;j++){                //each loop is for trying diffrent server
        server_cap=0;
        server_index=-1;
        for(i=0;i<MAX_SERVERS;i++){             //find next server to ask to do job
            printf("%d\n",server_cap<registerd_servers.servers[i].capacity);
            if(server_cap<registerd_servers.servers[i].capacity && not_tried(servers_tried,registerd_servers.servers[i].id)){
                printf("got a server\n");
                server_cap=registerd_servers.servers[i].capacity;
                server_id=registerd_servers.servers[i].id;
                server_index=i;
            }
    }   printf("server_cap:%d server_index:%d server_id:%d\n",server_cap,server_index,server_id);
        if(server_cap<capacity || server_index==-1){
            printf("no valid server found\n");
            close(com_sock);
            return;
    }
        servers_tried[j]=server_id;
        char payload[]="please do this job";
        int res;
        int not_rejected=1;
        char header[LB_JOB_H_LEN];
        char capacity_c[2],my_job_id_c[2];
        sprintf(capacity_c,"%d",capacity);
        sprintf(my_job_id_c,"%d",my_job_id);
        strcpy(header,capacity_c);
        strcat(header,my_job_id_c);
        my_job_id_c[1]='\0';
        capacity_c[1]='\0';
        printf("im sending job of number:%d %s\n",my_job_id,my_job_id_c);
        res=send_message(registerd_servers.servers[server_index].comm_soc,LB_JOB,header,payload,sizeof(payload));
        if(res==-1){
            printf("couldnt contact server, trying diffrent one\n");
            not_rejected=0;
        }
        timer=time(NULL);
        
        while(time(NULL)-timer<5 && not_rejected){
            usleep(100000); //sleep 0.1 sec
            for(int j=0;j<JOB_CONF_LEN;j++){
                if(job_confirmation_list[j].req_id==my_job_id){
                    printf("found my job id in conformation list\n");
                    if(job_confirmation_list[j].answer==1){
                        char payload[20];   //contains servers welcome socket address
                        strcpy(payload,job_confirmation_list[j].client_sock);
                        payload[strlen(job_confirmation_list[j].client_sock)]='\0';
                        printf("the socket is:%s\n",payload);
                        printf("im sending job of number:%c\n",my_job_id_c[0]);
                        res=send_message(com_sock,CLIENT_REQ_ACK,my_job_id_c,payload,sizeof(payload));
                        if(res==-1){
                            printf("couldnt contact client, close connection\n");
                            close(com_sock);
                            return;
                        }
                        pthread_mutex_lock(&job_confirmation_list_mutex);
                        strcat(job_confirmation_list[j].client_sock,"");
                        job_confirmation_list[j].req_id=0;
                        job_confirmation_list[j].server_id=0;
                        job_confirmation_list[j].time=0;
                        pthread_mutex_unlock(&job_confirmation_list_mutex);
                        printf("job number %d was handeld\n",my_job_id);
                        close(com_sock); 
                        return;
                    }else if (job_confirmation_list[j].answer==0)
                    {
                        not_rejected=0;             //got rejection, try new server
                        pthread_mutex_lock(&job_confirmation_list_mutex);
                        strcat(job_confirmation_list[j].client_sock,"");
                        job_confirmation_list[j].req_id=0;
                        job_confirmation_list[j].server_id=0;
                        job_confirmation_list[j].time=0;
                        pthread_mutex_unlock(&job_confirmation_list_mutex);
                        printf("server %d rejected job %d \n",my_job_id,registerd_servers.servers[server_index].id);
                    }
                    
                    
                }
            }
            
        }
    }
    printf("cant find server for job %d, rejecting job\n",job_id);
    close(com_sock);
    return;
}


int main() {
    fd_set welcome_sockets,welcome_sockets_loop;
    int res;
    int client_welcom_socket = socket(AF_INET, SOCK_STREAM, 0);
    assert (client_welcom_socket != -1);
    struct sockaddr_in client_soc_Addr;
    client_soc_Addr.sin_family = AF_INET;
    client_soc_Addr.sin_port = htons(CLIENT_PORT);
    client_soc_Addr.sin_addr.s_addr = INADDR_ANY;
    res=bind(client_welcom_socket, (struct sockaddr*)&client_soc_Addr , sizeof(client_soc_Addr));
    listen(client_welcom_socket,5);

    int server_welcom_socket= socket(AF_INET, SOCK_STREAM, 0);
    assert (server_welcom_socket != -1);
    struct sockaddr_in server_soc_Addr;
    server_soc_Addr.sin_family = AF_INET;
    server_soc_Addr.sin_port = htons(SERVER_PORT);
    server_soc_Addr.sin_addr.s_addr = INADDR_ANY;
    printf("ip%d\n",server_soc_Addr.sin_addr.s_addr);
    res=bind(server_welcom_socket, (struct sockaddr*)&server_soc_Addr , sizeof(server_soc_Addr));
    listen(server_welcom_socket,5);
    FD_ZERO(&welcome_sockets);
    FD_SET(client_welcom_socket,&welcome_sockets);

    FD_SET(server_welcom_socket,&welcome_sockets);
    //create multicast port
    time_t update_timer;
    time_t job_conf_clean_timer;
    struct timeval select_timeout;
    select_timeout.tv_sec=1;
    select_timeout.tv_usec=0;
    int max=client_welcom_socket;
    if(client_welcom_socket<server_welcom_socket){
        max=server_welcom_socket;
    }
    int state=START;
    int running=1;
    int i;

    char ip[INET_ADDRSTRLEN];

    while (running)
    {
        switch (state)
        {
        case START:
            registerd_servers.servers_num=0;
            for(i=0;i<MAX_SERVERS;i++){
                registerd_servers.servers[i].capacity=-1; //negative capacity to indicate no server registered in that spot
                registerd_servers.servers[i].updated=0;
                registerd_servers.servers[i].id=0;
                if (pthread_mutex_init(&registerd_servers_mutex[i],NULL)!=0){       
                printf("failed to init registerd_servers_mutex in index %d ",i);
                }
            }
            for(i=0;i<JOB_CONF_LEN;i++){
                    job_confirmation_list[i].req_id=0;
                    job_confirmation_list[i].server_id=0;
                    job_confirmation_list[i].time=0;
                }

            if (pthread_mutex_init(&servers_num_mutex,NULL)!=0){      
                printf("failed to init servers_num_mutex");
            }
            if (pthread_mutex_init(&job_confirmation_list_mutex,NULL)!=0){
                printf("failed to init job_confirmation_list_mutex");
            }
            if (pthread_mutex_init(&job_id_mutex,NULL)!=0){
                printf("failed to init job_id_mutex");
            }
            if (pthread_mutex_init(&server_id_mutex,NULL)!=0){
                printf("failed to init server_id_mutex");
            }
            update_timer=time(NULL);
            job_conf_clean_timer= time(NULL);
            state=LISTENING;

        case LISTENING:
            welcome_sockets_loop=welcome_sockets;
            //FD_ZERO(&welcome_sockets_loop);
            // FD_SET(client_welcom_socket,&welcome_sockets);
            // FD_SET(server_welcom_socket,&welcome_sockets);

            select(max+1,&welcome_sockets_loop,NULL,NULL,&select_timeout);       //maby neet to change to max
            select_timeout.tv_sec=1;
            if(FD_ISSET(server_welcom_socket,&welcome_sockets_loop)){
                socklen_t addr_len;             
                addr_len = sizeof(server_soc_Addr);
                int new_server_sock=accept(server_welcom_socket,(struct sockaddr*)&server_soc_Addr , &addr_len);
                if(registerd_servers.servers_num<MAX_SERVERS){
                    struct timeval tv;
                    tv.tv_sec = 15;
                    tv.tv_usec = 0;
                    setsockopt(new_server_sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof tv);
                    pthread_mutex_lock(&servers_num_mutex);
                    registerd_servers.servers_num++;
                    pthread_mutex_unlock(&servers_num_mutex);
                    pthread_t server_thread;
                    pthread_create(&server_thread,NULL,(void*)&server_connection,(void*) &new_server_sock);
                }
                else{
                    printf("main thread rejected a server, max server reached");
                    close(new_server_sock);
                }
            }

            if(FD_ISSET(client_welcom_socket,&welcome_sockets_loop)){
                socklen_t addr_len;              //need to make sure that returns num of waiting and not 0 for sucsses in non blocking
                addr_len = sizeof(client_soc_Addr);
                int new_client=accept(client_welcom_socket,(struct sockaddr*)&client_soc_Addr , &addr_len);
                struct timeval tv;
                tv.tv_sec = 15;
                tv.tv_usec = 0;
                setsockopt(new_client, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof tv);
                pthread_t client_thread;
                pthread_create(&client_thread,NULL,(void*)&client_connection,(void*) &new_client);
                }
            
            if (time(NULL)-update_timer>=5){
                printf("sending multicast update req\n");
                for(i=0;i<MAX_SERVERS;i++){
                    registerd_servers.servers[i].updated=0;
                }
                //send multicast update requst
                update_timer=time(NULL);
            }
            if (time(NULL)-job_conf_clean_timer>=5){            //put back to 1 
                printf("cleaning up job conformation list\n");
                pthread_mutex_lock(&job_confirmation_list_mutex);
                for(i=0;i<JOB_CONF_LEN;i++){
                    if(job_confirmation_list[i].req_id!=0 && time(NULL)-job_confirmation_list[i].time>5){
                        job_confirmation_list[i].req_id=0;
                        job_confirmation_list[i].server_id=0;
                        job_confirmation_list[i].time=0;
                    }
                }
                pthread_mutex_unlock(&job_confirmation_list_mutex);
                job_conf_clean_timer=time(NULL);   

            }
        }
    }
    return 0;
}


