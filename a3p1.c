#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <pthread.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <poll.h>
#include <sys/times.h> 
#include <assert.h>
#include <stdarg.h>
#include <time.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/time.h>
#include <errno.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <netdb.h>

#define _REENTRANT

// fifo 0-1 (server to client)
// fifo 1-0 (client to server)
#define MAX_NTOKEN 3
#define MAXLINE 80
#define MAXWORD 20
#define PORT 9567 // 9xxx with xxx being last 3 numbers of student id 
#define NCLIENT 1 // only 1 client in this part 
#define HELLO 8 
#define DONE 9

typedef struct sockaddr  SA;

// kinds: 0 = get, 1 = put, 2 = delete, 3 = gtime, 4 = TIME, 5 = OK, 6 = Error 

typedef struct { 
char  content[3][MAXLINE];  // 3 lines at most 
int identifier;
char object_name[32];
int content_size;
int kind; // 0-3
} ClientFrame;


typedef struct {
    char  content[3][MAXLINE];  // 3 lines at most 
    int content_size;
    int available; // 1 = exists, 0 = DNE
    int owner; // owner id 
    char object_name[MAXLINE];
} Object;


typedef struct { 
char  status[MAXLINE];  // OK/error message
double time; // time 
int kind; // 4-6 
Object object; // object for get  
} ServerFrame;




Object table[16];
char status_msg[3][5]  = {"ERROR", "OK", "TIME"}; // status message (ok, error,..)
char req[5][6]={"GET", "PUT", "DELETE", "GTIME", "TIME"};
char server_fifos[3][MAXLINE]={"fifo-0-1","fifo-0-2","fifo-0-3"};
char client_fifos[3][MAXLINE]={"fifo-1-0","fifo-2-0","fifo-3-0"};

struct tms tstart; 
struct tms tend;
clock_t start2,end2;
time_t start;
int s_fifos[3]={0,0,0};
int c_fifos[3]={0,0,0};
char tokens[3][MAXLINE];
char errors[3][MAXLINE]={"Object does not exist", "Object belongs to a different client", "Object already exists"};
struct pollfd pfds[NCLIENT+1]; // managing socket + NClients  


 

void printTimes(int real, struct tms* start, struct tms* end){
    static long     clocktick = 0;
    if (clocktick == 0)   // get clock ticks 
        if ((clocktick = sysconf(_SC_CLK_TCK)) < 0){
            perror("sysconf error");
        }

    printf("real:  %7.2f\n", real / (double) clocktick);

    printf("user:  %7.2f\n",
      (end->tms_utime - start->tms_utime) / (double) clocktick);
    printf("sys:   %7.2f\n",
      (end->tms_stime-start->tms_stime) / (double) clocktick);

    printf("child user:  %7.2f\n", (end->tms_cutime-start->tms_cutime) / (double) clocktick);

    printf("child system: %7.2f\n", (end->tms_cstime-start->tms_cstime) / (double) clocktick);

}



// The fatal and warning functions were made by the authors of the 
// AWK Programming Language

void FATAL (const char *fmt,...)
{
    va_list  ap;
    fflush (stdout);
    va_start (ap, fmt);  vfprintf (stderr, fmt, ap);  va_end(ap);
    fflush (NULL);
    exit(1);
}

void WARNING (const char *fmt,...)
{
    va_list  ap;
    fflush (stdout);
    va_start (ap, fmt);  vfprintf (stderr, fmt, ap);  va_end(ap);
}

void mpause(int ms){ // pause for an amount of miliseconds
    if (usleep(ms*1000)!=0){
        perror("usleep: ");
        return;
    }
    else{
        return;
    }
}
void Done(int id){
    // do nothing 
    ServerFrame frame;
    memset((char *) &frame, 0, sizeof(frame));
    frame.kind=2;
    write(pfds[id+1].fd, (char *) &frame, sizeof(frame));
}

