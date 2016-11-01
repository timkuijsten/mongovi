OS=$(shell uname)

COMPAT=""

ifeq (${OS},Linux)
COMPAT=strlcat.o strlcpy.o reallocarray.o
endif

ifeq (${OS},Darwin)
COMPAT=reallocarray.o
endif

INCDIR=-I/usr/include/libbson-1.0/ -I/usr/include/libmongoc-1.0/ -I/usr/local/include/libbson-1.0/ -I/usr/local/include/libmongoc-1.0/

CFLAGS=-Wall -Wextra ${INCDIR}
LDFLAGS=-lmongoc-1.0 -lbson-1.0 -ledit
OBJ=jsmn.o jsonify.o main.o mongovi.o shorten.o prefix_match.o

mongovi: ${OBJ} ${COMPAT}
	$(CC) ${CFLAGS} -o $@ ${OBJ} ${COMPAT} ${LDFLAGS}

%.o: %.c
	$(CC) ${CFLAGS} -c $<

%.o: compat/%.c
	$(CC) ${CFLAGS} -c $<

test: ${OBJ} ${COMPAT}
	$(CC) $(CFLAGS) mongovi.c prefix_match.c test/parse_path.c -o mongovi-test jsmn.o jsonify.o shorten.o ${COMPAT} ${LDFLAGS}
	./mongovi-test

test-dep:
	$(CC) $(CFLAGS) shorten.c test/shorten.c -o shorten-test
	./shorten-test
	$(CC) $(CFLAGS) prefix_match.c compat/reallocarray.c test/prefix_match.c -o prefix_match-test
	./prefix_match-test

depend:
	$(CC) ${CFLAGS} -E -MM *.c > .depend

.PHONY: clean 
clean:
	rm -f ${OBJ} mongovi shorten-test prefix_match-test mongovi-test
