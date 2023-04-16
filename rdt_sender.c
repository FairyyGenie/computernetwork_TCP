#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h> 
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <signal.h>
#include <sys/time.h>
#include <time.h>
#include <assert.h>

#include"packet.h"
#include"common.h"

#define STDIN_FD    0
#define RETRY  120 //millisecond

int next_seqno=0;
int send_base=0;
int window_size = 10;
int timedOut = 0;
int eof = 0;

int sockfd, serverlen;
struct sockaddr_in serveraddr;
struct itimerval timer; 
tcp_packet *sndpkt;
tcp_packet *recvpkt;
sigset_t sigmask;       
int firstByteInWindow = 0;
int length;
char buffer[DATA_SIZE];
FILE *fp;

int acks[200000];

void start_timer()
{
    sigprocmask(SIG_UNBLOCK, &sigmask, NULL);
    setitimer(ITIMER_REAL, &timer, NULL);
}


void stop_timer()
{
    sigprocmask(SIG_BLOCK, &sigmask, NULL);
}

void resend_packets(int sig)
{
    if (sig == SIGALRM)
    {
        if(eof == 1){
            exit(0);
        }
        //Resend all packets range between 
        //sendBase and nextSeqNum
        fseek(fp, SEEK_SET, firstByteInWindow);
        length = fread(buffer, 1, DATA_SIZE, fp);
 		sndpkt = make_packet(length);
        VLOG(INFO, "Timeout happened");
        if(sendto(sockfd, sndpkt, TCP_HDR_SIZE + get_data_size(sndpkt), 0, 
                    ( const struct sockaddr *)&serveraddr, serverlen) < 0)
        {
            error("sendto");
        }
        //open the file if it's not already open, go back to the sendbase, retransmit all bytes from there. 
    }
    timedOut = 1;
    stop_timer();
}



/*
 * init_timer: Initialize timer
 * delay: delay in milliseconds
 * sig_handler: signal handler function for re-sending unACKed packets
 */
void init_timer(int delay, void (*sig_handler)(int)) 
{
    signal(SIGALRM, sig_handler);
    timer.it_interval.tv_sec = delay / 1000;    // sets an interval of the timer
    timer.it_interval.tv_usec = (delay % 1000) * 1000;  
    timer.it_value.tv_sec = delay / 1000;       // sets an initial value
    timer.it_value.tv_usec = (delay % 1000) * 1000;

    sigemptyset(&sigmask);
    sigaddset(&sigmask, SIGALRM);
}


int main (int argc, char **argv)
{
    for(int i = 0; i < 200000; i++){
    acks[i] = 0;
    }
    int portno;
    int next_seqno;
    char *hostname;

    /* check command line arguments */
    if (argc != 4) {
        fprintf(stderr,"usage: %s <hostname> <port> <FILE>\n", argv[0]);
        exit(0);
    }
    hostname = argv[1];
    portno = atoi(argv[2]);
    fp = fopen(argv[3], "r");
    if (fp == NULL) {
        error(argv[3]);
    }

    /* socket: create the socket */
    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) 
        error("ERROR opening socket");


    /* initialize server server details */
    bzero((char *) &serveraddr, sizeof(serveraddr));
    serverlen = sizeof(serveraddr);

    /* covert host into network byte order */
    if (inet_aton(hostname, &serveraddr.sin_addr) == 0) {
        fprintf(stderr,"ERROR, invalid host %s\n", hostname);
        exit(0);
    }

    /* build the server's Internet address */
    serveraddr.sin_family = AF_INET;
    serveraddr.sin_port = htons(portno);

    assert(MSS_SIZE - TCP_HDR_SIZE > 0);

    //Stop and wait protocol

    init_timer(RETRY, resend_packets);
    next_seqno = 0;
    int windowCreated = 0;
    //bytes[i] is the last byte that packet NUMBER i should contain. 
    int bytes[2048];
    bzero(bytes, sizeof(bytes));
 	int length;
 	int lastByteinWindow;
 	int packetBase = 0;
    while (1)
    {
    	timedOut = 0;
    	if(!windowCreated){
    		for(int i = 0; i < 10; i++){
    			length = fread(buffer, 1, DATA_SIZE, fp);
                if (length <= 0){
                    VLOG(INFO, "End Of File has been reached");
                    sndpkt = make_packet(0);
                    sendto(sockfd, sndpkt, TCP_HDR_SIZE,  0,
                            (const struct sockaddr *)&serveraddr, serverlen);
                    eof = 1;
                    break;
                }
 				bytes[i+1] = bytes[i] + length;
 				sndpkt = make_packet(length);
                sndpkt->hdr.seqno=bytes[i];
                printf("seq no: %d\n", sndpkt->hdr.seqno);
                memcpy(sndpkt->data, buffer, length);
 				if(sendto(sockfd, sndpkt, TCP_HDR_SIZE + get_data_size(sndpkt), 0, ( const struct sockaddr *)&serveraddr, serverlen) < 0){
                	error("sendto");
            	}	
    		}
    	windowCreated = 1;
    	}
        if(windowCreated){
            printf("%s\n", "sliding the window");
        }
    	lastByteinWindow = bytes[packetBase+window_size];
        firstByteInWindow = bytes[packetBase]+1;
    	do{
    		if(recvfrom(sockfd, buffer, MSS_SIZE, 0, (struct sockaddr *) &serveraddr, (socklen_t *)&serverlen) < 0){
                    error("recvfrom");
                }
                recvpkt = (tcp_packet *)buffer;
                printf("%d\n", recvpkt->hdr.ackno);
    	}
    	while(recvpkt->hdr.ackno < bytes[packetBase]);
    	if(!timedOut){
    		packetBase++;
    		length = fread(buffer, 1, DATA_SIZE, fp);
            if (length <= 0){
                VLOG(INFO, "End Of File has been reached");
                sndpkt = make_packet(0);
                sendto(sockfd, sndpkt, TCP_HDR_SIZE,  0,
                        (const struct sockaddr *)&serveraddr, serverlen);
                eof = 1;
                break;
            }
			bytes[packetBase+window_size] = bytes[packetBase+window_size-1] + length;
			sndpkt = make_packet(length);
            sndpkt->hdr.seqno = bytes[packetBase+window_size];
            printf("seq no: %d\n", sndpkt->hdr.seqno);
            memcpy(sndpkt->data, buffer, length);
			if(sendto(sockfd, sndpkt, TCP_HDR_SIZE + get_data_size(sndpkt), 0, ( const struct sockaddr *)&serveraddr, serverlen) < 0){
            	error("sendto");
            }
            free(sndpkt);	
    	}
    }
    return 0;
}