void Hello(int id){
    ServerFrame frame;
    memset((char *) &frame, 0, sizeof(frame));
    frame.kind=2;
    write(pfds[id+1].fd, (char *) &frame, sizeof(frame));
}


Object Get(char* object_name,int id){
    ServerFrame frame;
    memset(&frame,0,sizeof(frame));
    Object object;
    memset(&object,0,sizeof(object));

    for(int i=0; i< 16; i++){
        if(strcmp(table[i].object_name,object_name)==0){
            // make a new object and send frame 
          
            object.content_size=table[i].content_size;
            for (int y=0; y<object.content_size; y++){
                strcpy(object.content[y],table[i].content[y]);
            }
            object.available=1;
            frame.object=object;
            frame.kind=0;
            strcpy(frame.status,"OK");
            write (pfds[id+1].fd, (char *) &frame, sizeof(frame)); // send frame to client
            return object; // success         
        }

    }

    frame.kind=6;
    strcpy(frame.status,"Error: Object does not exist");
    write (pfds[id+1].fd, (char *) &frame, sizeof(frame)); // send frame to client
    return object; // failure, not found (error)

}

int Put(ClientFrame* frame,int id){
    ServerFrame s_frame;
    memset(&s_frame,0,sizeof(s_frame));
    char *msg;
    if(exists(frame->object_name)){
        strcpy(s_frame.status,"Error: Object already exists");
        s_frame.kind=6;
        write (pfds[id+1].fd, (char *) &s_frame, sizeof(s_frame)); // send frame to client
        return 3;
    }
    
    for(int i=0;i<16;i++){
        if(strlen(table[i].object_name)==0){ // empty block 
            strcpy(table[i].object_name,frame->object_name);
            table[i].owner=id;
            table[i].available=1;
            int content_size=frame->content_size;
            table[i].content_size=content_size;
            for(int y=0; y<content_size; y++){
                strcpy(table[i].content[y],frame->content[y]);   

            }
            strcpy(s_frame.status,"OK");
            s_frame.kind=1;
            write (pfds[id+1].fd, (char *) &s_frame, sizeof(s_frame)); // send frame to client
            
            return 0; // success
        }
    }

    return -1; // error no available space 

}

int exists(char *obj_name){     
    for(int i=0;i<16;i++){
        if(strcmp(table[i].object_name,obj_name)==0){ // empty block 
            return 1; // exists 
        }
    }
    return 0; // does not exist 

}

int Delete(char* object_name,int id){
    ServerFrame s_frame;
    memset(&s_frame,0,sizeof(s_frame));


    
    for(int i=0;i<16;i++){
        if(strcmp(table[i].object_name,object_name)==0){ //
            if(table[i].owner!=id){
                s_frame.kind=6;
                strcpy(s_frame.status,"Error: Object belongs to a different client");
                s_frame.kind=6;
                write (pfds[id+1].fd, (char *) &s_frame, sizeof(s_frame)); // send frame to client
                return 2;
            }

            memset(&table[i],0,sizeof(Object)); // delete object by reinitializing 
            strcpy(s_frame.status,"OK");
            s_frame.kind=2;
            write (pfds[id+1].fd, (char *) &s_frame, sizeof(s_frame)); // send frame to client        
            return 1; // success
        }
    }

    strcpy(s_frame.status,"Error: Object does not exist");
    s_frame.kind=6;
    write (pfds[id+1].fd, (char *) &s_frame, sizeof(s_frame)); // send frame to client
    return 1; // error no available space 
   
}

double Gtime(int id){
    ServerFrame s_frame;
    memset(&s_frame,0,sizeof(s_frame));
    s_frame.kind=4;
    if((end2=times(&tend))==-1){
            perror("times error");
        }

    double time =  (double)(end2-start2) / (double) sysconf(_SC_CLK_TCK);
    strcpy(s_frame.status,"OK");
    s_frame.time=time;
    write (pfds[id+1].fd, (char *) &s_frame, sizeof(s_frame)); // send frame to client 
    return time;

}



