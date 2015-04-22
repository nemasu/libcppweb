#ifndef __PACKETPARSERIMPL_H__
#define __PACKETPARSERIMPL_H__

#include <map>
#include <AsyncTransport.h>

using std::map;

class PacketParserImpl : public PacketParser {

	public:
		PacketParserImpl(bool isBinary) {
			isBinary = isBinary;
		}

		PacketParserImpl() {
			isBinary = false;
		}

		~PacketParserImpl();

		void
		setBinaryFrames(bool isBinary) {
			this->isBinary = isBinary;
		}

		int
		isHTTPTerminated ( unsigned char *buffer, unsigned int bufferSize );

		map<string, string>
		parseHTTP ( unsigned char *buffer, unsigned int size ); 
		
		Packet*
		deserialize ( unsigned char *buffer, unsigned int bufferSize, unsigned int *bufferUsed );

		char *
		serialize ( Packet *pkt, unsigned int *out_size );

	private:
		bool isBinary;
};

#endif
