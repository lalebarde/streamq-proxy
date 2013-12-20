# StreamQ-Proxy

## Warning

As of December 2013, this is an experimental project that does not implement 
the requirements below, but only provides a minimal feasability test, which
still does not work, for discussions, advices, contributions (cf State below).

## Welcome

StreamQ-Proxy aim is to provide a proxy capable of proxying complex
0MQ security mechanisms. Today, if you want to setup a server secured
with the CURVE mechanism as a broker and workers, all security
handchecks and message en/decoding have to be performed inside the 
broker, then the messages are dispatched to the workers in plain text.
We want the capability to delegate these towards the workers.

The use of ZMQ_STREAM socket type is the only identified solution
to achieve this without modifying the ZMTP protocol that sustends libzmq.

So the architecture we want to implement is as follows:

```
                  ___________proxy__________
Client  ------- frontend       /      backend ------- Worker
DEALER          ZMQ_STREAM         ZMQ_STREAM        DEALER
CURVE                                                CURVE
```

## Requirements

The proxy SHALL be transparent to the clients and the workers, as if
they were connected directly one-to-one.

The proxy SHALL have a ZMQ_STREAM socket called frontend to
interface with the clients, and another one called backend for the workers.

The proxy SHALL keep a list of clients and workers as they connect
and store their identity.

The proxy SHALL pool the frontend when at least one worker is available.

When a worker connects, its messages cannot be forwarded until there is
a client. So its identity along with its ZMTP signature SHALL be stored.

When a client connects, a persistent pairing is performed between its
identity and the identity of a worker. Persistent means that the same client
SHALL communicate always with the same worker all the time it is connected.

Pairing, thought persistent, SHALL be performed in a load balancing pattern.
A client will be assigned to an available worker or the less loaded one. It SHALL
be possible to assign the same client to the same worker for all connexions, with
a list of fallbacks.

After the pairing is performed, the worker identity and signature previously
stored SHALL be forwarded to its assigned client and then all messages are
forwarded in both ways.

Message forwarding consists in receiving a multipart message from a peer 
that starts with its identity, withdraw the identity of the destiny in the pairing
table, resend the message with first the identity of the destiny, and then the rest
of the message, except the identity of the origin.

Message forwarding either receives from the frontend and resend to the backend,
or receives from the backend and resend to the frontend.

The pairing table SHALL perform a pair identity access in o(1).

The proxy MAY manage IDENTITY optionaly set on the client or worker socket. It SHALL
not manage it by decoding the ZMTP metadata, but through the control socket.

Any mechanism shall be able to be used, not only CURVE.

Clients and worker disconnexions SHALL be managed*. When one peer is
disconnected, the pairing table SHALL be updated.

TODO: precise how disconnexions should be managed. Probably throught the control
socket when possible. Strategies shall be discussed when disconnexion is accidental.
Some possibilities are 1) Heartbeat via the control socket between each peer and the proxy,
 2) mandatory heartbeat between the client and the worker - then the proxy pairs would have
a TTL, 3) Simple proxy pair TTL. Possibly we could authorize a choice of strategies.

## State

This is a starter project with a very minimal test program with only one client and
one worker. So, I don't have to manage any pairing. It is implemented for CURVE, 
but any other mechanism shall be able to be used.

I have sticked to libzmq test_stream.cpp and zmq_proxy_steerable. The idea here 
is the proxy pools only workers at the beginning. When a worker connects, we pool 
also the client and the backend stays on standby until the client is identified. Then 
the proxy forwards all the messages from one to the other. There is a little
state machine for the frontend and for the backend in the proxy.

It starts well, both client and worker are identified in the proxy, then
both backend and frontend forward the identity and signature of one peer to
the other.

The problem is that just after the frontend has sent them, it receive back
from the client its identity (as expected), and a 1 byte message (content =
3) instead of the greeting.

TODO: makes this first feasability program work. Advices and contributions are welcome.

## Building and installation

TODO: See the INSTALL file included with the distribution.

## Resources

**Concerning 0MQ:**

Web site:http://www.zeromq.org/

Git repository: http://github.com/zeromq/libzmq

Development mailing list: zeromq-dev@lists.zeromq.org

Announcements mailing list: zeromq-announce@lists.zeromq.org

**Concerning StreamQ-Proxy:**

Git repository: http://github.com/lalebarde/streamq-proxy

## Copying

Free use of this software is granted under the terms of the GNU Lesser General
Public License (LGPL). For details see the files `COPYING` and `COPYING.LESSER`
included with the StreamQ-Proxy distribution.
