/*
    Copyright (c) 2007-2013 Contributors as noted in the AUTHORS file

    This file is part of 0MQ.

    0MQ is free software; you can redistribute it and/or modify it under
    the terms of the GNU Lesser General Public License as published by
    the Free Software Foundation; either version 3 of the License, or
    (at your option) any later version.

    0MQ is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU Lesser General Public License for more details.

    You should have received a copy of the GNU Lesser General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "testutil.hpp"
#include "../include/zmq_utils.h"
#ifdef HAVE_LIBSODIUM
#include <sodium.h>
#endif

#define CONTENT_SIZE 13
#define CONTENT_SIZE_MAX 512
#define REGISTER_SIZE 9
#define REGISTER_MSG "REGISTER"
#define ID_SIZE 10
#define ID_SIZE_MAX 32
#define QT_WORKERS    1 // WARNING: this code has been simplified and can accept only one worker
#define QT_CLIENTS    1 // WARNING: this code has been simplified and can accept only one client
#define QT_REQUESTS 100 // 100
#define is_verbose 1
#define BACKEND 0
#define CONTROL 1
#define FRONTEND 2
#define KEY_SIZE_0 41
#define KEY_SIZE 40

//  ZMTP protocol null_greeting structure
typedef unsigned char byte;
typedef struct {
    byte signature [10];    //  0xFF 8*0x00 0x7F
    byte version [2];       //  0x03 0x00 for ZMTP/3.0
    byte mechanism [20];    //  "NULL"
    byte as_server;
    byte filler [31];
} zmtp_greeting_t;

static char client_pub[KEY_SIZE_0], client_sec[KEY_SIZE_0],worker_pub[KEY_SIZE_0], worker_sec[KEY_SIZE_0];

static     zmq_msg_t client_identity; static size_t client_id_size = 0;
static     zmq_msg_t worker_identity; static size_t worker_id_size = 0;

static void
client_task (void *ctx)
{
    // Client socket
    void *client = zmq_socket (ctx, ZMQ_DEALER);
    assert (client);
//    int rc = zmq_setsockopt (client, ZMQ_IDENTITY, "client___", ID_SIZE); // includes '\0' as an helper for printf
//    assert (rc == 0);
    int rc = zmq_setsockopt (client, ZMQ_CURVE_SERVERKEY, worker_pub, KEY_SIZE);
    assert (rc == 0);
    rc = zmq_setsockopt (client, ZMQ_CURVE_PUBLICKEY, client_pub, KEY_SIZE);
    assert (rc == 0);
    rc = zmq_setsockopt (client, ZMQ_CURVE_SECRETKEY, client_sec, KEY_SIZE);
    assert (rc == 0);
    rc = zmq_connect (client, "tcp://127.0.0.1:9999");
    assert (rc == 0);
    if (is_verbose) printf("Create client\n");

    // Control socket - receives terminate command from main over inproc
    void *control = zmq_socket (ctx, ZMQ_SUB);
    assert (control);
    rc = zmq_setsockopt (control, ZMQ_SUBSCRIBE, "", 0);
    assert (rc == 0);
    rc = zmq_connect (control, "inproc://control");
    assert (rc == 0);

    char content [CONTENT_SIZE_MAX];
    content [CONTENT_SIZE_MAX - 1] = '\0';
    zmq_pollitem_t items [] = { { client, 0, ZMQ_POLLIN, 0 }, { control, 0, ZMQ_POLLIN, 0 } };
    int request_nbr = 0;
    bool run = true;
    int qtMsgPerRound = 1;
    while (run) {
        // send
        for (int i = 0; i < qtMsgPerRound; i++) {
            sprintf(content, "request #%03d", ++request_nbr); // CONTENT_SIZE
            rc = zmq_send (client, content, CONTENT_SIZE, 0);
            assert (rc == CONTENT_SIZE);
        }

        // receive
        int qt_received = 0;
        while (qt_received < qtMsgPerRound) { // all what has been sent shall be received back
            zmq_poll (items, 2, 10); // could put 0, but waiting a little (10 ms) saves cpu time
            if (items [0].revents & ZMQ_POLLIN) {
                int rcvmore;
                size_t sz = sizeof (rcvmore);
                rc = zmq_recv (client, content, CONTENT_SIZE_MAX, 0);
                assert (rc == CONTENT_SIZE);
                if (is_verbose) printf("client receive content = %s\n", content);
                //  Check that message is still the same
                assert (memcmp (content, "request #", 9) == 0);
                rc = zmq_getsockopt (client, ZMQ_RCVMORE, &rcvmore, &sz);
                assert (rc == 0);
                assert (!rcvmore);
                qt_received++;
            }
            if (items [1].revents & ZMQ_POLLIN) {
                rc = zmq_recv (control, content, CONTENT_SIZE_MAX, 0);
                if (is_verbose) printf("client receive command = %s\n", content);
                if (memcmp (content, "TERMINATE", 10) == 0) {
                    run = false;
                    break;
                }
            }
        }
        if (request_nbr >= QT_REQUESTS)
            run = false;
    }

    rc = zmq_close (client);
    assert (rc == 0);
    rc = zmq_close (control);
    assert (rc == 0);
    if (is_verbose) printf("Destroy client\n");
}

static void server_worker (void *ctx);

void
server_proxy (void *ctx)
{
    // Frontend socket talks to clients over TCP
    void *frontend = zmq_socket (ctx, ZMQ_STREAM);
    assert (frontend);
    int rc = zmq_bind (frontend, "tcp://127.0.0.1:9999");
    assert (rc == 0);

    // Backend socket talks to workers over inproc
    void *backend = zmq_socket (ctx, ZMQ_STREAM);
    assert (backend);
    rc = zmq_bind (backend, "tcp://127.0.0.1:9998");
    assert (rc == 0);

    // Control socket receives terminate command from main over inproc
    void *control = zmq_socket (ctx, ZMQ_SUB);
    assert (control);
    rc = zmq_setsockopt (control, ZMQ_SUBSCRIBE, "", 0);
    assert (rc == 0);
    rc = zmq_connect (control, "inproc://control");
    assert (rc == 0);

    // Launch pool of worker threads, precise number is not critical
    int thread_nbr;
    void* threads [QT_WORKERS];
    for (thread_nbr = 0; thread_nbr < QT_WORKERS; thread_nbr++) {
        threads[thread_nbr] = zmq_threadstart (&server_worker, ctx);
    }

    // variables
    char content [CONTENT_SIZE_MAX]; //    bigger than what we need to check we receive the expected size
    int size;

    int more;
    size_t moresz = sizeof more;
    zmq_pollitem_t items [] = {
        { backend, 0, ZMQ_POLLIN, 0 }, // BACKEND = 0
        { control, 0, ZMQ_POLLIN, 0 }, // CONTROL = 1
        { frontend, 0, ZMQ_POLLIN, 0 } // FRONTEND = 2
    };
    int qt_poll_items = 2;
    enum {suspend, resume, terminate} control_state = resume;
    enum {waiting_worker, waiting_client, curve_handcheck, curve_ready} backend_state = waiting_worker;
    enum {no_client, client_is_here} frontend_state = no_client;
    int qtWorkers = 0;
    rc = zmq_msg_init (&client_identity);
    assert (rc == 0);
    rc = zmq_msg_init (&worker_identity);
    assert (rc == 0);

    while (control_state != terminate) {
        //  Wait while there are either requests or replies to process.
        qt_poll_items = (qtWorkers > 0 ? 3 : 2); // if no worker is available, don't pool the clients
        rc = zmq_poll (&items [0], qt_poll_items, -1);
        if (rc < 0)
            break;

        //  Process a control command if any
        if (control && items [CONTROL].revents & ZMQ_POLLIN) {
            size = zmq_recv (control, content, CONTENT_SIZE_MAX, 0);
            if (size < 0)
                break;

            moresz = sizeof more;
            rc = zmq_getsockopt (control, ZMQ_RCVMORE, &more, &moresz);
            if (rc < 0 || more)
                break;

            // process control command
            content[size] = '\0';
            if (size == 8 && !memcmp(content, "SUSPEND", 8))
                control_state = suspend;
            else if (size == 7 && !memcmp(content, "RESUME", 7))
                control_state = resume;
            else if (size == 10 && !memcmp(content, "TERMINATE", 10))
                control_state = terminate;
            else
                fprintf(stderr, "Warning : \"%s\" bad command received by proxy\n", content); // prefered compared to "return -1"
        }
        //  Process a request
        if (control_state == resume && items [FRONTEND].revents & ZMQ_POLLIN) {
            bool hasToSendIdentityToWorker = true;
            frontend_state = client_is_here;

            //  First frame is identity
            zmq_msg_t identity;
            rc = zmq_msg_init (&identity);
            assert (rc == 0);
            rc = zmq_msg_recv (&identity, frontend, 0);
            assert (rc > 0);
            assert (zmq_msg_more (&identity)); // we expect at least one message after the identifier
            if (!client_id_size) { // first time store the identity
                client_id_size = zmq_msg_size (&identity);
                rc = zmq_msg_move (&client_identity, &identity);
                assert (rc == 0);
            }
            else { // other times, check it is the same
                assert (client_id_size == zmq_msg_size (&identity));
                assert (!memcmp(zmq_msg_data (&client_identity), zmq_msg_data (&identity), client_id_size));
            }
            rc = zmq_msg_close (&identity);
            assert (rc == 0);

            while (true) {
                // receive content
                size = zmq_recv (frontend, content, CONTENT_SIZE_MAX, 0);
                if (size < 0)
                    break;

                // is there more message ?
                rc = zmq_getsockopt (frontend, ZMQ_RCVMORE, &more, &moresz);
                if (rc < 0)
                    break;

                // send (request) to worker
                if (hasToSendIdentityToWorker) {
                    rc = zmq_msg_init (&identity);
                    assert (rc == 0);
                    rc = zmq_msg_copy (&identity, &worker_identity);
                    assert (rc == 0);
                    rc = zmq_msg_send (&identity, backend, ZMQ_SNDMORE);
                    assert (rc == (int) worker_id_size);
                    rc = zmq_msg_close (&identity);
                    assert (rc == 0);
                    hasToSendIdentityToWorker = false;
                }
                rc = zmq_send (backend, content, size, more? ZMQ_SNDMORE: 0);
                assert (rc == size);
                if (more == 0)
                    break;
            }
        }
        //  Process a reply
        if (control_state == resume && items [BACKEND].revents & ZMQ_POLLIN) {

            zmq_msg_t identity;

            if (backend_state != waiting_client) {
                //  First frame is identity
                rc = zmq_msg_init (&identity);
                assert (rc == 0);
                rc = zmq_msg_recv (&identity, backend, 0);
                assert (rc > 0);
                assert (zmq_msg_more (&identity)); // we expect at least one message after the identifier
                if (!worker_id_size) { // first time store the identity
                    worker_id_size = zmq_msg_size (&identity);
                    rc = zmq_msg_move (&worker_identity, &identity);
                    assert (rc == 0);
                }
                else { // other times, check it is the same
                    assert (worker_id_size == zmq_msg_size (&identity));
                    assert (!memcmp(zmq_msg_data (&worker_identity), zmq_msg_data (&identity), worker_id_size));
                }
                rc = zmq_msg_close (&identity);
                assert (rc == 0);

                if (backend_state == waiting_worker) {
                    qtWorkers++; // unconditionally so that the exchange of greetings can occur: we have to ear for the client now
                    backend_state = waiting_client;
                }
            }

            if (frontend_state == client_is_here) {

                backend_state = curve_handcheck;

                // Second frame depends on the backend_state
                size = zmq_recv (backend, content, CONTENT_SIZE_MAX, 0);
                if (size < 0)
                    break;

                // more frames ?
                rc = zmq_getsockopt (backend, ZMQ_RCVMORE, &more, &moresz);
                if (rc < 0)
                    break;

                if (backend_state == curve_handcheck) {
                    zmtp_greeting_t* g = (zmtp_greeting_t*) content;
                    if (!memcmp(g->mechanism, "CURVE", 5)) { // CURVE
                        // check the end of the handcheck - this is wrong for the moment (taken from NULL), but is not a showstopper
                        if (content [0] == 0 && content [1] == 5 && !memcmp(content + 2, "READY", 5) && !more) {
                            if (is_verbose) printf("proxy: worker %s has registered\n", worker_identity._);
                            backend_state = curve_ready;
                        }
                    }
//                    else
//                        assert (false); // unexpected mechanism
                }
                if (backend_state == curve_handcheck || backend_state ==  curve_ready) {
                        // send identity and greeting to client
                        rc = zmq_msg_init (&identity);
                        assert (rc == 0);
                        rc = zmq_msg_copy (&identity, &client_identity);
                        assert (rc == 0);
                        rc = zmq_msg_send (&identity, frontend, ZMQ_SNDMORE);
                        assert (rc == (int) client_id_size);
                        rc = zmq_msg_close (&identity);
                        assert (rc == 0);
                        rc = zmq_send (frontend, content, size, 0);
                        assert (rc == size);
                        while (more) {
                            // receive content
                            size = zmq_recv (backend, content, CONTENT_SIZE_MAX, 0);
                            if (size < 0)
                                break;

                            // is there more message ?
                            rc = zmq_getsockopt (backend, ZMQ_RCVMORE, &more, &moresz);
                            if (rc < 0)
                                break;

                            // send (answer) to client
                            rc = zmq_send (frontend, content, size, more? ZMQ_SNDMORE: 0);
                            assert (rc == size);
                        }
                    }
            } // if (frontend_state != no_client)
        } // if (control_state == resume && items [BACKEND].revents & ZMQ_POLLIN)
    } // while (control_state != terminate)

    msleep(100);

    for (thread_nbr = 0; thread_nbr < QT_WORKERS; thread_nbr++)
        zmq_threadclose (threads[thread_nbr]);

    rc = zmq_msg_close (&client_identity);
    assert (rc == 0);
    rc = zmq_msg_close (&worker_identity);
    assert (rc == 0);

    rc = zmq_close (frontend);
    assert (rc == 0);
    rc = zmq_close (backend);
    assert (rc == 0);
    rc = zmq_close (control);
    assert (rc == 0);
}

static void
server_worker (void *ctx)
{

    void *worker = zmq_socket (ctx, ZMQ_DEALER);
    assert (worker);
    if (is_verbose) printf("Create worker\n");
    int as_server = 1;
    int rc = zmq_setsockopt (worker, ZMQ_CURVE_SERVER, &as_server, sizeof (int));
    assert (rc == 0);
    rc = zmq_setsockopt (worker, ZMQ_CURVE_SECRETKEY, worker_sec, KEY_SIZE);
    assert (rc == 0);
//    rc = zmq_setsockopt (worker, ZMQ_IDENTITY, "worker___", ID_SIZE); // includes '\0' as an helper for printf
//    assert (rc == 0);
    rc = zmq_connect (worker, "tcp://127.0.0.1:9998");
    assert (rc == 0);

    // Control socket receives terminate command from main over inproc
    void *control = zmq_socket (ctx, ZMQ_SUB);
    assert (control);
    rc = zmq_setsockopt (control, ZMQ_SUBSCRIBE, "", 0);
    assert (rc == 0);
    rc = zmq_connect (control, "inproc://control");
    assert (rc == 0);

    // variables
    char content [CONTENT_SIZE_MAX]; //    bigger than what we need to check that
    char identity [ID_SIZE_MAX];

    bool run = true;
    while (run) {
        rc = zmq_recv (control, content, CONTENT_SIZE_MAX, ZMQ_DONTWAIT); // usually, rc == -1 (no message)
        if (rc > 0) {
            if (is_verbose) printf("worker %s receives command = %s\n", worker_identity._, content);
            if (memcmp (content, "TERMINATE", 10) == 0)
                run = false;
        }

        int rcvmore;
        size_t sz = sizeof (rcvmore);
        int size = zmq_recv (worker, content, CONTENT_SIZE_MAX, ZMQ_DONTWAIT); // 0);
        if (size > 0) {
            assert (size == CONTENT_SIZE);
            if (is_verbose) printf("worker %s has received from client content = %s\n", worker_identity._, content);
            rc = zmq_getsockopt (worker, ZMQ_RCVMORE, &rcvmore, &sz);
            assert (rc == 0);
            assert (!rcvmore);

            // Send 0..4 replies back
            int reply, replies = 1; //rand() % 5;    ONLY one reply to be conform to the new client definition
            for (reply = 0; reply < replies; reply++) {
                //  Send message from worker to client
                rc = zmq_send (worker, content, CONTENT_SIZE, 0);
                assert (rc == CONTENT_SIZE);
            }
        }
    }
    rc = zmq_close (worker);
    assert (rc == 0);
    rc = zmq_close (control);
    assert (rc == 0);
    if (is_verbose) printf("Destroy worker %s\n", worker_identity._);
}

// The main thread simply starts one client and the proxy, and then
// waits for the server to finish.

int main (void)
{
    setup_test_environment ();

    void *ctx = zmq_ctx_new ();
    assert (ctx);
    // Control socket receives terminate command from main over inproc
    void *control = zmq_socket (ctx, ZMQ_PUB);
    assert (control);
    int rc = zmq_bind (control, "inproc://control");
    assert (rc == 0);

    void* threads [1];

    // generate keys
    rc = zmq_curve_keypair (client_pub, client_sec);
    assert (rc == 0);
    rc = zmq_curve_keypair (worker_pub, worker_sec);
    assert (rc == 0);

    // start client thread with id
    threads[0] = zmq_threadstart  (&client_task, ctx);
    threads[1] = zmq_threadstart  (&server_proxy, ctx);

    zmq_threadclose (threads[0]); // after that, all clients have finished

    // clean everything

    rc = zmq_send (control, "TERMINATE", 10, 0); // makes the workers finish, and then the server task
    assert (rc == 10);
    zmq_threadclose (threads[1]); // wait for the server task to have finished

    rc = zmq_close (control);
    assert (rc == 0);


    msleep (1000); // not sure it is usefull
    rc = zmq_ctx_term (ctx);
    assert (rc == 0);
    return 0;
}
