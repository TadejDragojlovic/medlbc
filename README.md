# Mini Event Driven Network Load Balancer in C (MEDLBC)
- Miniature network load balancer built based off of NGINX's architecture for learning purposes
- Replicating multiproccessed environment from NGINX:
    1. Master process that handles children proccesses, doesn't communicate directly with clients
    2. Children processes (workers) utilize event driven architecture (epoll) to secure numerous separate communications with clients