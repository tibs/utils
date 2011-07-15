/*
 * Author: Tony J Ibbs
 * Date:   2005-04-05
 *
 * A simple tool to act as a recipient from udp2tcp.
 */

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>

int main(int argc, char **argv)
{
#define TS_PACKET_SIZE 188

  char  *hostname;
  char  *colon;
  int    port;
  int    sock;
  struct hostent    *hp;
  struct sockaddr_in addr;
  unsigned char data[TS_PACKET_SIZE];
  unsigned long total_bytes = 0;
  unsigned long past_delay = 0;

  if (argc != 2)
  {
    fprintf(stderr, "Usage: %s <ipaddr>[:<port>]\n\n"
            "<port> defaults to 88\n", argv[0]);
    return 1;
  }

  hostname = argv[1];

  if ((colon = strchr(hostname, ':')))
  {
    *colon = '\0';
    port = atoi(colon + 1);
  }
  else
    port = 88;

  hp = gethostbyname(hostname);
  if (!hp)
  {
    perror(hostname);
    fprintf(stderr, "Invalid host address");
    return 1;
  }
  memcpy(&addr.sin_addr, hp->h_addr, hp->h_length);
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);

  if ((sock = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP)) == -1)
  {
    perror("socket");
    fprintf(stderr, "Can't create socket");
    return 1;
  }

  if (connect(sock, (struct sockaddr*)&addr, sizeof(struct sockaddr_in)) == -1)
  {
    fprintf(stderr, "Connect failed");
    return 1;
  }

  for (;;)
  {
    long    delay_wanted;
    ssize_t len = recv(sock, data, TS_PACKET_SIZE, MSG_WAITALL);
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
    if (len != TS_PACKET_SIZE)
      printf("!!! Packet size %d, not %d\n",len,TS_PACKET_SIZE);
#define PACKETID
#ifdef PACKETID
    {
      unsigned int this_packet_number = 0;
      this_packet_number = data[3];
      this_packet_number = (this_packet_number << 8) | data[2];
      this_packet_number = (this_packet_number << 8) | data[1];
      this_packet_number = (this_packet_number << 8) | data[0];
      if (this_packet_number == 0xFFFFFFFF)
      {
        printf(".");
        fflush(stdout);
      }
      else
      {
        printf("\n%08u",this_packet_number);
      }
    }
#endif
    total_bytes += len;
    delay_wanted = (total_bytes - 2000000) * 4 - past_delay;
    if (delay_wanted > 20000)
    {
      printf(".");fflush(stdout);
      usleep(delay_wanted);
      past_delay += delay_wanted;
    }
  }

  printf("\n");
  return 0;
}

