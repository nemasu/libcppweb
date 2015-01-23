#include <CppWeb.h> 

using std::cout;
using std::endl;

CppWeb *cw;

class Listener : public WebListener {
	public:

		void
		onData(int fd, unsigned char *data, unsigned int size) {
			cw->send(fd, data, size);
		}

		void onConnect(int fd){}
		void onClose(int fd){}
};

Listener *listener;

int
main( int argv, char **argc ) {
	listener = new Listener();
	cw = new CppWeb(listener);
	cw->start(8000);

	while(1) {
		usleep(1000000);
	}

	delete cw;
	delete listener;
	
	return 0;
}
