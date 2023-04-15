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
tcp_packet *oldrecvpkt;
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
    struct outorder outoforder[1000];
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
    while (1)
    {
        printf("%s\n", "did we enter the while loop?");
        /*
         * recvfrom: receive a UDP datagram from a client
         */
        VLOG(DEBUG, "waiting from server \n");
        if (recvfrom(sockfd, buffer, MSS_SIZE, 0,
                     (struct sockaddr *)&clientaddr, (socklen_t *)&clientlen) < 0)
        {
            error("ERROR in recvfrom");
        }
        recvpkt = (tcp_packet *)buffer;
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
        /*if condition to screen whether it is out of order packet and if so
        we need to buffer the packet
        the buffer needs to be a priority queue and
        so when we write it back to the file it is in order*/
        printf("%d\n", howmany);
        sndpkt = make_packet(0);
        if (howmany != 0)
        {
            printf("%s\n", "did we enter the if loop?");
            for (int i = 0; i < howmany; i++)
            {
                printf("%s\n","here here here the for loop");
                if ((&outoforder[i]) != NULL)
                {
                    printf("%s\n","here here here");
                    if ((&outoforder[i])->seqnum == recvpkt->hdr.seqno + recvpkt->hdr.data_size)
                    {
                        // write into file only when receving correct sequence packet
                        fseek(fp, recvpkt->hdr.seqno, SEEK_SET);
                        fwrite(recvpkt->data, 1, recvpkt->hdr.data_size, fp);
                        printf("out of order packet number %d into file", recvpkt->hdr.seqno);
                        fseek(fp, (&outoforder[i])->seqnum, SEEK_SET);
                        fwrite((&outoforder[i])->out->data, 1, (&outoforder[i])->out->hdr.data_size, fp);
                        // cumulative ACK
                        
                        sndpkt->hdr.ackno = (&outoforder[i])->out->hdr.seqno + (&outoforder[i])->out->hdr.data_size;
                        sndpkt->hdr.ctr_flags = ACK;
                        if (sendto(sockfd, sndpkt, TCP_HDR_SIZE, 0,
                                   (struct sockaddr *)&clientaddr, clientlen) < 0)
                        {
                            error("ERROR in sendto");
                        }
                        /*send the packet of ACK the original ACk sequence number */
                    }
                }
                else
                {
                    printf("%s\n","here here 2222");
                    // buffer the packet
                    if (recvpkt->hdr.seqno != sndpkt->hdr.ackno)
                    {
                        printf("out of order packet number %d into buffer", recvpkt->hdr.seqno);
                        outoforder[howmany].seqnum = recvpkt->hdr.seqno;
                        outoforder[howmany].out = recvpkt;
                        howmany++;
                        // send the old acknowledgement info
                        if (sendto(sockfd, sndpkt, TCP_HDR_SIZE, 0,
                                   (struct sockaddr *)&clientaddr, clientlen) < 0)
                        {
                            error("ERROR in sendto");
                        }
                    }
                    else
                    {
                        printf("%s\n", "are we here?");
                        // write into file only when receving correct sequence packet
                        fseek(fp, recvpkt->hdr.seqno, SEEK_SET);
                        fwrite(recvpkt->data, 1, recvpkt->hdr.data_size, fp);

                        
                        sndpkt->hdr.ackno = recvpkt->hdr.seqno + recvpkt->hdr.data_size;
                        sndpkt->hdr.ctr_flags = ACK;
                        printf("writing into file %d ", recvpkt->hdr.seqno);
                        if (sendto(sockfd, sndpkt, TCP_HDR_SIZE, 0,
                                   (struct sockaddr *)&clientaddr, clientlen) < 0)
                        {
                            error("ERROR in sendto");
                        }
                        /*send the packet of ACK the original ACk sequence number */
                    }
                }
            }
        }
        else
        {
            printf("%s\n","here here are we okay?");
            if (recvpkt->hdr.seqno != sndpkt->hdr.ackno)
            {
                printf("out of order packet number %d into buffer", recvpkt->hdr.seqno);
                outoforder[howmany].seqnum = recvpkt->hdr.seqno;
                outoforder[howmany].out = recvpkt;
                howmany++;
                // send the old acknowledgement info
                if (sendto(sockfd, sndpkt, TCP_HDR_SIZE, 0,
                           (struct sockaddr *)&clientaddr, clientlen) < 0)
                {
                    error("ERROR in sendto");
                }
            }
            else
            {
                printf("%s\n","here here this might be the first?");
                // write into file only when receving correct sequence packet
                fseek(fp, recvpkt->hdr.seqno, SEEK_SET);
                fwrite(recvpkt->data, 1, recvpkt->hdr.data_size, fp);

                
                sndpkt->hdr.ackno = recvpkt->hdr.seqno + recvpkt->hdr.data_size;
                sndpkt->hdr.ctr_flags = ACK;
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