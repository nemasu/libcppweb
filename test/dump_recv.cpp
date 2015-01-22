#include <CppWeb.h> 

using std::cout;
using std::endl;

void
onData(int fd, unsigned char *data, unsigned int size) {
	cout << "onData - Fd " << fd << " got data of size: " << size << endl;
	cout << "string: " << data << endl;

	cout << "hex: ";
	for ( unsigned int i = 0; i < size; ++i ) {
		printf("%02X ", data[i] & 0xFF);
	}
	cout << endl;
}

int
main( int argv, char **argc ) {
	CppWeb cw(onData);
	cw.start(8000);

	while(1) {
		usleep(1000000);
	}

	return 0;
}
