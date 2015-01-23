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

class WebListener {
	public:
		virtual void onData(int fd, unsigned char *data, unsigned int length) = 0;
		virtual void onConnect(int fd) = 0;
		virtual void onClose(int fd) = 0;
};

class CppWeb {
	public:
		CppWeb( WebListener * );

		~CppWeb();
		
		void
		start( int port );

		void
		send( int fd, unsigned char *data, unsigned int size );

	private:
		static
		string base64_encode( unsigned char* data, int size );
			
		static unsigned int
		Hash(const char *mode, const char* dataToHash, size_t dataSize, unsigned char* outHashed); 
		
		PacketParser *packetParser;
		AsyncTransport *asyncTransport;
		map<unsigned int, bool> *upgraded;
		WebListener *webListener;

		static const string SecMagic;
		
		static void
		RecvThread( CppWeb *instance );
};

#endif