void printSframe(ServerFrame *frame, int kind,int identifier, char* object_name){
    if(kind==0){ // GET 
            printf("Received (src = server)  (OK) (%s)\n", object_name);
            Object obj; 
            char msg[5];
            obj=frame->object;
            strcpy(msg,status_msg[obj.available]);
            if(obj.available>0){
                for (int i=0;i<obj.content_size;i++){
                    printf("[%d]: %s\n", i,obj.content[i]);
                }
            }
            printf("\n\n");
    }

    else if(kind==1){ // PUT
        printf("Received (src = server) (OK)\n\n\n");
     
    }


    else if(kind==2){ // OK 
        printf("Received (src = server) (OK)\n\n\n");

    }

    else if(kind==4){ // gtime 
        printf("Received (src = server)  (TIME: %.2f)\n\n\n",frame->time);
        
    }
    else if(kind==6){ // error
        printf("Received (src = server)  (%s)\n\n\n",frame->status);
        
    }


    else{
        printf ("Unknown frame type (%d)\n\n", frame->kind);
  
    }
    
}


void processCFrame (ClientFrame *frame) // process frame and print 
{
    
    int kind = frame -> kind;
    int identifier = frame->identifier;
    int content_size=frame->content_size; 
    char *object_name; 
    object_name = frame->object_name;
    char msg[5];
    
    
    if(kind==0){ // GET 
        printf("Received (src = client:%d)  (GET: %s)\n", identifier, object_name);
        Object obj; 
        obj = Get(object_name,identifier);
        if(obj.available==1){
            printf ("Transmitted (src = server) (OK)  (%s)\n",object_name);

            for (int i=0;i<obj.content_size;i++){
                printf("[%d]: %s\n", i, obj.content[i]);
            }
     
        }
        else{
            printf ("Transmitted (src = server) (ERROR: Object does not exist)\n");

        }
        printf("\n\n");
    }

    else if(kind==1){ // PUT
        printf("Received (src = client:%d)  (PUT)  (%s)\n", identifier, object_name);

        for (int i=0;i<content_size;i++){
            printf("[%d]: %s\n", i,frame->content[i]);
        }
        int status = Put(frame,identifier);
        if(status==0){
            printf ("Transmitted (src = server) (OK)\n\n\n");
        }
        else{
            printf ("Transmitted (src = server) (Error: %s)\n\n\n",errors[status-1]);

        }
        
     
    }


    else if(kind==2){
        printf("Received (src = client:%d)  (DELETE)  (%s)\n", identifier, object_name);
        int status = Delete(frame->object_name,identifier);
        if(status==0){
            printf ("Transmitted (src = server) (OK)\n\n\n");
        }
        else{
            printf ("Transmitted (src = server) (Error: %s)\n\n\n",errors[status-1]);

        }

    }

    else if(kind==3){
        printf("Received (src = client:%d)  GTIME\n", identifier, object_name);
        double time = Gtime(identifier);
        printf ("Transmitted (src = server) (TIME: %.2f)\n\n\n",time);

    }
    else if(kind==HELLO){ // change this later if need be 
        printf("Received (src = client:%d)  HELLO\n", identifier, object_name);
        Hello(identifier);
        printf ("Transmitted (src = server) (Connection Accepted)\n\n\n");

    }

    else if(kind==DONE){ // change this later if need be 
        printf("Received (src = client:%d)  DONE\n", identifier, object_name);
        Done(identifier);
        printf ("Transmitted (src = server) (OK)\n\n\n");

    }





    else{
        printf ("Unknown frame type (%d)\n", frame->kind);
  
    }
}





