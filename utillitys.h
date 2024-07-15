
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
#include <ifaddrs.h>
//----------constants and global vars-----------------------------
//adresses
#define CLIENT_PORT 6500
#define SERVER_PORT 8700

// load balancer states
#define START 1
#define LISTENING 2

// server connection states
#define UPDATED 1
#define WAITING_FOR_UPDATE 2
#define LAST_CHANCE 3


//message types
#define LB_JOB 0
#define LB_UPDATE 1
#define LB_ACK 2
#define CLIENT_REQ 3
#define JOB_ACK 4
#define SERVER_UPDATE 5
#define CLIENT_REQ_ACK 6
#define CLIENT_JOB_MESSAGE 7

//message header length
#define LB_JOB_H_LEN 3
#define LB_UPDATE_H_LEN 1
#define LB_ACK_H_LEN 1
#define CLIENT_REQ_H_LEN 2
#define JOB_ACK_H_LEN 2
#define SERVER_UPDATE_H_LEN 2
#define CLIENT_REQ_ACK_H_LEN 2
#define CLIENT_JOB_MESSAGE_H_LEN 2




int header_length[]={LB_JOB_H_LEN,LB_UPDATE_H_LEN,LB_ACK_H_LEN,CLIENT_REQ_H_LEN,JOB_ACK_H_LEN,SERVER_UPDATE_H_LEN,CLIENT_REQ_ACK_H_LEN,CLIENT_JOB_MESSAGE_H_LEN}; //header_length[message type]= header length of that message type


struct message{
    int type;
    char header[3]; //change to be longest header length
    char payload[127-3];//make sure header + payload=127
    int payload_len;
};
struct address{
    char ip[16];
    int port;
};

void debug_print(char* string,int len){
    printf("-----\n");
    for(int i=0;i<len;i++){
        printf("%c",string[i]);
    }printf("\n");
    for(int i=0;i<len;i++){
        printf("%d ",string[i]);
    }
    printf("\n-----\n");
}



int send_message(int sock,int type,char* header,char* payload,int payload_len){
    int header_len=header_length[type];
    
    char* full_header=(char*)malloc(header_len+1);
    if (full_header==NULL){
        printf("full_header malloc fail\n");
        return -1;
    }
    memset(full_header,0,header_len+1);
    int len=(header_len+payload_len) * sizeof(char);
    
    char type_c[2];     
    sprintf(type_c,"%d",type);
    type_c[1]='\0';
    
    strncpy(full_header,type_c,1);         //construct header
    strncpy(full_header+1,header,header_len-1);
    full_header[header_len]='\0';
    
    if (len>127){       //max len is 127 (can fit in char)
        printf("message length exeeded, max len is 127byte tried to send:%dbytes\n",len);
        free(full_header);
        return -1;
    }
    char* message=(char*)malloc((len+2) * sizeof(char));    //extra char for message length (excluding the size of the length indicator)
    memset(message,0,(len+2) * sizeof(char));
    message[0]=(char)len+'0';

    memcpy(message + 1, full_header, header_len);
    memcpy(message + 1 + header_len, payload, payload_len);
    message[len+1]='\0';

    printf("sent message:%s\n",message);
    int res=send(sock,message,len+1,0);

    free(full_header);
    free(message);
    return res;

}


struct message* get_message(int soc){        // importent!! always remember to free the return value from this!!
    char buff[1];
    int res;
    res=recv(soc,&buff,sizeof(buff),0);  
    if (res <= 0) {  // Handle error or timeout
        if (res == -1) {
            perror("error: ");
        }
        return NULL;
    }
    
    int len = buff[0]-'0';
    char* message_buff=(char*)malloc((len+1)* sizeof(char));
    res=recv(soc,message_buff,len,0);
    if (res==-1){
        printf("got invalid recv\n");       //error
        free(message_buff);
        return NULL;
    }if (res==0){
        printf("timeout reciving\n");       //time out
        return NULL;
    }
    printf("recieved a message: %s\n",message_buff);

    struct message* new_message = (struct message*)malloc(sizeof(struct message));

    new_message->type=message_buff[0]-'0';
    int header_len=header_length[new_message->type];

    printf("message type %d\n",new_message->type);
    new_message->payload_len=len-header_len+1;
    strncpy(new_message->header, message_buff + 1, header_len - 1);
    new_message->header[header_len - 1] = '\0';  // Null-terminate the header
    strncpy(new_message->payload, message_buff + header_len, new_message->payload_len+1);
    new_message->payload[(new_message->payload_len)] = '\0';  // Null-terminate the payload

    return new_message;
}


struct address* address_parsing(char* address_string){
    int i;
    struct address* parsed_address = (struct address*)malloc(sizeof(struct address));
    for(i=0;i<strlen(address_string);i++){
        if(address_string[i]==':'){
            strncpy(parsed_address->ip,address_string,i);
            parsed_address->ip[i]='\0';
            char port_c[6];
            strcpy(port_c,address_string+i+1);
            port_c[5]='\0';
            parsed_address->port=atoi(port_c);
            break;
        }
    }
    if(i==strlen(address_string)){
        printf("i got invalid addres parsing. deallocating and going to cry in the corner...\n");
        free(parsed_address);
        return NULL;
    }
    return parsed_address;

}


int createWelcomeSocket(int port, int maxClient){
    int serverSocket, opt=1;
    struct sockaddr_in serverAddr;
    socklen_t server_size;

    serverSocket= socket(PF_INET,SOCK_STREAM,0);
    if(serverSocket<0){
        perror("socket failed");
        return -1;
    }
    if (setsockopt(serverSocket, SOL_SOCKET, SO_REUSEADDR ,&opt, sizeof(opt))){
        perror("socket option failed");
        close(serverSocket);
        return -1;
    }
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(port);
    serverAddr.sin_addr.s_addr = INADDR_ANY;
    server_size= sizeof(serverAddr);

    if((bind(serverSocket,(struct sockaddr *)&serverAddr,server_size))<0) {
        perror("binding failed");
        close(serverSocket);
        return -1;
    }

    if((listen(serverSocket,maxClient))<0){
        perror("listen failed");
        close(serverSocket);
        return -1;
    }
    return serverSocket;
}

char* get_my_ip(){
    static char ip[INET_ADDRSTRLEN];
    struct ifaddrs *ifaddr, *ifa;
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
    return ip;

}


