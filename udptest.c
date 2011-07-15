// A simple test program for UDP reception
// Reads data over UDP, streamed by udpserve, and checks for dropped packets
//
// Author: Tony J Ibbs
// Date: 2005-03-31

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <unistd.h>      // open, close

static int udp_listen_socket(char *hostname, int port)
{
  struct hostent *hp;
  int sock;
  const int one = 1;
  int multicast;
  struct sockaddr_in ipaddr;

  printf("Connecting to %s on port %d\n",hostname,port);

  hp = gethostbyname(hostname);
  if (!hp)
  {
    perror(hostname);
    fprintf(stderr, "Invalid host address");
    return -1;
  }
  memcpy(&ipaddr.sin_addr, hp->h_addr, hp->h_length);
  ipaddr.sin_family = AF_INET;
  ipaddr.sin_port = htons(port);

  if ((sock = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP)) == -1)
  {
    perror("socket");
    fprintf(stderr, "Can't create socket");
    return -1;
  }

  // Is this a multicast address?
  multicast =  IN_CLASSD(ntohl(ipaddr.sin_addr.s_addr));

  if (multicast)
  {
    printf("Address is multicast\n");
    if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (char *) &one,
                   sizeof(one)) < 0)
    {
      perror("setsockopt: reuseaddr");
    }
  }
  else
  {
    // This is unicast, so address in the URL is not useful
    // on bind - it needs to specify our local address
    ipaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    printf("Address is unicast\n");
  }

  if (bind(sock, (struct sockaddr *)&ipaddr, sizeof(ipaddr)) < 0)
  {
    perror("bind");
    close(sock);
    return -1;
  }

  // For multicast, need to join the group.
  if (multicast)
  {
    struct ip_mreq mreq;
    // Multicast - need to listen on an interface.
    // INADDR_ANY picks the default, OK if only one interface
    mreq.imr_interface.s_addr = htonl(INADDR_ANY);
    mreq.imr_multiaddr = ipaddr.sin_addr;

    if (setsockopt(sock, IPPROTO_IP, IP_ADD_MEMBERSHIP,
                   (char *)&mreq, sizeof(mreq)) < 0)
    {
      perror("IP_ADD_MEMBERSHIP");
      close(sock);
      return -1;
    }
  }
  return sock;
}

int main(int argc, char **argv)
{
#define TS_PACKET_SIZE 188

  char  *hostname;
  char  *colon;
  int    port;
  int    sock;
  int    max = 0;  // == forever
  int    mult = 1;
  int    packet_size;
  unsigned char data[100*TS_PACKET_SIZE];
  unsigned int last_packet_number = 0;
  int had_first_packet = 0;
  unsigned int total_packets = 0;
  unsigned int total_lost = 0;
  int quiet = 0;
#if 0
  unsigned long total_bytes = 0;
  unsigned long past_delay = 0;
#endif

  if (argc < 2)
  {
    fprintf(stderr,
            "Usage: %s <ipaddr>[:<port>] [<mult>] [<max>] [q]\n\n"
            "<port> defaults to 88.\n"
            "<mult> is the packet size in units of 188 (so data is <mult>*188 bytes)\n"
            "<max> is the number of packets to read before stopping\n"
            "(if not given, or 0, read forever)\n"
            "'q' means don't give individual error messages for dropped packets\n",
            argv[0]);
    return 1;
  }

  hostname = argv[1];

  if (argc > 2)
  {
    mult = atoi(argv[2]);
    if (mult < 0)
    {
      printf("Packet size multiplier %d does not make sense\n",mult);
      return 1;
    }
  }
  packet_size = mult * TS_PACKET_SIZE;

  if (argc > 3)
  {
    max = atoi(argv[3]);
    if (max < 0)
    {
      printf("Maximum number of packets %d does not make sense\n",max);
      return 1;
    }
  }

  if (argc > 4)
  {
    if (argv[4][0] == 'q')
      quiet = 1;
    else
    {
      fprintf(stderr,"Unrecognised '%s'\n",argv[4]);
      return 1;
    }
  }

  if ((colon = strchr(hostname, ':')))
  {
    *colon = '\0';
    port = atoi(colon + 1);
  }
  else
    port = 88;

  sock = udp_listen_socket(hostname,port);
  if (sock < 0) return 1;

  for (;;)
  {
#if 0
    long   delay_wanted;
#endif
    unsigned int this_packet_number = 0;
    size_t len = recv(sock, data, packet_size, MSG_WAITALL);
    if (len < 0)
    {
      perror("Error in recv");
      break;
    }
    if (len == 0)
    {
      printf("End of file\n");
      break;
    }

    if (len != packet_size)
    {
      printf("Read packet of unexpected size %d (expected %d)\n",len,packet_size);
    }

    this_packet_number = data[3];
    this_packet_number = (this_packet_number << 8) | data[2];
    this_packet_number = (this_packet_number << 8) | data[1];
    this_packet_number = (this_packet_number << 8) | data[0];

    if (!quiet)
      printf("%6d: got packet %08u",total_packets+1,this_packet_number);

    if (!had_first_packet)
    {
      had_first_packet = 1;
      if (!quiet)
        printf(" (first packet)");
    }
    else
    {
      if (this_packet_number != (last_packet_number + 1))
      {
          if (!quiet)
            printf(", expected packet %08u (missed %3d)",
                   last_packet_number+1,this_packet_number - (last_packet_number+1));
          total_lost += (this_packet_number - (last_packet_number+1));
      }
    }
    if (!quiet)
      printf("\n");
    last_packet_number = this_packet_number;
    total_packets ++;
    if (max != 0 && total_packets >= max)
      break;

#if 0
    // Impose a delay to simulate displaying data
    total_bytes += len;
    delay_wanted = (total_bytes - 2000000) * 4 - past_delay;
    //printf("delay: %d\n",delay_wanted);
    if (delay_wanted > 20000)
    {
      printf(".");fflush(stdout);
      usleep(delay_wanted);
      past_delay += delay_wanted;
    }
#endif
  }
  printf("Total number of packets received: %d\n",total_packets);
  printf("Minimum number of packets lost:   %d\n",total_lost);

  close(sock);
  return 0;
}
