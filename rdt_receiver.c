#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/time.h>
#include <assert.h>

#include "common.h"
#include "packet.h"

/*
 * You are required to change the implementation to support
 * window size greater than one.
 * In the current implementation the window size is one, hence we have
 * only one send and receive packet
 */
tcp_packet *recvpkt;
tcp_packet *oldsndpkt;
tcp_packet *sndpkt;

int main(int argc, char **argv)
{
    int sockfd;                    /* socket */
    int portno;                    /* port to listen on */
    int clientlen;                 /* byte size of client's address */
    struct sockaddr_in serveraddr; /* server's addr */
    struct sockaddr_in clientaddr; /* client addr */
    int optval;                    /* flag value for setsockopt */
    FILE *fp;
    char buffer[MSS_SIZE];
    struct timeval tp;

    /*
     * check command line arguments
     */
    if (argc != 3)
    {
        fprintf(stderr, "usage: %s <port> FILE_RECVD\n", argv[0]);
        exit(1);
    }
    portno = atoi(argv[1]);

    fp = fopen(argv[2], "w");
    if (fp == NULL)
    {
        error(argv[2]);
    }

    /*
     * socket: create the parent socket
     */
    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0)
        error("ERROR opening socket");

    /* setsockopt: Handy debugging trick that lets
     * us rerun the server immediately after we kill it;
     * otherwise we have to wait about 20 secs.
     * Eliminates "ERROR on binding: Address already in use" error.
     */
    optval = 1;
    setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR,
               (const void *)&optval, sizeof(int));

    /*
     * build the server's Internet address
     */
    bzero((char *)&serveraddr, sizeof(serveraddr));
    serveraddr.sin_family = AF_INET;
    serveraddr.sin_addr.s_addr = htonl(INADDR_ANY);
    serveraddr.sin_port = htons((unsigned short)portno);

    // write structure to save out of order packet into buffer array
    struct outorder
    {
        int seqnum;
        tcp_packet *out;
    };
    struct outorder outoforder[20000];
    // keep track of how many out of order packet there is
    int howmany = 0;

    /*
     * bind: associate the parent socket with a port
     */
    if (bind(sockfd, (struct sockaddr *)&serveraddr,
             sizeof(serveraddr)) < 0)
        error("ERROR on binding");

    /*
     * main loop: wait for a datagram, then echo it
     */
    VLOG(DEBUG, "epoch time, bytes received, sequence number");

    clientlen = sizeof(clientaddr);

    //initiating sndpkt and oldsndpkt
    sndpkt = make_packet(0);
    oldsndpkt=make_packet(0);
    while (1)
    {
        /*
         * recvfrom: receive a UDP datagram from a client
         */
        //VLOG(DEBUG, "waiting from server \n");
        if (recvfrom(sockfd, buffer, MSS_SIZE, 0,
                     (struct sockaddr *)&clientaddr, (socklen_t *)&clientlen) < 0)
        {
            error("ERROR in recvfrom");
        }
        recvpkt = (tcp_packet *)buffer;
        printf("%d\n", recvpkt->hdr.seqno);
        assert(get_data_size(recvpkt) <= DATA_SIZE);
        if (recvpkt->hdr.data_size == 0)
        {
            VLOG(INFO, "End Of File has been reached");
            fclose(fp);
            break;
        }
        /*
         * sendto: ACK back to the client
         */
        gettimeofday(&tp, NULL);
        VLOG(DEBUG, "%lu, %d, %d", tp.tv_sec, recvpkt->hdr.data_size, recvpkt->hdr.seqno);
       
        //if there are packets in the buffer already
        if (howmany != 0)
        {
            //loop through each packet in buffer to check for orders into the file
            for (int i = 0; i < howmany; i++)
            {
                //break loop if buffer not proper saving packets
                if ((&outoforder[i]) ->out== NULL)
                {   
                    printf("the packet didn't get safe into the buffer\n");
                    break;
                } 
                //write into the file based on sequencing
                    if (recvpkt->hdr.seqno==oldsndpkt->hdr.ackno)
                        {
                            // write into file only when receving correct sequence packet
                            fseek(fp, recvpkt->hdr.seqno, SEEK_SET);
                            fwrite(recvpkt->data, 1, recvpkt->hdr.data_size, fp);
                            printf(" %s \n", "missing packet received correct sequence no buffer");
                            sndpkt->hdr.ackno = recvpkt->hdr.seqno + recvpkt->hdr.data_size;
                            sndpkt->hdr.ctr_flags = ACK;
                            oldsndpkt->hdr.ackno=sndpkt->hdr.ackno;
                            oldsndpkt->hdr.ctr_flags=sndpkt->hdr.ctr_flags;
                            printf(" recv hdr seqno %d \n", recvpkt->hdr.seqno);
                            printf(" send ackno %d \n", sndpkt->hdr.ackno);
                            printf("writing into file %d ", recvpkt->hdr.seqno);
                            if ((&outoforder[i])->seqnum == recvpkt->hdr.seqno + recvpkt->hdr.data_size)
                    {
                        // write into file only when receving correct sequence packet
                        printf("out of order packet number %d into file HERES\n", recvpkt->hdr.seqno);
                        fseek(fp, (&outoforder[i])->seqnum, SEEK_SET);
                        fwrite((&outoforder[i])->out->data, 1, (&outoforder[i])->out->hdr.data_size, fp);
                        // cumulative ACK
                        
                        sndpkt->hdr.ackno = (&outoforder[i])->out->hdr.seqno + (&outoforder[i])->out->hdr.data_size;
                        sndpkt->hdr.ctr_flags = ACK;
                        oldsndpkt->hdr.ackno=sndpkt->hdr.ackno;
                        oldsndpkt->hdr.ctr_flags=sndpkt->hdr.ctr_flags;
                        printf(" recv hdr seqno %d \n", recvpkt->hdr.seqno);
                        printf(" send ackno %d \n", sndpkt->hdr.ackno);

                        if (sendto(sockfd, sndpkt, TCP_HDR_SIZE, 0,
                                   (struct sockaddr *)&clientaddr, clientlen) < 0)
                        {
                            error("ERROR in sendto");
                        }
                        /*send the packet of ACK the original ACk sequence number */
                    }
                            if (sendto(sockfd, sndpkt, TCP_HDR_SIZE, 0,
                                    (struct sockaddr *)&clientaddr, clientlen) < 0)
                            {
                                error("ERROR in sendto");
                            }
                            /*send the packet of ACK the original ACk sequence number */
                        }
                    else
                    {
                        // buffer the packet
                        if (recvpkt->hdr.seqno != sndpkt->hdr.ackno)
                        {
                            if (recvpkt->hdr.seqno==outoforder[howmany-1].seqnum){
                                break;
                            }
                            printf("out of order packet number %d into buffer (packet buffering)\n", recvpkt->hdr.seqno);

                            outoforder[howmany].seqnum = recvpkt->hdr.seqno;
                            outoforder[howmany].out = recvpkt;
                            howmany=howmany+1;
                            printf(" recv hdr seqno %d \n", recvpkt->hdr.seqno);
                            printf(" send ackno %d \n", oldsndpkt->hdr.ackno);
                            // send the old acknowledgement info
                            if (sendto(sockfd, oldsndpkt, TCP_HDR_SIZE, 0,
                                    (struct sockaddr *)&clientaddr, clientlen) < 0)
                            {
                                error("ERROR in sendto");
                            }
                        }
                    }   
            }
        }
        else
        {
            if (recvpkt->hdr.seqno != sndpkt->hdr.ackno)
            {
                printf("out of order packet number %d into buffer\n", recvpkt->hdr.seqno);
                outoforder[howmany].seqnum = recvpkt->hdr.seqno;
                outoforder[howmany].out = recvpkt;
                howmany++;
                printf(" recv hdr seqno %d \n", recvpkt->hdr.seqno);
                printf(" send ackno %d \n", oldsndpkt->hdr.ackno);

                // send the old acknowledgement info
                if (sendto(sockfd, oldsndpkt, TCP_HDR_SIZE, 0,
                           (struct sockaddr *)&clientaddr, clientlen) < 0)
                {
                    error("ERROR in sendto");
                }
            }
            else
            {
                // write into file only when receving correct sequence packet
                fseek(fp, recvpkt->hdr.seqno, SEEK_SET);
                fwrite(recvpkt->data, 1, recvpkt->hdr.data_size, fp);

                
                sndpkt->hdr.ackno = recvpkt->hdr.seqno + recvpkt->hdr.data_size;
                sndpkt->hdr.ctr_flags = ACK;
                oldsndpkt->hdr.ackno=sndpkt->hdr.ackno;
                oldsndpkt->hdr.ctr_flags=sndpkt->hdr.ctr_flags;
                printf(" recv hdr seqno %d \n", recvpkt->hdr.seqno);
                printf(" send ackno %d \n", sndpkt->hdr.ackno);

                if (sendto(sockfd, sndpkt, TCP_HDR_SIZE, 0,
                           (struct sockaddr *)&clientaddr, clientlen) < 0)
                {
                    error("ERROR in sendto");
                }
                /*send the packet of ACK the original ACk sequence number */
            }
        }
    }

    return 0;
}
