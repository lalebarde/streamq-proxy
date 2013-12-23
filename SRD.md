# Software Requirements Document

## Functional Requirements

| # | Requirement | Status |
| --:| ---------------- |:---------:|
| 100 | The proxy SHALL be transparent to the clients and the workers, as if they were connected directly one-to-one. | - |
| 110 | The proxy SHALL have a ZMQ_STREAM socket called frontend to interface with the clients, and another one called backend for the workers. | - |
| 120 | The proxy SHALL keep a list of clients and workers as they connect and store their identity. | - |
| 130 | The proxy SHALL pool the frontend when at least one worker is available. | - |
| 140 | When a worker connects, its messages cannot be forwarded until there is a client. So its identity along with its ZMTP signature SHALL be stored. | - |
| 150 | When a client connects, a persistent pairing is performed between its identity and the identity of a worker. Persistent means that the same client SHALL communicate always with the same worker all the time it is connected. | - |
| 160 | Pairing, thought persistent, SHALL be performed in a load balancing pattern. A client will be assigned to an available worker or the less loaded one. It SHALL be possible to assign the same client to the same worker for all connexions, with a list of fallbacks. | - |
| 170 | After the pairing is performed, the worker identity and signature previously stored SHALL be forwarded to its assigned client and then all messages are forwarded in both ways. | - |
| 180 | Message forwarding consists in receiving a multipart message from a peer that starts with its identity, withdraw the identity of the destiny in the pairing table, resend the message with first the identity of the destiny, and then the rest of the message, except the identity of the origin. | - |
| 190 | Message forwarding either receives from the frontend and resend to the backend, or receives from the backend and resend to the frontend. | - |
| 200 | The pairing table SHALL perform a pair identity access in o(1). | - |
| 210 | The proxy MAY manage IDENTITY optionaly set on the client or worker socket. It SHALL not manage it by decoding the ZMTP metadata, but through the control socket. | - |
| 220 | Any mechanism shall be able to be used, not only CURVE. | - |
| 230 | Clients and worker disconnexions SHALL be managed*. When one peer is disconnected, the pairing table SHALL be updated. | - |

TODO: precise how disconnexions should be managed. Probably throught the control
socket when possible. Strategies shall be discussed when disconnexion is accidental.
Some possibilities are 1) Heartbeat via the control socket between each peer and the proxy,
 2) mandatory heartbeat between the client and the worker - then the proxy pairs would have
a TTL, 3) Simple proxy pair TTL. Possibly we could authorize a choice of strategies.

## Implementation Requirements

| # | Requirement | Status |
| --:| ---------------- |:---------:|
| 900 | Language SHALL be C++ | OK |

## Requirements on requirements

| # | Requirement | Status |
| --:| ---------------- |:---------:|
| 10 | Requirements numbers, when assigned, SHALL never be changed | OK |
|20 | Status SHALL be one of the following: `OK` Released, `I` Implemented but not enough tested, `-` Not yet implemented. | OK |
|30 | Requirement numbers 0 to 99 are reserved for *Requirements on requirements*, 100 to 799 for *Functional Requirements*, 800 to 899 to *Interface Control Requirements*, 900 to 999 for *Implementation Requirements* | OK |


