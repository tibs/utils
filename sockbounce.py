#! /usr/bin/env python
"""sockbounce.py -- a simple client to reflect packets from a socket
"""

import sys
import socket

class DoneException(Exception):
    pass

def get_packet(sock):
    """Read a packet from the socket.
    """
    data = sock.recv(1024)
    if len(data) == 0:
        sys.stdout.write("0")
        raise DoneException
    return data

def send_packet(sock,data,f=None):
    """Send a packet down a socket.
    """
    data_left = len(data)
    data_sent = 0
    while data_sent < data_left:
        data_sent += sock.send(data[data_sent:])
    if f:
        f.write(data)

def read_next_packet(sock,f=None):
    """Read and reflect the next packet from the socket.
    """
    try:
        data = get_packet(sock)
    except DoneException:
        send_packet(sock,"")
        sock.close()
        sys.stdout.write("\nEOF\n");
        raise DoneException
    send_packet(sock,data)
    sys.stdout.write(".")
    sys.stdout.flush()

def main():
    total_packets = 0
    sock = socket.socket(socket.AF_INET,socket.SOCK_STREAM)
    print "Waiting on port 8888"
    sock.bind(("localhost",8888))
    sock.listen(1)
    conn, addr = sock.accept()
    print 'Connected by', addr
    #print "Writing to file temp.ts"
    #stream = file("temp.ts","wb")
    stream = None
    try:
        while 1:
            read_next_packet(conn,stream)
            total_packets += 1
    except DoneException:
        #stream.close()
        pass
    sys.stdout.write("\n")
    sys.stdout.write("Total packets: %d\n"%total_packets)
    sock.close()



if __name__ == "__main__":
#    try:
        main()
#    except KeyboardInterrupt:
#        print
