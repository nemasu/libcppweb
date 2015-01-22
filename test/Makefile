
all: test_dump_recv test_echo

test_dump_recv: dump_recv.cpp
	g++ -g -std=c++11 -L../ -I../ -I/usr/include/libasock dump_recv.cpp ../libcppweb.so -o test_dump_recv

test_echo: echo.cpp
	g++ -g -std=c++11 -L../ -I../ -I/usr/include/libasock echo.cpp ../libcppweb.so -o test_echo

clean:
	rm -rf *.o test_*
