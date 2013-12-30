# StreamQ-Proxy

## Warnings

1. As of December 2013, this is an experimental project that does not implement yet
the [requirements](SRD.md) below, but only provides a minimal feasability test, which
demonstrates the feasability of CURVE proxying (cf § *State* below).

2. For conveniance, I have copied a few include files from libzmq. The rational is to be able to work with this test as a standalone project, or integrated in libzmq/tests. These files are: include/zmq.h, include/zmq_utils.h, src/platform.hpp, and tests/testutil.hpp

## Welcome

StreamQ-Proxy aims at providing a proxy capable of proxying complex
0MQ security mechanisms. Today, if you want to setup a server secured
with the CURVE mechanism as a broker and workers, all security
handchecks and message en/decoding have to be performed inside the 
broker, then the messages are dispatched to the workers in plain text.
We want the capability to delegate these towards the workers.

The use of ZMQ_STREAM socket type is the only identified solution
to achieve this without modifying the ZMTP protocol that sustends libzmq.

So the architecture we want to implement is as follows:

```
                 ___________________server__________________
                 ____________proxy___________
Client  ------- frontend       /      backend ------- Worker
DEALER          ZMQ_STREAM         ZMQ_STREAM         DEALER
CURVE                                                 CURVE
```

## Requirements

Follow the link [here](SRD.md)

## State

This is a starter project with a very minimal test program with only one client and
one worker. So, I don't have to manage any pairing. It is implemented for CURVE, 
but any other mechanism shall be able to be used.

I have sticked to libzmq test_stream.cpp and zmq_proxy_steerable. The idea here 
is the proxy pools only workers at the beginning. When a worker connects, we pool 
also the client and the backend stays on standby until the client is identified. Then 
the proxy forwards all the messages from one to the other. There is a little
state machine for the frontend and for the backend in the proxy.

This first feasability program works.

TODO:

1. Test multi-part messages.

2. Test asynchronicity (send several messages before waiting for responses).

3. Extend to many clients & workers.

## Building and installation

We have a simple bash builder for our first test program test_curve_proxying.

```
git clone https://github.com/lalebarde/streamq-proxy
cd streamq-proxy
./build-test_curve_proxying
```
Run with:
```
tests/test_curve_proxying
```

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
