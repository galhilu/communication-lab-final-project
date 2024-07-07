
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


//message types
#define LB_JOB 0
#define LB_UPDATE 1
#define LB_ACK 2
#define CLIENT_REQ 3
#define JOB_ACK 4
#define SERVER_UPDATE 5
#define CLIENT_REQ_ACK 6

//message header length
#define LB_JOB_H_LEN 1
#define LB_UPDATE_H_LEN 2
#define LB_ACK_H_LEN 3
#define JOB_ACK_H_LEN 3
#define SERVER_UPDATE_H_LEN 3
#define CLIENT_REQ_ACK_H_LEN 2

int header_length[]={LB_JOB_H_LEN,LB_UPDATE_H_LEN,LB_ACK_H_LEN,JOB_ACK_H_LEN,SERVER_UPDATE_H_LEN}; //header_length[message type]= header length of that message type


struct message{
    int type;
    char header[3]; //change to be longest header length
    char payload[127-3];//make sure header + payload=127
    int payload_len;
};






int send_message(int sock,int type,char* header,char* payload,int payload_len){
   
    char* full_header=(char*)malloc(sizeof(char)*header_length[type]);
    char temp[2];
    char test[1];
    sprintf(temp,"%d",type);
    printf("type:%s\n",temp);
    strcpy(full_header,temp);         //construct header
    strcat(full_header,header);
    printf("full_header:%s\n",full_header);

    int len=sizeof(full_header)+sizeof(char)*payload_len;
    printf("want to send %d long message\n",len);
    if (len>127){       //max len is 127 (can fit in char)
        printf("message length exeeded, max len is 127byte tried to send:%dbytes\n",len);
        free(full_header);
        return -1;
    }
    char* message=(char*)malloc((header_length[type]+payload_len+1) * sizeof(char));    //extra char for message length (excluding the size of the length indicator)
    test[0]=(char)len+'0';
    printf("test is:%c\n",*test);
    strcpy(message,test);
    strcat(message,full_header);
    strcat(message,payload);
    free(full_header);
    int res;
    printf("sent:%s\n",message);
    res=send(sock,message,sizeof(message),0);
    printf("res%d\n",res);
    return res;

}


struct message* get_message(int soc){        
    char buff[1];
    int res;
    printf("len\n");
    res=recv(soc,&buff,sizeof(buff),0);       
    if (res==-1){                           //error
        printf("got invalid recv length\n");
        perror("error:\n");
        return NULL;
    }
    if (res==0){                            //time out
        printf("timeout reciving length\n");
        return NULL;
    }
    
    int temp = atoi(buff);
    printf("temp is%d\n",temp);
    char* message_buff=(char*)malloc(temp * sizeof(char));
    res=recv(soc,message_buff,sizeof(message_buff),0);
    if (res==-1){
        printf("got invalid recv\n");       //error
        free(message_buff);
        return NULL;
    }if (res==0){
        printf("timeout reciving\n");       //time out
        return NULL;
    }
    printf("buff is%s\n",message_buff);
    struct message* new_message;
    new_message->type=(int)(message_buff[0]-'0');
    strncpy(new_message->header,message_buff,header_length[new_message->type]);
    strncpy(new_message->payload,message_buff+sizeof(char)*header_length[new_message->type],header_length[new_message->type]);
    new_message->payload_len=sizeof(message_buff)-header_length[new_message->type];
    return new_message;
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

    printf("Server is listen to port %d and wait for new client...\n", port);

    if((listen(serverSocket,maxClient))<0){
        perror("listen failed");
        close(serverSocket);
        return -1;
    }
    return serverSocket;
}