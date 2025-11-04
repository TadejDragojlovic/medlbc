INCDIR = include
SRCDIR = src

SRC_FILES := $(wildcard $(SRCDIR)/*.c)

# https://www.cs.colby.edu/maxwell/courses/tutorials/maketutor/

default: all

all: loadbalancer backend_server

loadbalancer: $(filter-out $(SRCDIR)/dummy_backend.c, $(SRC_FILES))
	gcc -o loadbalancer -I$(INCDIR) $^

backend_server: $(SRCDIR)/dummy_backend.c $(SRCDIR)/server.c $(SRCDIR)/utils.c
	gcc -o backend_server -I$(INCDIR) $^

clean:
	rm -f loadbalancer backend_server *.o
