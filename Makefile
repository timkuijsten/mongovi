OS=$(shell uname)

COMPAT=""

ifeq (${OS},Linux)
COMPAT=strlcat.o strlcpy.o reallocarray.o
endif

ifeq (${OS},Darwin)
COMPAT=reallocarray.o
endif

INCDIR=-I/usr/local/include/libbson-1.0/ -I/usr/local/include/libmongoc-1.0/

CFLAGS=-Wall -Wextra ${INCDIR}
LDFLAGS=-lmongoc-1.0 -lbson-1.0 -ledit
OBJ=jsmn.o jsonify.o mongovi.o shorten.o

mongovi: ${OBJ} ${COMPAT}
	$(CC) ${CFLAGS} -o $@ ${OBJ} ${COMPAT} ${LDFLAGS}

%.o: %.c
	$(CC) ${CFLAGS} -c $<

%.o: compat/%.c
	$(CC) ${CFLAGS} -c $<

test: ${OBJ}
	$(CC) $(CFLAGS) shorten.c test/shorten.c -o shorten-test
	./shorten-test

.PHONY: clean 
clean:
	rm -f mongovi ${OBJ}
