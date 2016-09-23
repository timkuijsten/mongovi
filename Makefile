OS= $(shell uname)

ifeq (${OS},Linux)
INCDIR= -I/usr/include/libbson-1.0/ -I/usr/include/libmongoc-1.0/
else
INCDIR= -I/usr/local/include/libbson-1.0/ -I/usr/local/include/libmongoc-1.0/
endif

CFLAGS= -Wall -Wextra ${INCDIR}
LDFLAGS=-lmongoc-1.0 -lbson-1.0 -ledit
OBJ= jsmn.o jsonify.o mongovi.o shorten.o common.o
COMPAT= compat-strlcat.o compat-strlcpy.o

mongovi: ${OBJ} ${COMPAT}
	$(CC) ${CFLAGS} -o $@ ${OBJ} ${COMPAT} ${LDFLAGS}

%.o: %.c
	$(CC) ${CFLAGS} -c $<

%.o: compat/%.c
	$(CC) ${CFLAGS} -c $<

.PHONY: clean 
clean:
	rm -f mongovi ${OBJ}
