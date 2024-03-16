/*
# ------------------------------------------------------------
# sockMsg - A client-server program to test formatted messages and TCP sockets
#
# compile with:  g++ sockMsg.cc -o  sockMsg
#
# Usage:  sockMsg -s
#         sockMsg -c serverName
#
# Summary:
#   - The server listens to port MYPORT, and accepts up to NCLIENT
#     connections using poll(). For each client, the server accepts
#     a sequence of messages and replies to each message with an ACK.
#
#   - Each client connects to the server, and iterates NITER times.
#     In each iteration, a sequence of messages are transmitted.
#
# Author: Prof. Ehab Elmallah (for CMPUT 379, U. of Alberta)
# ------------------------------------------------------------
*/

#define _REENTRANT

#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <cassert>
#include <cmath>
#include <ctime>
#include <cstring>
#include <cstdarg>

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/time.h>
#include <sys/times.h>
#include <fcntl.h>
#include <errno.h>

#include <netinet/in.h>
#include <sys/socket.h>
#include <netdb.h>

#include <poll.h>

#include <string>
#include <vector>
using namespace std;

// ------------------------------
#define MAXLINE   132
#define MAXWORD    32

#define NCLIENT  5  // client limit
#define MYPORT   2222

#define NF 3		 // number of fields in each message
#define NITER 10	 // number of iterations per client
#define MSG_KINDS 5
// ------------------------------

typedef enum { STR, INT, FLOAT, DONE, ACK } KIND;	  // Message kinds
char KINDNAME[][MAXWORD]= { "STR", "INT", "FLOAT", "DONE", "ACK" };

typedef struct { char d[NF][MAXLINE]; } MSG_STR;
typedef struct { int  d[NF]; } MSG_INT;
typedef struct {float d[NF]; } MSG_FLOAT;

typedef union { MSG_STR  mStr; MSG_INT mInt; MSG_FLOAT mFloat; } MSG;

typedef struct { KIND kind; MSG msg; } FRAME;

#define MAXBUF  sizeof(FRAME)

typedef struct sockaddr  SA;

// ------------------------------
// The WARNING and FATAL functions are due to the authors of
// the AWK Programming Language.

void FATAL (const char *fmt, ... )
{
    va_list  ap;
    fflush (stdout);
    va_start (ap, fmt);  vfprintf (stderr, fmt, ap);  va_end(ap);
    fflush (NULL);
    exit(1);
}

void WARNING (const char *fmt, ... )
{
    va_list  ap;
    fflush (stdout);
    va_start (ap, fmt);  vfprintf (stderr, fmt, ap);  va_end(ap);
}
// ------------------------------------------------------------

//newpage
// ------------------------------
// composeMSTR - compose a message of 3 strings: a, b, and c.

MSG composeMSTR (const char *a, const char *b, const char *c)
{
    MSG  msg;

    memset( (char *) &msg, 0, sizeof(msg) );
    strcpy(msg.mStr.d[0],a);
    strcpy(msg.mStr.d[1],b);
    strcpy(msg.mStr.d[2],c);
    return msg;
}    
// ------------------------------
// composeMINT - compose a message of 3 integers: a, b, and c.

MSG composeMINT (int a, int b, int c)
{
    MSG  msg;

    memset( (char *) &msg, 0, sizeof(msg) );
    msg.mInt.d[0]= a; msg.mInt.d[1]= b; msg.mInt.d[2]= c;
    return msg;
}    
// ------------------------------
// composeMFLOAT - compose a message of 3 floating numbers: a, b, and c.

MSG  composeMFLOAT (float a, float b, float c)
{
    MSG  msg;

    memset( (char *) &msg, 0, sizeof(msg) );
    msg.mFloat.d[0]= a; msg.mFloat.d[1]= b; msg.mFloat.d[2]= c;
    return msg;
}
// ------------------------------
void printFrame (const char *prefix, FRAME *frame)
{
    MSG  msg= frame->msg;
    
    printf ("%s [%s] ", prefix, KINDNAME[frame->kind]);
    switch (frame->kind) {
    case STR:
        printf ("'%s' '%s' '%s'",
	   	 msg.mStr.d[0], msg.mStr.d[1], msg.mStr.d[2]);
        break;
     case INT:
         printf ("%d, %d, %d",
	   	  msg.mInt.d[0], msg.mInt.d[1], msg.mInt.d[2]);
         break;
      case FLOAT:
          printf ("%f, %f, %f",
	   	   msg.mFloat.d[0], msg.mFloat.d[1], msg.mFloat.d[2]);
          break;		   
      case ACK: case DONE:
          break;
      default:
          WARNING ("Unknown frame type (%d)\n", frame->kind);
	  break;
      }
      printf("\n");
}
//newpage
// ------------------------------
int sendFrame (int fd, KIND kind, MSG *msg)
{
    int    len;
    FRAME  frame;

    assert (fd >= 0);
    memset( (char *) &frame, 0, sizeof(frame) );
    frame.kind= kind;
    frame.msg=  *msg;

    len= write (fd, (char *) &frame, sizeof(frame));

    if (len == 0) WARNING ("sendFrame: sent frame has zero length. \n");
    else if (len != sizeof(frame))
        WARNING ("sendFrame: sent frame has length= %d (expected= %d)\n",
	          len, sizeof(frame));
    return len;		  
}
// ------------------------------
int rcvFrame (int fd, FRAME *framep)
{
    int    len; 
    FRAME  frame;

    assert (fd >= 0);
    memset( (char *) &frame, 0, sizeof(frame) );

    len= read (fd, (char *) &frame, sizeof(frame));
    *framep= frame;
    
    if (len == 0) WARNING ("rcvFrame: received frame has zero length \n");
    else if (len != sizeof(frame))
        WARNING ("rcvFrame: received frame has length= %d (expected= %d)\n",
		  len, sizeof(frame));
    return len;
}

