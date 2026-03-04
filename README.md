# Mini Event Driven Network Load Balancer in C (MEDLBC)
- Miniature network load balancer built based off of NGINX's architecture for learning purposes
- Replicating multiproccessed environment from NGINX:
    1. Master process that handles children proccesses, doesn't communicate directly with clients
    2. Children processes (workers) utilize event driven architecture (epoll) to secure numerous separate communications with clients
- It is lightweight, it is was developed as a deep-dive into system and network programming with the sole purpose to learn about how load balancers work under the hood

<br>

## Implemented functionalities

<br>

## Architecture

<br>

## Build
- This project is built specifically for Linux environments to leverage low-level kernel features.
- System requirements:
    * Linux (Kernel 2.6.27+ required for `epoll` and `eventfd`)
    * GCC / Clang (C11)
    * GNU Make
- Building the project:
    1. Clone the repository locally
    2. In the project directory run: `make`
    3. Start the dummy backend (basic tcp echo servers): `./upserver <PORT>`
        * You can start multiple backends, but number of upservers and specific ports and IPS need to be configured in the source file `worker.c`, specifically global variable `static struct UpstreamServer upstream_servers`
    4. Run the load balancer: `./loadbalancer`

<br>

## Future Roadmap
- [] Config file: similar to nginx, use this config file to setup upstreams, set connection boundaries, or other lb behaviors
- [] Health checks: mechanism that frequently checks if the upserver is responsive
- [] Weighted Round-robin or other load balancing algorithms

<br>