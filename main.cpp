#include <unistd.h>
#include <string.h>
#include <iostream>
#include <thread>
#include <AsyncTransport.h>

using std::cout;
using std::endl;
using std::map;
using std::string;
using std::thread;

//Simple packet that just contains byte array or HTTP headers
class PacketImpl : public Packet {
	public:
		PacketImpl() {
			size = 0;
			data = NULL;
		}

		~PacketImpl() {
			if ( size > 0 && data != NULL ) {
				delete [] data;
			}
		}

		unsigned char *data;
		int size;
		map<string, string> headers;
};

class PacketParserImpl : public PacketParser {

	const string secMagic = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

	~PacketParserImpl() { }

	int
	isHTTPTerminated ( unsigned char *buffer, unsigned int bufferSize ) {
		for( int i = 3; i < bufferSize; ++i ) {
			if(    buffer[i-0] == '\n'
				&& buffer[i-1] == '\r'
				&& buffer[i-2] == '\n'
				&& buffer[i-3] == '\r' ) {

				return i;
			}
		}

		return -1;
	}

	map<string, string>
	parseHTTP ( unsigned char *buffer, unsigned int size ) {
		map<string, string> header;

		int beg = 0;
		int end = 0;
		bool hasGet = false;

		for( int i = 1; i < size; ++i ) {
		
			if( buffer[i] == '\n' && buffer[i-1] == '\r' ) {
				
				if( !hasGet ) {
					buffer[i] = '\0';
					string cmd( (const char *)buffer );
					if ( cmd.find_first_of("GET") == 0 ) {
						string value =
							cmd.substr( cmd.find_first_of("/"), cmd.find_first_of("HTTP/1.1")-1 );
						header["URI"] = value;
						hasGet = true;
					} else {
						return header;
					}
				} else {
					end = i - 1;
					string line(buffer+beg, buffer+end);
					int mid      = line.find_first_of(':');
					string key   = line.substr(0, mid);
					string value = line.substr(mid+1, line.size());
					header[key]  = value;
					beg = i+1;
				}
			}
		}

		return header;
	}
	
	
	Packet* deserialize ( unsigned char *buffer, unsigned int bufferSize, unsigned int *bufferUsed ) {
		int index = -1;
		if( bufferSize >= 4 && (index = isHTTPTerminated(buffer, bufferSize)) > 0 ) {
			map<string, string> headers =
				parseHTTP( buffer, index );

			PacketImpl *newPacket = new PacketImpl();
			newPacket->headers = headers;
			(*bufferUsed) = index;
			return (Packet*) newPacket;
		} else {
			return NULL;
		}

		return NULL;
	}

	char * serialize ( Packet *pkt, unsigned int *out_size ) {
		PacketImpl *packet = (PacketImpl*) pkt;
		if ( packet->headers.size() > 0 ) {
			//HTTP response

		} else {
			//Normal Data
			char *out = new char[packet->size];
			memcpy(out, packet->data, packet->size);
			(*out_size) = packet->size;
			return out;
		}
		
		return NULL;
	}
};

typedef void (*recv_data_callback)(unsigned char *, unsigned int);

class cppweb {
	public:
		cppweb( recv_data_callback cb ) {
			onData = cb;

			packetParser   = new PacketParserImpl();
			asyncTransport = new AsyncTransport( packetParser );
			upgraded       = new map<unsigned int, bool>();
		}

		void
		start( int port ) {
			asyncTransport->init( port );
			asyncTransport->start();

			thread t( RecvThread, this );
			t.detach();
		}
	
		~cppweb() {
			//TODO stop thread
			delete packetParser;
			delete asyncTransport;
			delete upgraded;
		}
		
	private:
		PacketParser *packetParser;
		AsyncTransport *asyncTransport;
		map<unsigned int, bool> *upgraded;
		recv_data_callback onData;

		static void RecvThread( cppweb *instance ) {
			recv_data_callback onData  = instance->onData;
			AsyncTransport *asyncTransport = instance->asyncTransport;
			map<unsigned int, bool> *upgraded = instance->upgraded;
			
			while( 1 ) {
					PacketImpl *packet = (PacketImpl*) asyncTransport->getPacket();
					int fd = packet->fd;
					
					if(upgraded->count(fd) > 0) {
						onData(packet->data, packet->size);
					} else {
						//need handshake first
						//If upgrade requested, send reply
						//TODO this
					}
					
					delete packet;
				}	
		}
};

void
onData(unsigned char *data, unsigned int size) {
	cout << "Got data of size: " << size << endl;
	for ( int i = 0; i < size; ++i ) {
		cout << data[i];
	}
	cout << endl << endl;
}

int
main( int argv, char **argc ) {
	cppweb cw(onData);
	cw.start(80);

	while(1) {
		usleep(1000);
	}

	return 0;
}
