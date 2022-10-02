OS=$(shell uname)

PROG=   mongovi

VERSION_MAJOR   = 2
VERSION_MINOR   = 0
VERSION_PATCH   = 0

COMPAT=""

ifeq (${OS},Linux)
COMPAT=strlcpy.o reallocarray.o
endif

ifeq (${OS},Darwin)
COMPAT=reallocarray.o
endif

ifndef USRDIR
  USRDIR=  /usr/local
endif
BINDIR=  $(USRDIR)/bin
MANDIR=  $(USRDIR)/share/man

SRCFILES = shorten.h jsonify.h jsmn.h parse_path.c test/parse_path.c \
	    test/shorten.c test/jsonify.c test/prefix_match.c compat/compat.h \
	    compat/strlcpy.c compat/reallocarray.c mongovi.c shorten.c \
	    jsonify.c prefix_match.h prefix_match.c parse_path.h jsmn.c

INCDIR=-I$(DESTDIR)/usr/include/libbson-1.0/ -I$(DESTDIR)/usr/include/libmongoc-1.0/ -I$(DESTDIR)/usr/local/include/libbson-1.0/ -I$(DESTDIR)/usr/local/include/libmongoc-1.0/

CFLAGS=-std=c17 -Wall -Wextra -pedantic ${INCDIR} -DVERSION_MAJOR=${VERSION_MAJOR} -DVERSION_MINOR=${VERSION_MINOR} -DVERSION_PATCH=${VERSION_PATCH}
LDFLAGS=-lmongoc-1.0 -lbson-1.0 -ledit
OBJ=jsmn.o jsonify.o mongovi.o shorten.o prefix_match.o parse_path.o

INSTALL_DIR=  install -dm 755
INSTALL_BIN=  install -m 555
INSTALL_MAN=  install -m 444

${PROG}: Makefile ${OBJ} ${COMPAT}
	$(CC) ${CFLAGS} -o $@ ${OBJ} ${COMPAT} ${LDFLAGS}

%.o: %.c
	$(CC) ${CFLAGS} -c $<

%.o: compat/%.c
	$(CC) ${CFLAGS} -c $<

%.o: test/%.c
	$(CC) ${CFLAGS} -c $<

testparsepath: parse_path.h parse_path.c test/parse_path.c ${OBJ} ${COMPAT}
	$(CC) $(CFLAGS) test/parse_path.c -o testparsepath ${COMPAT} ${LDFLAGS}

testshorten: shorten.h shorten.c test/shorten.c ${OBJ} ${COMPAT}
	$(CC) $(CFLAGS) test/shorten.c -o testshorten

testprefixmatch: prefix_match.h prefix_match.c test/prefix_match.c ${OBJ} ${COMPAT}
	$(CC) $(CFLAGS) test/prefix_match.c -o testprefixmatch

testjsonify: jsonify.h jsonify.c test/jsonify.c jsmn.o ${OBJ} ${COMPAT}
	$(CC) $(CFLAGS) test/jsonify.c -o testjsonify jsmn.o

runtest: testshorten testprefixmatch testparsepath testjsonify
	./testshorten
	./testprefixmatch
	./testparsepath
	./testjsonify

lint:
	${CC} ${CFLAGS} -fsyntax-only ${SRCFILES} ${HDRFILES} ${SRCFILES} 2>&1

install:
	${INSTALL_DIR} ${DESTDIR}${BINDIR}
	${INSTALL_DIR} ${DESTDIR}${MANDIR}/man1
	${INSTALL_BIN} ${PROG} ${DESTDIR}${BINDIR}
	${INSTALL_MAN} ${PROG}.1 ${DESTDIR}${MANDIR}/man1

depend:
	$(CC) ${CFLAGS} -E -MM *.c > .depend

.PHONY: clean 
clean:
	rm -f ${OBJ} ${COMPAT} mongovi testshorten testprefixmatch testparsepath testjsonify
