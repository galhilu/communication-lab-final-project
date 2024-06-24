#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <sys/socket.h>
#include <netinet/in.h> 
//#include <arpa/inet.h>
#include <assert.h>
#include <string.h>
#include <pthread.h>

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
#define WAITING_FOR UPDATE 2
#define LAST_CHANCE 3


//message types
#define LB_JOB 0
#define LB_UPDATE 1
#define LB_ACK 2
#define CLIENT_REQ 3

//message header length
#define LB_JOB_H_LEN 1
#define LB_UPDATE_H_LEN 2
#define LB_ACK_H_LEN 3




//constants
#define MAX_SERVERS 5

//global vars
struct server_registry registerd_servers;   //struct for saving registerd servers info
int state=START;                            //load balancer state indicator

int header_length[]={LB_JOB_H_LEN,LB_UPDATE_H_LEN,LB_ACK_H_LEN}; //header_length[message type]= header length of that message type

int job_confirmation_list[10][2]={{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0}}; //job confirmation is [job id,accepting server id]
int job_id=0;

//-------------------------------------------------------------------------
struct server_registry{
    struct server_data servers[MAX_SERVERS];
    int servers_num;
};

struct server_data{
    int capacity;       //-1 indicates no server data is stored
    int comm_soc;
    int updated;        
};

void server_connection(int com_sock){  
    //receive first server register message with time out
    //if timeout
        //close connection
        //server num--
        //exit thread
    //else
        //parse message
        //add data to registerd_servers in lowst vacant index in  the array
        //set timer
        //set thread state to updated
    while (1)
    {
        //wait for incoming message
        //parse
        //handle with switch case of the message type
        // if state== updated && server updated flag is down
            //set state to waiting for update
            //set timer
        //if state==waiting for update && 5 seconds passed
            //send last chance message
            //set state to last chance
            //set timer
        //if state==last chance && 5 seconds passed
            //close connection
            //capacity=-1
            //server num --
            //break
    }
    
    return;
};

void client_connection(int com_sock){                   //need to solve problem of how to get server job ack message without server receiver thread geting it (or if receiver gets it how to inform this thread) 
    char buff [2];
    int res;
    res=recv(com_sock,buff,sizeof(char)*2,0);       //need to add timeout
    if (res==-1){
        printf("got invalid recv");
        return;
    }
    if (buff[1]!=(char)CLIENT_REQ){
    printf("got invalid type");
    return;
    }
    char* message=(char*)malloc((buff[0]) * sizeof(char));
    res=recv(com_sock,message,sizeof(message),0);
    if (res==-1){
        printf("got invalid recv");
        free(message);
        return;
    }
    int capacity=message[0];
    if (capacity<=0){
        printf("got invalid capacity");
        free(message);
        return;
    }
    int server_cap=0;
    int server_index=-1;
    int i;
    for(i=0;i<MAX_SERVERS;i++){
        if(server_cap<registerd_servers.servers[i].capacity){
            server_cap=registerd_servers.servers[i].capacity;
            server_index=i;
        }
    }
    if(server_cap<capacity || server_index==-1){
        printf("no valid server found");
        free(message);
        return;
    }
    //if timeout
        //close connection
    //find least loaded server
    //if not enough capacity
        //close connection
    //send server job message
    //send client allocated server
    //close connection 
}

char* make_message(int type,char* payload,int payload_len){
    char* header=(char*)malloc(sizeof(char)*header_length[type]);

    switch (type)               //construct header
    {
    case LB_JOB:
        *header=(char)(type);          
        break;

    case LB_UPDATE:
        *header=(char)(type);
        break;

     case LB_ACK:
        *header=(char)(type);
        break;   
    default:
        break;
    }

    int len=sizeof(header)+sizeof(char)*payload_len;
    
    if (len>127){       //max len is 127 (can fit in char)
        printf("message length exeeded, max len is 127byte tried to send:%dbytes\n",len);
        free(header);
        return NULL;
    }
    char* message=(char*)malloc((header_length[type]+payload_len+1) * sizeof(char));    //extra char for message length (excluding the size of the length indicator)
    strcpy(message,(char)len);
    strcat(message,header);
    strcat(message,payload);
    free(header);
    return message;

    

}


int main() {
    int client_welcom_socket = socket(AF_INET, SOCK_STREAM|SOCK_NONBLOCK, 0);
    assert (client_welcom_socket != -1);
    struct sockaddr_in client_soc_Addr;
    client_soc_Addr.sin_family = AF_INET;
    client_soc_Addr.sin_port = htons(CLIENT_PORT);
    client_soc_Addr.sin_addr.s_addr = INADDR_ANY;
    bind(client_welcom_socket, (struct sockaddr*)&client_soc_Addr , sizeof(client_soc_Addr));


    int server_welcom_socket= socket(AF_INET, SOCK_STREAM|SOCK_NONBLOCK, 0);
    assert (server_welcom_socket != -1);
    struct sockaddr_in server_soc_Addr;
    server_soc_Addr.sin_family = AF_INET;
    server_soc_Addr.sin_port = htons(SERVER_PORT);
    server_soc_Addr.sin_addr.s_addr = INADDR_ANY;
    bind(server_welcom_socket, (struct sockaddr*)&server_soc_Addr , sizeof(server_soc_Addr));
    
    //create multicast port
    time_t timer; //load balancer timer
    int running=1;
    int i;
    while (running)
    {
        switch (state)
        {
        case START:
            registerd_servers.servers_num=0;
            for(i=0;i<MAX_SERVERS;i++){
                registerd_servers.servers[i].capacity=-1; //negative capacity to indicate no server registered in that spot
                registerd_servers.servers[i].updated=0;
            }
            timer=time(NULL);
            state=LISTENING;
            break;

        case LISTENING:
            if(listen(server_welcom_socket,1)>0){                           //need to make sure that returns num of waiting and not 0 for sucsses in non blocking
                if(registerd_servers.servers_num<MAX_SERVERS){
                    int new_server_sock=accept(server_welcom_socket,(struct sockaddr*)&server_soc_Addr , sizeof(server_soc_Addr));  //if welcome soc is non block does 
                    registerd_servers.servers_num++;
                    //start new thread and run it in server_connection(new_server_sock)
                }
                else{
                    //reject
                }
            }

            if(listen(client_welcom_socket,1)>0){                           //need to make sure that returns num of waiting and not 0 for sucsses in non blocking
                int new_client=accept(client_welcom_socket,(struct sockaddr*)&server_soc_Addr , sizeof(server_soc_Addr));
                //reciv message
                //check that is valid (if not terminate connaction)
                //parse to get requested capacity
                //find least loaded server
                //if least loaded is not enough, reject
                //if server is good, send server job message and after it was accepted send to client too
                //terminate client connection
                }
            
            if (time(NULL)-timer>=5){
                for(i=0;i<MAX_SERVERS;i++){
                    registerd_servers.servers[i].updated=0;
                }
                //send multicast update requst
                timer=time(NULL);
            }
                
            break;
        }
    }

}