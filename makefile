INCDIR = include
SRCDIR = src

SRC_FILES := $(wildcard $(SRCDIR)/*.c)

# https://www.cs.colby.edu/maxwell/courses/tutorials/maketutor/

default: all

all: loadbalancer upserver

loadbalancer: $(filter-out $(SRCDIR)/dummy_backend.c, $(SRC_FILES))
	gcc -o loadbalancer -I$(INCDIR) $^

upserver: $(SRCDIR)/dummy_backend.c $(SRCDIR)/server.c $(SRCDIR)/utils.c
	gcc -o upserver -I$(INCDIR) $^

clean:
	rm -f loadbalancer upserver *.o
