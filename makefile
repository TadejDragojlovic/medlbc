INCDIR = include
SRCDIR = src

SRC_FILES := $(wildcard $(SRCDIR)/*.c)

# https://www.cs.colby.edu/maxwell/courses/tutorials/maketutor/

default: loadbalancer

loadbalancer: $(SRC_FILES)
	gcc -o loadbalancer -I$(INCDIR) $^

clean:
	rm -f loadbalancer *.o
