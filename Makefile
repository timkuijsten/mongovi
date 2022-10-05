CFLAGS += -std=c17 -Wall -Wextra -pedantic-errors ${INCLUDES} \
    -DVERSION_MAJOR=${VERSION_MAJOR} -DVERSION_MINOR=${VERSION_MINOR} \
    -DVERSION_PATCH=${VERSION_PATCH}

LDFLAGS += -lmongoc-1.0 -lbson-1.0 -ledit

PREFIX = /usr/local
BINDIR = ${PREFIX}/bin
MANDIR = ${PREFIX}/man

VERSION_MAJOR = 2
VERSION_MINOR = 0
VERSION_PATCH = 0

INSTALL_DIR = install -dm 755
INSTALL_ETC = install -m 0640
INSTALL_BIN = install -m 0555
INSTALL_MAN = install -m 0444

SRCFILES = shorten.h jsonify.h jsmn.h parse_path.c test/parse_path.c \
	    test/shorten.c test/jsonify.c test/prefix_match.c compat/compat.h \
	    compat/strlcpy.c compat/reallocarray.c mongovi.c shorten.c \
	    jsonify.c prefix_match.h prefix_match.c parse_path.h jsmn.c

mongovi: mongovi.o jsmn.o jsonify.o shorten.o prefix_match.o parse_path.o \
    ${COMPAT}
	${CC} ${CFLAGS} -o $@ mongovi.o jsmn.o jsonify.o shorten.o \
	    prefix_match.o parse_path.o ${COMPAT} ${LDFLAGS}

.SUFFIXES: .c .o
.c.o:
	${CC} ${CFLAGS} -c $<

lint:
	${CC} ${CFLAGS} -fsyntax-only ${SRCFILES} 2>&1

testshorten: shorten.c test/shorten.c
	${CC} ${CFLAGS} -o $@ test/shorten.c

testprefixmatch: prefix_match.c test/prefix_match.c ${COMPAT}
	${CC} ${CFLAGS} -o $@ test/prefix_match.c ${COMPAT}

testparsepath: parse_path.c test/parse_path.c
	${CC} ${CFLAGS} -o $@ test/parse_path.c

testjsonify: jsonify.c test/jsonify.c jsmn.o
	${CC} ${CFLAGS} -o $@ test/jsonify.c jsmn.o

runtests: testshorten testprefixmatch testparsepath testjsonify
	./testshorten
	./testprefixmatch
	./testparsepath
	./testjsonify

install:
	${INSTALL_DIR} ${DESTDIR}${BINDIR}
	${INSTALL_DIR} ${DESTDIR}${MANDIR}/man1
	${INSTALL_BIN} mongovi ${DESTDIR}${BINDIR}
	${INSTALL_MAN} mongovi.1 ${DESTDIR}${MANDIR}/man1

manhtml:
	mandoc -T html -Ostyle=man.css mongovi.1 > mongovi.1.html

clean:
	rm -f *.o *.html mongovi testshorten testprefixmatch testparsepath \
	    testjsonify
