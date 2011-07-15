Simple UDP and TCP utilities
============================
These are a set of simple UDP and TCP utilities that have been developed
on an ad-hoc basis. They are Linux specific (although they probably work
on BSD as well).

The source files are:

* tcprecv.c - Receives data from udp2tcp.

* tcpsend.c - A utility to send a file over TCP, and optionally receive
  data back into another file. This is useful in conjunction with the
  "snoop" tool that we have historically used for regression testing on some
  boards. An empty command line will give help.

* udp2tcp.c - Reads from a UDP socket, listening for a request for TCP output,
  and then redirects packets from the UDP socket to UDP.

* udpserve.c - A simple UDP server, sending packets that contain an ascending
  packet number so that the client can tell if packets are being dropped.

* udptest.c - Reads data over UDP, assumed to be from udpserve, and checks for
  dropped packets.

* sockbounce.py - An embarassingly unsophisticated script to reflect packets.
  Normally hacked to some particular purpose before actually being used.

