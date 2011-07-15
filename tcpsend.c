/*
 * A utility to send a file over TCP, and optionally receieve data back
 * into another file.
 * Author: Tony J. Ibbs <tibs@tonyibbs.co.uk>
 * Date: 2005/07/11
 */

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <sys/wait.h>
#include <netinet/in.h>
#include <netdb.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>

#define TRUE    1
#define FALSE   0
#define BUFFER_SIZE 1500

#define DEBUG_PUTC(x)       {if (dotty){putc(x,stdout);fflush(stdout);}}

typedef unsigned char byte;

static void print_usage(void)
{
  printf("Usage:\n"
         "\n"
         "    tcpsend [<switches>] <host>[:<port>] <file>\n"
         "\n"
         "where:\n"
         "\n"
         "  <host>          is the IP address of the host to send data to.\n"
         "  <host>:<port>   is the same but specifies a port to use (the\n"
         "                  default is port 88).\n"
         "  <file>          is the name of the file to send.\n"
         "\n"
         "and <switches> are:\n"
         "\n"
         "  -loop           loop repeating the file.\n"
         "  -retry          keep trying if connection refused.\n"
         "  -receive <file> read data back over TCP/IP into the named file.\n"
         "  -rx <file>      the same.\n"
         "  -dots           output indicators of packet transfer\n"
         "\n"
         "  -hang           hang (stop sending) after some small number of packets\n"
         "                  - this is intended for use in testing the recipient\n"
         "                  process\n"
         "\n"
         "Note that <switches> may actually occur at any position on the\n"
         "command line. For instance:\n"
         "\n"
         "          tcpsend 10.10.1.98:8888 data.es -rx result.es\n"
         );
}

