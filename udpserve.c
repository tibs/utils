/*
 * A simple UDP server, sending identifiable packets so the client can
 * tell if packets are being lost.
 *
 * (this could easily be made part of the MPEG tools, but I've kept it
 * separate for the moment for simplicity, and because I'm not sure it's
 * generally useful enough)
 *
 * Author: Tony J. Ibbs
 * Date: 2005-03-31
 */

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>

#include <sys/wait.h>
#include <sys/types.h>
#include <netdb.h>       // gethostbyname
#include <sys/socket.h>  // send
#include <arpa/inet.h>   // inet_aton
#include <netinet/in.h>  // sockaddr_in
#include <unistd.h>      // open, close
#include <sys/time.h>    // gettimeofday

#define TS_PACKET_SIZE 188

static void write_socket_data(int           output,
                              unsigned char data[],
                              int           data_len,
                              unsigned int  packet_number)
{
  ssize_t  written = 0;
  ssize_t  left    = data_len;
  int      start   = 0;

  // (When writing to a file, we don't expect to ever write less than
  // the requested number of bytes. However, if `output` is a socket,
  // it is possible that the underlying buffering might cause a
  // partial write.)
  errno = 0;
  while (left > 0)
  {
    written = send(output,&(data[start]),left,0);
    if (written == -1)
    {
      if (errno == ENOBUFS)
      {
        fprintf(stderr,"!!! Warning: 'no buffer space available' writing out"
                " packet %u - retrying\n",packet_number);
        errno = 0;
      }
      else
      {
        fprintf(stderr,"### Error writing out packet %u: %s\n",
                packet_number,strerror(errno));
        return; // i.e., just give up on this packet
      }
    }
    left -= written;
    start += written;
  }
  return;
}

extern int connect_udp_socket(char *hostname,
                              int   port,
                              char *multicast_ifaddr)
{
  int output;
  int result;
  struct hostent *hp;
  struct sockaddr_in ipaddr;

  printf("Connecting to %s via UDP\n",hostname);

  // SOCK_DGRAM => UDP
  output = socket(AF_INET, SOCK_DGRAM, 0);
  if (output == -1)
  {
    fprintf(stderr,"### Unable to create socket: %s\n",strerror(errno));
    return -1;
  }

  hp = gethostbyname(hostname);
  if (hp == NULL)
  {
    fprintf(stderr,"### Unable to resolve host %s: %s\n",
            hostname,strerror(h_errno));
    return -1;
  }
  memcpy(&ipaddr.sin_addr.s_addr, hp->h_addr, hp->h_length);
  ipaddr.sin_family = hp->h_addrtype;
#if !defined(__linux__)
  // On BSD, the length is defined in the datastructure
  ipaddr.sin_len = sizeof(struct sockaddr_in);
#endif // __linux__
  ipaddr.sin_port = htons(port);

  if (IN_CLASSD(ntohl(ipaddr.sin_addr.s_addr)))
  {
    // Needed if we're doing multicast
    unsigned char ttl = 16;
    result = setsockopt(output, IPPROTO_IP, IP_MULTICAST_TTL,
                        (char *)&ttl, sizeof(ttl));
    if (result < 0)
    {
      fprintf(stderr,
              "### Error setting socket for IP_MULTICAST_TTL: %s\n",
              strerror(errno));
      return -1;
    }
    printf("Connection is multicast\n");

    if (multicast_ifaddr)
    {
      struct in_addr addr;
      inet_aton(multicast_ifaddr, &addr);
      result = setsockopt(output,IPPROTO_IP,IP_MULTICAST_IF,
		         (char *)&addr,sizeof(addr));
      if (result < 0)
      {
        fprintf(stderr,"### Unable to set multicast interface %s: %s\n",
                multicast_ifaddr,strerror(errno));
        return -1;
      }
      printf("Using multicast interface %s\n",multicast_ifaddr);
    }
  }

  result = connect(output,(struct sockaddr*)&ipaddr,sizeof(ipaddr));
  if (result < 0)
  {
    fprintf(stderr,"### Unable to connect to host %s: %s\n",
            hostname,strerror(errno));
    return -1;
  }
  printf("Connected  to %s on socket %d\n",hostname,output);
  return output;
}

