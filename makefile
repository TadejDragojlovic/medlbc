INCDIR = include
SRCDIR = src

# https://www.cs.colby.edu/maxwell/courses/tutorials/maketutor/

default: loadbalancer

loadbalancer: $(SRCDIR)/lb.c $(SRCDIR)/utils.c $(SRCDIR)/worker.c $(SRCDIR)/connection.c
	gcc -o loadbalancer -I$(INCDIR) $^

clean:
	rm -f loadbalancer *.o