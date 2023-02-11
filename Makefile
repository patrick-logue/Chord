CC=gcc
CFLAG_INCLUDE=-I. -Iinclude -Iprotobuf -lm
CFLAGS=-Wall $(CFLAG_INCLUDE) -Wextra -std=gnu99 -ggdb
LDLIBS=-lprotobuf-c -lcrypto
SRC=src
VPATH= $(SRC) include protobuf

all: example_hash chord_protobuf chord

example_hash: hash.o example_hash.o
	$(CC) $(CFLAGS) $(SRC)/hash.c $(SRC)/example_hash.c -o example_hash $(LDLIBS)

chord_protobuf:
	protoc-c --c_out=. protobuf/chord.proto

chord: hash.o chord_arg_parser.o protobuf/chord.pb-c.c chord.c a4protos.o chord_functions.o

a4protos.o: a4protos.c a4protos.h

chord_functions.o: chord_functions.c chord_functions.h 

clean:
	rm -rf protobuf/*.pb-c.* *~ chord *.o example_hash

.PHONY : clean all