int split(char * inStr, int id) // split function used from starter code file
{
    const char fs[] = " \n\t";
    int    i;
    int count;
    char *tokenp;


    // initialize variables
    count= 0;

    if(strcmp(inStr,"\n")==0){
        return 0;
    }
    
    for (i=0; i < 3; i++){
        memset(tokens[i], 0, sizeof(tokens[i]));
    }
   

    tokenp=strtok(inStr, fs);
    
    if (tokenp== NULL){
        return 0;
    }
    
    if (tokenp[0] == "#" || atoi(tokenp)!=id){ // ids do not match (invalid command)
        return 0;
    }

    
    strcpy(tokens[count],tokenp); 
    count++;
    

    while (count<3) // max split = 3
    {
        tokenp  = strtok(NULL, fs);

        if (tokenp==NULL){
            return 1;
        }
        strcpy(tokens[count],tokenp); 
        count++;   
    }

    return 1; // matching ids, valid command    
}


ClientFrame receive (int fd)
{
    int    len; 
    ClientFrame  frame;
    assert (fd >= 0);
    memset( (char *) &frame, 0, sizeof(frame));
    len = read (fd, (char *) &frame, sizeof(frame));
    if (len != sizeof(frame))
        WARNING ("Received frame has length= %d (expected= %d)\n", len, sizeof(frame));
    return frame;         
}


void executeCommand(char *command){
    if (strcmp(command,"quit")==0){
        // print system cpu time, etc. 
        printf("Quitting\n");
        for(int i=0; i<3;i++){ // close fifos before termination 
            close(s_fifos[i]);
            close(c_fifos[i]);
        }
        if((end2=times(&tend))==-1){
            perror("times error");
        }

        int real = end2-start2; // get real time 
        printTimes(real,&tstart,&tend);  // pass arguments to printTimes() function
        exit(0);
    }
  
    else if(strcmp(command,"list")==0){
        int content_size;
        printf("Stored Object Table\n");
        for (int i=0;i<16;i++){
            if (table[i].available==1){
                printf("(owner = %d, name= %s)\n",(table[i].owner), table[i].object_name);
                content_size=table[i].content_size;
                for(int y=0; y<content_size; y++){
                    printf("[%d]: %s\n", y, table[i].content[y]);  
                } 
                
            }
        }

        printf("\n\n");
    }
}

void keyboardCmd(){
    char buf[MAXLINE];
    if (fgets(buf,MAXLINE,stdin)!=NULL){
        buf[strcspn(buf,"\n")]=0; // remove empty line
        executeCommand(buf);
    }
    return;
}


void server(int port){
    // open fifos 

    start = clock(); 

    if((start2=times(&tstart))==-1){
        perror("times error");
    }
    int ret;


    Object objects[16]; // 16 objects 
    ClientFrame cframe;
    memset((char *) &table,0,sizeof(table)); // initialize to 0's
    memset( (char *) &cframe, 0, sizeof(cframe) );


    int   i, N, len, done[NCLIENT+1];


    int timeout=25; // timeout in ms 


    struct pollfd pfds[NCLIENT+1]; // managing socket + NClients  
    struct sockaddr_in  from;
    socklen_t           fromlen;

    for (i= 1; i <= NCLIENT; i++){
        done[i]= -1;
    }
    pfds[0].fd=  serverListen (PORT, NCLIENT);;
    pfds[0].events= POLLIN;
    pfds[0].revents= 0;

    printf ("Server is accepting connections (port= %d)\n", PORT);
    
    N = 1;		// N descriptors to poll
    while(1) {
        if((ret=poll(pfds,1,timeout)) < 0){ // poll error 
            perror("poll: ");
        } 
        if ( (N < NCLIENT+1) && (pfds[0].revents & POLLIN)) {
           // accept a new connection
	        fromlen= sizeof(from);
	        pfds[N].fd= accept(pfds[0].fd, (SA *) &from, &fromlen);

	        pfds[N].events= POLLIN;
	        pfds[N].revents= 0;
	        done[N]= 0;
	        N++;
        }


        if(ret>0){ // available data
            for (int i=0;i<1;i++){
                if(pfds[i].revents && POLLIN){
                    ClientFrame cframe;
                    cframe=receive(pfds[i].fd); // pass fd2 to receive frame; 
                    processCFrame(&cframe);
                    pfds[i].revents=0; // reset revents field
                    pfds[i].events=POLLIN;
                }
            }
            
        }

        if (testDone(done,N-1) == 1) break;


    }

}


