
all: libcppweb

libcppweb: main.cpp
	clang++ -Wall -Werror -g -std=c++11 -I/usr/include/libasock main.cpp -lasock -o libcppweb

clean:
	rm -rf *.o libcppweb