// ------------------------------
// testDone: return 0 if all done flags are -1 (i.e., no client has started),
//           or if at least one done flag is zero (i.e., at least one client
// 	     is still working)

int testDone (int done[], int nClient)
{
    int  i, sum= 0;

    if (nClient == 0) return 0;
    
    for (i= 1; i <= nClient; i++) sum += done[i];
    if (sum == -nClient) return 0;

    for (i= 1; i <= nClient; i++) { if (done[i] == 0) return 0; }
    return 1;
}    
    

//newpage
// ------------------------------
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
    int                 sfd;
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
        FATAL ("serverListen: bind failed \n");

    // indicate how many connection requests can be queued

    listen (sfd, nClient);
    return sfd;
}

//newpage
// ------------------------------
void do_client (char *serverArg, int portNo)
{
    int    i, len, sfd;
    char   serverName[MAXWORD];
    FRAME  frame;
    MSG    msg;
    struct  timespec delay;
    
    strcpy (serverName, serverArg);
    printf ("do_client: trying server '%s', portNo= %d\n", serverName, portNo);
    sfd= clientConnect (serverName, portNo);
    printf ("do_client: connected \n");

    delay.tv_sec=  0;
    delay.tv_nsec= 900E+6;	// 900 millsec (this field can't be 1 second)
    
    for (i= 1; i < NITER; i++) {
        msg= composeMSTR ("Edmonton", "Red Deer", "Calgary");
        len= sendFrame (sfd, STR, &msg);
        len= rcvFrame(sfd, &frame);  printFrame("received ", &frame);

        msg= composeMINT (10, 20, 30);
        len= sendFrame (sfd, INT, &msg);
        len= rcvFrame(sfd, &frame);  printFrame("received ", &frame);

        msg= composeMFLOAT (10.25, 20.50, 30.75);
        len= sendFrame (sfd, FLOAT, &msg);
	len= rcvFrame(sfd, &frame);  printFrame("received ", &frame);

	printf ("----------\n");
        nanosleep(&delay,NULL);
    }

    msg= composeMINT(0,0,0);
    len= sendFrame (sfd, DONE, &msg);
    len= rcvFrame(sfd, &frame);  printFrame("received ", &frame);

    printf("do_client: closing socket \n"); close(sfd);
}

//newpage
// ------------------------------
// do_sever:  clients are numbered i= 1,2, ..., NCLIENT.
//            The done[] flags:
//            done[i]= -1 when client i has not connected
//                   =  0 when client i has connected but not finished
//                   = +1 when client i has finished or server lost connection
//
void do_server ()
{
    int   i, N, len, rval, timeout, done[NCLIENT+1];
    int   newsock[NCLIENT+1];
    char  buf[MAXBUF], line[MAXLINE];
    MSG   msg;
    FRAME frame;

    struct pollfd       pfd[NCLIENT+1];
    struct sockaddr_in  from;
    socklen_t           fromlen;

    for (i= 0; i <= NCLIENT; i++) done[i]= -1;

    // prepare for noblocking I/O polling from the managing socket
    timeout= 0;
    pfd[0].fd=  serverListen (MYPORT, NCLIENT);;
    pfd[0].events= POLLIN;
    pfd[0].revents= 0;

    printf ("Server is accepting connections (port= %d)\n", MYPORT);
    
    N= 1;		// N descriptors to poll
    while(1) {
       rval= poll (pfd, N, timeout);
       if ( (N < NCLIENT+1) && (pfd[0].revents & POLLIN) ) {
           // accept a new connection
	   fromlen= sizeof(from);
	   newsock[N]= accept(pfd[0].fd, (SA *) &from, &fromlen);

           pfd[N].fd= newsock[N];
	   pfd[N].events= POLLIN;
	   pfd[N].revents= 0;
	   done[N]= 0;
	   N++;
       }

       // check client sockets
       //
       for (i= 1; i <= N-1; i++) {
           if ((done[i] == 0) && (pfd[i].revents & POLLIN)) { 
	       len= rcvFrame(pfd[i].fd, &frame);

               memset(line,0,MAXLINE);
	       sprintf(line, "received from pollfd[i= %d]= %d (len= %d): ",
	       		      i, pfd[i].fd, len);
	       if (len == 0) {
	           printf ("server lost connection with client\n");
		   done[i]= 1;
		   continue;      // start a new for iteration
	       }	   

               printFrame (line, &frame);
               if (frame.kind == DONE)
	           {printf ("%s: Done\n", line); done[i]= 1;}
	       
               sendFrame(pfd[i].fd, ACK, &msg);
          }	       
       }

       if (testDone(done,N-1) == 1) break;
    }	   

    printf ("do_server: server closing main socket (");
    for (i=1 ; i <= N-1; i++) printf ("done[%d]= %d, ", i, done[i]);
    printf (") \n");
    for (i=1; i <= N-1; i++) close(pfd[i].fd);
    close(pfd[0].fd);
}    

// ------------------------------
int main (int argc, char *argv[])
{
    if (argc < 2)  FATAL ("Usage: %s [-c|-s] serverName \n", argv[0]);

    if ( strstr(argv[1], "-c") != NULL) {
        if (argc != 3) FATAL ("Usage: %s -c serverName \n");
	do_client(argv[2],MYPORT);
    }	
    else if ( strstr(argv[1], "-s") != NULL) do_server();

}