void client(char *inputFile, int id, char *sname, int port){



    int    i, len, sfd;
    char   serverName[MAXWORD];
    ClientFrame hello;
    memset((char *) &hello, 0, sizeof(hello));
    hello.kind=HELLO;
    
    strcpy (serverName, sname);
    printf ("Client: Trying server '%s', portNo= %d\n", serverName, port);
    if((sfd = clientConnect (serverName, port)) < 0){
        perror("socket");
    }



    FILE *fd;
    int transmit;
    
    if ((fd = fopen(inputFile, "r")) == NULL){
        perror("open file");
        return;
    }


    int length;
    char buf[MAXLINE];
    ClientFrame frame;

    int check;
    printf ("Client (idNumber= %d, inputFile = %s)\n\n",id, inputFile);

    // Send hello packet 
    write(sfd, (char *) &hello, sizeof(hello));
    printf("Transmitted (src = client:%d) (%s) \n", id, "HELLO");
    int status=0;
    

    while(1){
        if (status==1){

            if(fgets(buf,MAXLINE,fd)==NULL){ // eof or error 
                return;
            }
             
            check=split(buf,id);
        
            
            if (check==1){ // command is valid
                memset((char *) &frame,0,sizeof(frame)); // initialize frame with 0's
                frame.identifier=id;
                int count=0;
                
                if (strcmp(tokens[1],"put")==0){
                    
                    frame.kind=1;
                    status = 0;
                    strcpy(frame.object_name,tokens[2]);
                    
                    for(int i=0; i < 5;i++){ // 3 + 2 lines at most
                        fgets(buf,sizeof(buf),fd);
                        int buf_len=strlen(buf);
                        if (buf_len >0 && buf[buf_len-1]=='\n'){
                            buf[buf_len-1]=0;
                        }

                        if (buf[0]=='}'){
                            // finish 
                            break;
                        }
                        //buf[strcspn(buf,'\n')]=0; // remove trailing whitespace
                        else if (strlen(buf)>0 && buf[0] != '{' && buf[0] != '}' && buf[0]!= '\n'){
                            // get next lines 
                            strcpy(frame.content[count],buf);
                            count=count+1;
                        }
                        

                    }
                    frame.content_size=count;
                    printf("Transmitted (src = client:%d) (%s) (%s)\n", id, "PUT", frame.object_name);
                        for(int i=0;i<count;i++){
                            printf("[%d]: %s\n", i, frame.content[i]);
                    }
                    write (sfd, (char *) &frame, sizeof(frame)); // send frame to server  
                
                }

                else if (strcmp(tokens[1],"get")==0){
                    frame.kind=0;
                    strcpy(frame.object_name,tokens[2]); 
                    status = 0;   
                    write (sfd, (char *) &frame, sizeof(frame)); // send frame to server  
                    printf("Transmitted (src = client:%d) (%s) (%s)\n", id, "GET", frame.object_name);
                }
                else if (strcmp(tokens[1],"delete")==0){
                    frame.kind=2;
                    strcpy(frame.object_name,tokens[2]);  
                    status = 0;    
                    write (sfd, (char *) &frame, sizeof(frame)); // send frame to server  
                    printf("Transmitted (src = client:%d) (DELETE) (%s)\n", id, frame.object_name);
                }
                else if (strcmp(tokens[1],"gtime")==0){
                    frame.kind=3;
                    status = 0;       
                    write (sfd, (char *) &frame, sizeof(frame)); // send frame to server
                    printf("Transmitted (src= client:%d) GTIME\n", id);
 
                }
                else if (strcmp(tokens[1],"done")==0){
                    frame.kind=DONE;
                    status = 0;       
                    write (sfd, (char *) &frame, sizeof(frame)); // send frame to server
                    printf("Transmitted (src= client:%d) DONE\n", id);
 
                }
                else if (strcmp(tokens[1],"delay")==0){
                    int ms=atoi(tokens[2]);
                    printf("*** Entering a delay period of %d msec\n", ms);
                    mpause(ms); // change this to work with miliseconds 
                    printf("*** Exiting delay period\n\n\n");   
                }
                else if (strcmp(tokens[1],"quit")==0){
                    return;               
                }
                else{
                    printf("Invalid Command\n");
                }
                        
            }

        }

        else{
            int    len; 
            ServerFrame  s_frame;
            memset( (char *) &s_frame, 0, sizeof(s_frame));
            
            len = read(sfd, (char *) &s_frame, sizeof(s_frame));
            if (len != sizeof(s_frame)){
                WARNING ("Received frame has length= %d (expected= %d)\n", len, sizeof(s_frame));
            }
            printSframe(&s_frame,s_frame.kind,id,frame.object_name);  
            status=1;
        }
    }
}

