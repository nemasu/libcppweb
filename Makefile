CCOPTS=-std=c++11 -fpic -Wall -Werror -I/usr/include/libasock

all: CCOPTS += -O2
all: libcppweb

debug: CCOPTS += -g
debug: libcppweb

libcppweb: CppWeb.o PacketParserImpl.o
	g++ ${CCOPTS} -shared CppWeb.o PacketParserImpl.o -o libcppweb.so -lcrypto -lasock

PacketParserImpl.o: PacketParserImpl.h PacketParserImpl.cpp
	g++ -c ${CCOPTS} PacketParserImpl.cpp -o PacketParserImpl.o

CppWeb.o: CppWeb.cpp CppWeb.h
	g++ -c ${CCOPTS} CppWeb.cpp -o CppWeb.o

clean:
	rm -rf *.o libcppweb*

install: all
	mkdir -p /usr/include/libcppweb
	cp -v CppWeb.h /usr/include/libcppweb
	cp -v libcppweb.so /usr/lib

