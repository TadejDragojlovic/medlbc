# Mini Event Driven Network Load Balancer in C (MEDLBC)
- Miniature network load balancer built based off of NGINX's architecture for learning purposes
- Replicating multiproccessed environment from NGINX:
    1. Master process that handles children proccesses, doesn't communicate directly with clients
    2. Children processes (workers) utilize event driven architecture (epoll) to secure numerous separate communications with clients
- It is lightweight, it is was developed as a deep-dive into system and network programming with the sole purpose to learn about how load balancers work under the hood

<br>

## Implemented features
- Event-driven concurrency via `epoll`: Utilizes linux **epoll** in *edge-triggered* mode to manage thousands of concurrent connections within a single thread (per worker)
- Scalable master-worker model: High-availability architecture where the master process manages lifecycle and health of worker processes
- TCP Load balancing: Efficiently distributes traffic between multiple backends (upstream servers) 
- Round-robin implementation: Uses shared memory and atomic operations to distribute load without the overhead of mutexes
- Graceful shutdown: Implements *self-pipe* with `eventfd` to ensure workers finish active tasks before exiting
- Fault tolerance: Master process automatically detects worker crashes and attempts to spawn replacements immediately

<br>

## Architecture
1. I/O Multiplexing & Non-blocking IO
    - The core of each worker is an asynchronous event loop. By setting all sockets to `O_NONBLOCK` and using `epoll`, the balancer avoids the "one thread per connection" overhead. This allows the system to remain responsive even under high load.
2. NGINX like Process Management
    - **Master** process:
        * Responsible for initialization, signal handling and worker lifecycle management. It creates the shared memory segment and the initial listener socket.
    - **Worker** process:
        * They inherit the listener socket and execute the event loop. Since workers are separate processes, a segmentation fault in one worker does not bring down the entire load balancer.
3. Inter-Process Synchronization
    - To maintain a global Round-Robin index without inter-process locks, I implemented a shared memory region. This ensures that even with multiple workers running on different CPU cores, the traffic distribution remains balanced and thread-safe at the hardware level.
4. Connection State Machine
    - To handle the asynchronous nature of connecting to upstreams while simultaneously reading from clients, this system uses a custom structure `ConnectionContext` state machine. This tracks the lifecycle of every request from `CTX_IDLE` to `CTX_SUCCESS` without blocking the worker's exection flow.

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
- [ ] Config file: similar to nginx, use this config file to setup upstreams, set connection boundaries, or other lb behaviors
- [ ] Health checks: mechanism that frequently checks if the upserver is responsive
- [ ] Weighted Round-robin or other load balancing algorithms

<br>