int clientConnect (const char *serverName, int portNo)
{
    int                 sfd;
    struct sockaddr_in  server;
    struct hostent      *hp;                    // host entity

    // lookup the specified host
    //
    hp= gethostbyname(serverName);
    if (hp == (struct hostent *) NULL)
        FATAL("clientConnect: failed gethostbyname '%s'\n", serverName);

    // put the host's address, and type into a socket structure
    //
    memset ((char *) &server, 0, sizeof server);
    memcpy ((char *) &server.sin_addr, hp->h_addr, hp->h_length);
    server.sin_family= AF_INET;
    server.sin_port= htons(portNo);

    // create a socket, and initiate a connection
    //
    if ( (sfd= socket(AF_INET, SOCK_STREAM, 0)) < 0)
        FATAL ("clientConnect: failed to create a socket \n");

    if (connect(sfd, (SA *) &server, sizeof(server)) < 0)
	FATAL ("clientConnect: failed to connect \n");

    return sfd;
}


// ------------------------------
int serverListen (int portNo, int nClient)
{


    struct sockaddr_in  from;
    socklen_t           fromlen;



    int  sfd;
    struct sockaddr_in  sin;

    memset ((char *) &sin, 0, sizeof(sin));

    // create a managing socket
    //
    if ( (sfd= socket (AF_INET, SOCK_STREAM, 0)) < 0)
        FATAL ("serverListen: failed to create a socket \n");

    // bind the managing socket to a name
    //
    sin.sin_family= AF_INET;
    sin.sin_addr.s_addr= htonl(INADDR_ANY);
    sin.sin_port= htons(portNo);

    if (bind (sfd, (SA *) &sin, sizeof(sin)) < 0)
        FATAL ("Server: bind failed \n");

    // indicate how many connection requests can be queued

    listen (sfd, nClient);
    return sfd;
}


int testDone (int done[], int num_clients)
{
    int  i, sum= 0;

    if (num_clients == 0){
        return 0;
    }
    
    for (i= 1; i <= num_clients; i++){
        sum += done[i];
    }
    if (sum == -num_clients){
        return 0;
    }

    for (i= 1; i <= num_clients; i++) { 
        if (done[i] == 0) return 0; 
    }
    return 1;
}    
    


int main(int argc, char * argv[]){

    // MAKE SURE TO CHECK IF CONNECTION WAS CLOSED WHEN READ RETURNS 0 !!!!!


    if (strcmp(argv[1],"-s")==0 && argc == 3){
        server(atoi(argv[2]));
    }
    else if (strcmp(argv[1],"-c")==0 && argc == 6){
       
        client(argv[3],atoi(argv[2]),atoi(argv[4]),atoi(argv[5])); // pass input file and idNumber=1
    }
    
    else{
        printf("Invalid arguments provided\n");
    }

    return 0;
    
}
