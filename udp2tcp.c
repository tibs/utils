/*
 * Author: Tony J Ibbs
 * Date:   2005-04-05
 *
 * A simple tool to read from a UDP socket, and listen for a request for TCP output,
 * upon receiving which it will redirect packets from the UDP socket to TCP.
 */

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>  // Posix standard primitive system data types
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <unistd.h>      // open, close

// C99 also defines equivalent types in <stdint.h>, but the unsigned types
// are spelt uint8_t, etc., instead of u_int8_t. Given the need to support
// older compilers, go with the Posix standard.
typedef u_int8_t  byte;
typedef u_int16_t u_int16;
typedef u_int32_t u_int32;
typedef u_int64_t u_int64;
typedef int16_t  int16;
typedef int32_t  int32;
typedef int64_t  int64;

#define SOCKET int
#define TS_PACKET_SIZE 188

/*
 * Write data out to a socket
 *
 * - `output` is a socket for our output
 * - `data` is the data to write out
 * - `data_len` is how much of it there is
 *
 * Returns 0 if all went well, 1 if something went wrong.
 */
static int write_socket_data(SOCKET output,
                             byte   data[],
                             int    data_len)
{
  ssize_t  written = 0;
  ssize_t  left    = data_len;
  int     start   = 0;

  errno = 0;
  while (left > 0)
  {
    written = send(output,&(data[start]),left,0);
    if (written == -1)
    {
      if (errno == ENOBUFS)
      {
        fprintf(stderr,"!!! Warning: 'no buffer space available' writing out"
                " packet data - retrying\n");
        errno = 0;
      }
      else
      {
        fprintf(stderr,"### Error writing: %s\n",strerror(errno));
        return 1;
      }
    }
    left -= written;
    start += written;
  }
  return 0;
}

