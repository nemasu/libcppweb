#include <CppWeb.h> 

using std::cout;
using std::endl;

CppWeb *cw;

void
onData(int fd, unsigned char *data, unsigned int size) {
	cw->send(fd, data, size);
}

int
main( int argv, char **argc ) {
	cw = new CppWeb(onData);
	cw->start(8000);

	while(1) {
		usleep(1000000);
	}

	delete cw;
	return 0;
}
