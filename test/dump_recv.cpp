#include <CppWeb.h> 

using std::cout;
using std::endl;

class Listener : public WebListener {
    public:
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

        void onConnect(int fd) {}
        void onClose(int fd) {}
};

int
main( int argv, char **argc ) {
    Listener listener;
    CppWeb cw(listener);
    cw.start(8000);

    while(1) {
        usleep(1000000);
    }

    return 0;
}