static SOCKET udp_listen_socket(char *hostname, int port)
{
  struct hostent *hp;
  SOCKET sock;
  const int one = 1;
  int multicast;
  struct sockaddr_in ipaddr;

  printf("Making UDP connection to %s on port %d",hostname,port);
  
  hp = gethostbyname(hostname);
  if (!hp)
  {
    perror(hostname);
    printf("\n");
    fprintf(stderr, "Invalid host address");
    return -1;
  }
  memcpy(&ipaddr.sin_addr, hp->h_addr, hp->h_length);
  ipaddr.sin_family = AF_INET;
  ipaddr.sin_port = htons(port);

  if ((sock = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP)) == -1)
  {
    printf("\n");
    perror("socket");
    fprintf(stderr, "Can't create socket");
    return -1;
  }

  // Is this a multicast address?
  multicast =  IN_CLASSD(ntohl(ipaddr.sin_addr.s_addr));

  if (multicast)
  {
    printf(" (multicast)\n");
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
    printf(" (unicast)\n");
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

static int run_server(char  *udp_host,
                      int    udp_port,
                      int    listen_port,
                      int    mult)
{
  int    err;
  SOCKET server_socket;
  SOCKET client_socket;
  SOCKET udp_socket;
  struct sockaddr_in ipaddr;
  byte  *data;
  int    packet_size = mult * TS_PACKET_SIZE;

  data = malloc(sizeof(byte) * packet_size);
  if (data == NULL) 
  {
    fprintf(stderr,"### Cannot allocate data buffer of size %d*%d=%d\n",
            mult,TS_PACKET_SIZE,packet_size);
    return 1;
  }

  // Create a socket.
  server_socket = socket(AF_INET, SOCK_STREAM, 0);
  if (server_socket == -1)
  {
    fprintf(stderr,"### Unable to create socket: %s\n",strerror(errno));
    free(data);
    return 1;
  }

  // Bind it to port `listen_port` on this machine
  memset(&ipaddr,0,sizeof(ipaddr));
#if !defined(__linux__)
  // On BSD, the length is defined in the datastructure
  ipaddr.sin_len = sizeof(struct sockaddr_in);
#endif
  ipaddr.sin_family = AF_INET;
  ipaddr.sin_port = htons(listen_port);
  ipaddr.sin_addr.s_addr = INADDR_ANY;  // any interface

  err = bind(server_socket,(struct sockaddr*)&ipaddr,sizeof(ipaddr));
  if (err == -1)
  {
    fprintf(stderr,"### Unable to bind to port %d: %s\n",
            listen_port,strerror(errno));
    free(data);
    return 1;
  }

  for (;;)
  {
    printf("Listening for a connection on port %d\n",listen_port);

    // Listen for someone to connect to it
    err = listen(server_socket,1);
    if (err == -1)
    {
      fprintf(stderr,"### Error listening for client: %s\n",strerror(errno));
      free(data);
      return 1;
    }

    // Accept the connection
    client_socket = accept(server_socket,NULL,NULL);
    if (client_socket == -1)
    {
      fprintf(stderr,"### Error accepting connection: %s\n",strerror(errno));
      free(data);
      return 1;
    }

    // And connect to the UDP as well
    udp_socket = udp_listen_socket(udp_host,udp_port);
    if (udp_socket < 0)
    {
      fprintf(stderr,"### Unable to connect to UDP host %s, port %d\n",
              udp_host,udp_port);
      return 1;
    }

    printf("Copying packets...\n");

    for (;;)
    {
      int    ii;
      size_t len = recv(udp_socket, data, mult*TS_PACKET_SIZE, MSG_WAITALL);
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
        printf("!!! Packet of size %d, not %d\n",len,packet_size);

#ifdef PACKETNUMS
      // This code is useful if we are receiving data from udpserve,
      // which puts a packet number in the first four bytes of each
      // <mult>*188 byte packet.
      {
        unsigned int this_packet_number;
        this_packet_number = data[3];
        this_packet_number = (this_packet_number << 8) | data[2];
        this_packet_number = (this_packet_number << 8) | data[1];
        this_packet_number = (this_packet_number << 8) | data[0];
        printf("%08u\n",this_packet_number);
      }
#endif
      for (ii = 0; ii < mult; ii++)
      {
        err = write_socket_data(client_socket,&data[ii*TS_PACKET_SIZE],TS_PACKET_SIZE);
        if (err) break;
      }
      if (err) break;
    }
    close(udp_socket);
    close(client_socket);
  }

  free(data);
  return 0;
}

int main(int argc, char **argv)
{
  char  *udp_host = NULL;
  char  *colon;
  long   udp_port = 88;
  long   listen_port;
  int    mult = 7;

  if (argc != 3)
  {
    fprintf(stderr,"Usage: udp2tcp <from>[:<port>] <listen-port> [<mult>]\n"
            "Reads packets over UDP from the host with IP <from>, default port 88.\n"
            "Listens on TCP port <listen-port> for a connection, and on receiving one\n"
            "streams UDP packets over TCP.\n"
            "If <mult> is given, it is the size of the packets in multiples of 188\n"
            "(i.e., TS packets are assumed). <mult> defaults to 7.\n"
           );
    return 1;
  }

  udp_host = argv[1];
  if ((colon = strchr(udp_host, ':')))
  {
    *colon = '\0';
    udp_port = atoi(colon + 1);
  }

  if (argc > 3)
    mult = atoi(argv[4]);
  if (mult <= 0)
  {
    fprintf(stderr,"Packet size multiplier %s does not make sense\n",argv[4]);
    return 1;
  }

  listen_port = atoi(argv[2]);
  if (listen_port <= 0)
  {
    fprintf(stderr,"Port %s does not make sense\n",argv[2]);
    return 1;
  }

  printf("UDP from %s:%ld, listening for a TCP connection on port %ld\n"
         "Packet size = %d (%d * 188)\n",udp_host,udp_port,listen_port,
         TS_PACKET_SIZE,mult);

  if (run_server(udp_host,udp_port,listen_port,mult) < 0)
    return 1;
  else
    return 0;
}
