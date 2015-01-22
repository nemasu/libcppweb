#ifndef __CPPWEB_H__
#define __CPPWEB_H__

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

typedef void (*recv_data_callback)(int, unsigned char *, unsigned int);

class CppWeb {
	public:
		CppWeb( recv_data_callback cb );

		~CppWeb();
		
		void
		start( int port );

	private:
		static
		string base64_encode( unsigned char* data, int size );
			
		static unsigned int
		Hash(const char *mode, const char* dataToHash, size_t dataSize, unsigned char* outHashed); 
		
		PacketParser *packetParser;
		AsyncTransport *asyncTransport;
		map<unsigned int, bool> *upgraded;
		recv_data_callback onData;

		static const string SecMagic;
		
		static void
		RecvThread( CppWeb *instance );
};

#endif