int main(int argc, char **argv)
{
  int    had_hostname = FALSE;
  int    had_filename = FALSE;
  char  *hostname;
  char  *colon;
  char  *filename = NULL;
  char  *receive_filename = NULL;
  unsigned short portno = 88;
  int    dotty = FALSE;

  int    retry_mode = FALSE;
  int    loop_mode = FALSE;

  int    force_hang = FALSE;

  int done;
  struct sockaddr_in addr = {0};
  int conn;	// Socket
  int fd = -1;	// File descriptor
  int rxfd = -1;
  int pid;

  unsigned int i;
  int result = 0;
  struct hostent *hp;

  conn = -1;

  if (argc < 2)
  {
    print_usage();
    return 1;
  }

  for (; argc > 1; --argc, ++argv)
  {
    if (!strcmp(argv[1], "-loop"))
    {
      loop_mode = TRUE;
    }
    else if (!strcmp(argv[1], "-retry"))
    {
      retry_mode = TRUE;
    }
    else if (!strcmp(argv[1], "-dots"))
    {
      dotty = TRUE;
    }
    else if (!strcmp(argv[1], "-hang"))
    {
      force_hang = TRUE;
    }
    else if (!strcmp(argv[1],"-receive") || !strcmp(argv[1],"-rx"))
    {
      if (argc < 2)
      {
        printf("%s needs a file name\n",argv[1]);
      }
      receive_filename = argv[2];
      argv++;
      argc--;
    }
    else if (!had_hostname)
    {
      hostname = argv[1];
      had_hostname = TRUE;

      if ((colon = strchr(hostname, ':')))
      {
        char *cp;
        unsigned long num;
        *colon = '\0';
        num = strtoul(colon+1, &cp, 0);
        if (num <= 0 || num >= 65536 || cp == colon+1)
        {
          printf("Bad port number '%s'\n",colon);
          print_usage();
          return 1;
        }
        portno = (unsigned short)num;
      }
      else
        portno = 88;

      hp = gethostbyname(hostname);
    }
    else if (!had_filename)
    {
      filename = argv[1];
      had_filename = TRUE;
      if ((fd = open(filename, O_RDONLY)) < 0)
      {
        printf("Unable to open '%s': %s\n",argv[1],strerror(errno));
        return 1;
      }
      addr.sin_port = htons(portno);
      addr.sin_addr.s_addr = htonl(0x0a0502d1);
      memcpy(&addr.sin_addr.s_addr, hp->h_addr, hp->h_length);
      addr.sin_family = hp->h_addrtype;
    }
    else
    {
      printf("Unexpected command line option '%s'\n",argv[1]);
      print_usage();
      return 1;
    }
  }

  if (!had_filename)
  {
    printf("No files to send\n");
    print_usage();
    return 1;
  }
  if (!had_hostname)
  {
    printf("No IP address to send to\n");
    print_usage();
    return 1;
  }
  if (receive_filename)
  {
    if ((rxfd = creat(receive_filename, 00777)) < 0)
    {
      printf("Unable to open '%s': %s\n",receive_filename,strerror(errno));
      return 1;
    }
  }

  if ((conn = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0)
  {
    perror("Error creating socket");
    result = 1;
    goto giveup;
  }
  printf("Connecting to %s on port %d\n",hostname,ntohs(addr.sin_port));

  for (;;)
  {
    if (connect(conn, (struct sockaddr *)&addr, sizeof(struct sockaddr_in)) < 0)
    {
      unsigned long temp;
      if ((errno == ECONNREFUSED) && retry_mode)
      {
        printf(".");
        sleep(1);
        continue;
      }
      temp = ntohl(addr.sin_addr.s_addr);
      fprintf(stderr, "Error connecting to %d.%d.%d.%d port %d: %s\n",
              (temp >> 24) & 0xFF, (temp >> 16) & 0xFF, (temp >>  8) & 0xFF,
              temp & 0xFF, ntohs(addr.sin_port), strerror(errno));
      result = 2;
      goto giveup;
    }
    break;
  }

  printf("Starting send...\n");

  for (;;)
  {
    fd_set read_fds, write_fds;
    int    result;
    int    num_to_check = (fd>rxfd?fd:rxfd)+1;

    FD_ZERO(&read_fds);
    FD_ZERO(&write_fds);
    if (filename)
      FD_SET(conn,&write_fds);
    if (receive_filename)
      FD_SET(conn,&read_fds);

    result = select(conn+1,&read_fds,&write_fds,NULL,NULL);
    if (result == -1)
    {
      printf("Error in select: %s\n",strerror(errno));
      return 1;
    }
    else if (result == 0) // Hmm, odd
      continue;           // what else should we do?

    // Read from file, write to socket
    if (FD_ISSET(conn,&write_fds))
    {
      int     result;
      byte    buffer[BUFFER_SIZE];
      ssize_t length = read(fd,buffer,BUFFER_SIZE);
      if (length == 0)
      {
        DEBUG_PUTC('\n');
        printf("EOF in %s\n",filename);
        close(fd);
        filename = NULL;
        // Finished writing...
        result = shutdown(conn,SHUT_WR);
        if (result == -1)
        {
          printf("Error shutting down write on socket: %s\n",strerror(errno));
          return 1;
        }
      }
      else if (length == -1)
      {
        printf("Error reading from file: %s\n",strerror(errno));
        (void) shutdown(conn,SHUT_WR);
        return 1;
      }
      if (length > 0)
      {
        result = send(conn,buffer,length,0);
        if (result == -1)
        {
          printf("Error writing to socket: %s\n",strerror(errno));
          (void) shutdown(conn,SHUT_WR);
          return 1;
        }
        DEBUG_PUTC('w');
      }

      if (force_hang)
      {
        printf("Forcing hang - not writing to socket any more\n");
        close(fd);
        filename = NULL;
        // But don't shutdown the connection...
      }
    }

    // Read from socket, write to file
    if (receive_filename && FD_ISSET(conn,&read_fds))
    {
      byte    buffer[BUFFER_SIZE];
      ssize_t count;
      ssize_t length = recv(conn,buffer,BUFFER_SIZE,0);
      if (length == 0)
      {
        DEBUG_PUTC('\n');
        printf("EOF from socket\n");
        close(rxfd);
        receive_filename = NULL;
      }
      else if (length == -1)
      {
        printf("Error reading from socket: %s\n",strerror(errno));
        (void) shutdown(conn,SHUT_RD);
        return 1;
      }
      else
        DEBUG_PUTC('r');
      if (length > 0)
      {
        count = write(rxfd,buffer,length);
        if (count == -1)
        {
          printf("Error writing to %s: %s\n",receive_filename,strerror(errno));
          (void) shutdown(conn,SHUT_RD);
          return 1;
        }
        else if (count != length)
        {
          printf("Error writing to %s: wrote %d bytes instead of %d\n",
                 receive_filename,count,length);
          (void) shutdown(conn,SHUT_RD);
          return 1;
        }
      }
    }
    // Stop when we've got nothing to send, and nothing more to receive
    if (filename == NULL && receive_filename == NULL)
      break;
  }
  DEBUG_PUTC('\n');
  printf("Finished\n");

giveup:
  if (conn >= 0 && close(conn) < 0)
    perror("Error closing conn socket");

  return result;
}
