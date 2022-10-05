OS=$(shell uname)

ifeq (${OS},Linux)
COMPAT=strlcpy.o reallocarray.o
INCLUDES=-I/usr/include/libbson-1.0 -I/usr/include/libmongoc-1.0
endif

ifeq (${OS},Darwin)
COMPAT=reallocarray.o
endif

%.o: compat/%.c
	${CC} ${CFLAGS} -c $<

include Makefile