int main(int argc, char **argv)
{
  char *hostname;
  char *p;
  long  port = 88;
  long  mult = 1;
  char *multicast_if = NULL;
  int   socket;
  int   ii;
  unsigned char data[100*TS_PACKET_SIZE];
  int           data_len;
  unsigned int  packet_number = 0;
  unsigned long delay = 1;
  int every = 0;
  struct timeval then;
  struct timeval now;

  if (argc < 2)
  {
    fprintf(stderr,
            "Usage: udpserve <host>[:<port>] [-mult <mult>] [-if <interface>] [-delay <n>] [-every <n>]\n"
            "\n"
            "    <host> is the host to send data to, <port> defaults to 88\n"
            "\n"
            "    If '-mult' is given, it indicates that packets of size <mult>*188\n"
            "    bytes will be served. <mult> defaults to 1, and must be 1..20\n"
            "\n"
            "    If '-if' is given, and <host> is a multicast address, then\n"
            "    <interface> is the IP address of the network interface to use.\n"
            "\n"
            "    If '-delay' is given, then usleep(<n>) will be called between packets\n"
            "    (the default is usleep(1)). '-delay 0' means no delay.\n"
            "\n"
            "    If '-every' is given, only sleep after every <n>th packet\n"
            "    (the default is every 1, after every packet).\n"
           );
    return 1;
  }

  hostname = argv[1];
  p = strchr(hostname,':');

  if (p != NULL)
  {
    char *ptr;
    p[0] = '\0';  // nb: modifying argv[1]
    errno = 0;
    port = strtol(p+1,&ptr,10);
    if (errno)
    {
      p[0] = ':';
      fprintf(stderr,"### Cannot read port number in %s (%s)\n",
              hostname,strerror(errno));
      return 1;
    }
    if (ptr[0] != '\0')
    {
      p[0] = ':';
      fprintf(stderr,"### Unexpected characters in port number in %s\n",
              hostname);
      return 1;
    }
    if (port < 0)
    {
      p[0] = ':';
      fprintf(stderr,"### Negative port number in %s\n",hostname);
      return 1;
    }
  }

  ii = 2;
  while (ii < argc)
  {
    if (!strcmp("-mult",argv[ii]))
    {
      mult = atoi(argv[ii+1]);
      if (mult <= 0)
      {
        fprintf(stderr,"### Packet size multiplier %s does not make sense\n",argv[ii+1]);
        return 1;
      }
      else if (mult > 100)
      {
        fprintf(stderr,"### Packet size multiplier > 100 not supported\n");
        return 1;
      }
      ii ++;
    }
    else if (!strcmp("-if",argv[ii]))
    {
      multicast_if = argv[ii+1];
      ii++;
    }
    else if (!strcmp("-delay",argv[ii]))
    {
      delay = atoi(argv[ii+1]);
      if (delay < 0)
      {
        fprintf(stderr,"### Delay %s does not make sense\n",argv[ii+1]);
        return 1;
      }
      ii ++;
    }
    else if (!strcmp("-every",argv[ii]))
    {
      every = atoi(argv[ii+1]);
      if (every < 1)
      {
        if (every == 0)
          fprintf(stderr,"### Try -delay 0 instead of -every 0\n");
        else
          fprintf(stderr,"### Sleep every %s does not make sense\n",argv[ii+1]);
        return 1;
      }
      ii ++;
    }
    else
    {
      fprintf(stderr,"### Unexpected argument %s\n",argv[ii]);
      return 1;
    }
    ii++;
  }

  socket = connect_udp_socket(hostname,port,multicast_if);
  if (socket < 0) return 1;

  data_len = mult*TS_PACKET_SIZE;
  printf("Transmitting with packet size %d (%ld*%d)\n",data_len,mult,TS_PACKET_SIZE);
  printf("Delaying %lu microseconds between packets\n",delay);
  memset(data,0xFF,data_len);
  gettimeofday(&then, NULL);
  for (packet_number = 0; ; packet_number ++)
  {
    data[0] =  packet_number        % 0xFF;
    data[1] = (packet_number >>  8) % 0xFF;
    data[2] = (packet_number >> 16) % 0xFF;
    data[3] = (packet_number >> 24) % 0xFF;
    write_socket_data(socket,data,data_len,packet_number);
    if (delay > 0)
    {
      static int sleep_count = 1;
      sleep_count ++;
      if (sleep_count > every)
      {
        usleep(delay);
        sleep_count = 1;
      }
    }
#define REPORT_EVERY        10000
#define US_PER_SECOND       1000000
#define FLOAT_US_PER_SECOND 1000000.0
    if (packet_number > 0 && packet_number % REPORT_EVERY == 0)
    {
      unsigned long elapsed;
      gettimeofday(&now, NULL);
      elapsed = (now.tv_sec - then.tv_sec) * US_PER_SECOND +
        (now.tv_usec - then.tv_usec);
      printf("%d packets transmitted in %.2f seconds",
             REPORT_EVERY,elapsed/FLOAT_US_PER_SECOND);
      printf(" (i.e. %.2f kilobytes/second, %.2f megabits/second)\n",
             (mult*TS_PACKET_SIZE*REPORT_EVERY/1024) / (elapsed/FLOAT_US_PER_SECOND),
             (mult*TS_PACKET_SIZE*REPORT_EVERY*8/(1024*1024)) / (elapsed/FLOAT_US_PER_SECOND));
      then = now;
    }
  }

  close(socket);
  return 0;
}
