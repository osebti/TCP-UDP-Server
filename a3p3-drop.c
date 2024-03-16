#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <fcntl.h>


#define MAX_LINE_SIZE 1024
#define MAX_SEQ_NUM 1000
#define SERV_PORT 3000
// Message Iypes
#define DATA_TYPE 1
#define ACK_TYPE 2
#define NAK_TYPE 3

//Message Structures
struct Message {
int type;
int seqNum;
char data[MAX_LINE_SIZE];
};

void error(const char *msg){
    perror(msg);
    exit(1);

}


void server(){
    int sockfd,newsockfd;
    socklen_t clilen;
    struct sockaddr_in serv_addr,cli_addr;
    struct Message recvMsg,sendMsg;
    sockfd= socket (AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0){
        error ( "ERROR opening socket");
    }
    memset ((char *) &serv_addr, 0, sizeof (serv_addr) );

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = (INADDR_ANY);
    serv_addr.sin_port =  (SERV_PORT);
    if (bind(sockfd,(struct sockaddr *) &serv_addr, sizeof (serv_addr)) < 0){
        error ( "ERROR on binding" ) ;
    }

    int seqExpected = 1;
    while (1){  
        clilen=sizeof(cli_addr);
        if (recvfrom(sockfd, &recvMsg, sizeof (recvMsg), 0, (struct sockaddr *) &cli_addr, &clilen) < 0){
            error("ERROR receiving message") ;
        }
        
        printf ("Received: DATA(%d)\n", recvMsg.seqNum);

        if (recvMsg.seqNum == seqExpected){
            sendMsg.type= ACK_TYPE;
            sendMsg.seqNum = seqExpected;
            seqExpected++;
        }
        else{
            sendMsg. type= NAK_TYPE;
            sendMsg.seqNum = recvMsg.seqNum;

        }

        if(sendto (sockfd, &sendMsg,sizeof (sendMsg), 0, (struct sockaddr *) &cli_addr, clilen) < 0){
            error ( "ERROR sending acknowledgment") ;
        }
        if(sendMsg.type == ACK_TYPE){
            printf ("Transmitted: ACK(%d)\n", sendMsg.seqNum);
        }

        else{
            printf("Transmitted: NAK(%d)\n", sendMsg.seqNum);
            exit(1);
        }

    }

    close(sockfd);

}

void client (const char *serverName, const char *dataFile, int nLine){
    int sockfd;
    struct sockaddr_in serv_addr;
    struct hostent *server;
    FILE *file;
    char line[MAX_LINE_SIZE];
    struct Message sendMsg, recvMsg;
    sockfd = socket (AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0){
        error ("ERROR opening socket");\
    }
    int flags = fcntl(sockfd, F_GETFL);
    flags = flags | O_NONBLOCK;
    fcntl(sockfd, F_SETFL,flags);
    server = gethostbyname(serverName);
    if (server==NULL){
        fprintf(stderr,"ERROR, no such host\n");
        exit(0);
    }
    printf("%s\n",(char *)server->h_addr_list[0]);

    memset ( (char * ) &serv_addr, 0, sizeof (serv_addr) ) ;
    serv_addr.sin_family=AF_INET;
    bcopy ((char *) server->h_addr,(char *)&serv_addr.sin_addr.s_addr,server->h_length);
    //serv_addr.sin_addr.s_addr=inet_addr(server->h_addr);
    serv_addr.sin_port = (SERV_PORT);
    file = fopen (dataFile, "r");
    if (file==NULL){
        error ( "ERROR opening data file" );
    }
    int seqNum = 1;

    while (fgets (line,MAX_LINE_SIZE, file) != NULL){
        sendMsg.type=DATA_TYPE;
        sendMsg.seqNum=seqNum;
        strncpy (sendMsg.data, line, MAX_LINE_SIZE);

        if(sendto(sockfd,&sendMsg,sizeof (sendMsg),0, (struct sockaddr *) &serv_addr, sizeof(serv_addr))<0){
            
            error ( "ERROR sending data");
        }

        printf ("Transmitted: DATA(%d)\n", sendMsg.seqNum) ;
        socklen_t servlen = sizeof(serv_addr);

        // Non-blocking recvfrom so that client does not wait for server response
        if (recvfrom(sockfd,&recvMsg,sizeof(recvMsg),0, (struct sockaddr *)&serv_addr, &servlen) > 0){
            //error ( "ERROR receiving acknowledgment");
            if(recvMsg.type == ACK_TYPE){

                printf ("Received: ACK(%d)\n",recvMsg.seqNum);
            }

            else{
                printf ("Received: NAK(%d)\n", recvMsg. seqNum);

            }
        }
        
    
        seqNum++;
    }
    fclose(file);
    close(sockfd);
        
        
}

int main(int argc, char *argv[]){

    if(argc<2){
        fprintf(stderr, "Usage for Server: %s -s\n", argv[0]);
        fprintf(stderr, "Usage for Client: %s -c serverName dataFile nLine\n",argv[0]);
        exit(1);

    }
    if(strcmp (argv[1],"-s") == 0){
        server();
    }
    else if(strcmp (argv[1],"-c") == 0){
        const char *serverName = argv[2];
        const char *dataFile = argv[3];
        int nLine = atoi(argv[4]);
        client(serverName,dataFile,nLine);
    }
    else{
        fprintf(stderr,"Invalid Arguments\n");
        exit(1);
    }

    return 0;

}



// Part 3 Q1

/* The program does not compile because we need to import the header file containing 
the declaration of struct hostent. It is normal that compilation fails as the compiler
does not see the declaration for the struct hostent which we make use of in the program. 
We will add an include statetement to the code to resolve the problem: '#include <netdb.h>'. 
*/


// Q2

/*
Error 1
There is an error in the main function with one of the if conditions: "if argc < 3" will cause 
the program to exit with status 1 and print an error message when we try to run the server
with the command "./a3-compiles.c -s" which contains only two arguments. Therefore we need 
to change the if-condition to: "if argc < 3"

Error 2
Need to use htons on SERV_PORT (client function and server function) to avoid potential errors

Error 3
Need to use  htonl on INADDR_ANY (server function) to avoid potential errors


*/

// Q3

/*
No although a3-p3-comm.c achieves 2 way communication it only transmits the one line from the
input file in each transmission independently of the argument specified by the user (nLines).
Therefore, it does not conform to the specifications.

*/

// Q4 

/*
The server obtains the client's protocal address by using the recvfrom function. It uses a variable 
name cli_addr (of type sockaddr_in) in the recvfrom function as well as the size of cli_addr in bytes.
which is stored in the variable clilen. The recvfrom function will then
fill in the variable cli_addr's port number and ip address when a client sends a message. This 
ensures the protocal address is successfully retrieved. 
*/


// Q5 
/* 
File name: input 3
File size: 833 kB
Message Size: 1024 
Sequence Number: 6696

Sequence leading up to NAK message:
Received: DATA(3341)
Transmitted: ACK(3341)
Received: DATA(6696)
Transmitted: NAK(6696)

*/







