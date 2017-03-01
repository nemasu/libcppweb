CC=g++
CCOPTS=-std=c++11 -fpic -Wall -Werror

all: CCOPTS += -O2
all: libcppweb strip

debug: CCOPTS += -g
debug: libcppweb

libcppweb: CppWeb.o PacketParserImpl.o
	${CC} ${CCOPTS} -shared CppWeb.o PacketParserImpl.o -o libcppweb.so -lcrypto -lasock -lpthread

PacketParserImpl.o: PacketParserImpl.h PacketParserImpl.cpp
	${CC} -c ${CCOPTS} PacketParserImpl.cpp -o PacketParserImpl.o

CppWeb.o: CppWeb.cpp CppWeb.h
	${CC} -c ${CCOPTS} CppWeb.cpp -o CppWeb.o

strip: libcppweb
	strip --strip-debug libcppweb.so

clean:
	rm -rf *.o libcppweb*

install: all
	mkdir -p /usr/include/libcppweb
	cp -v *.h /usr/include/libcppweb
	cp -v libcppweb.so /usr/lib

