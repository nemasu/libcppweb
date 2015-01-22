#include "CppWeb.h"
#include "PacketImpl.h"
#include "PacketParserImpl.h"

#include <unistd.h>
#include <string.h>
#include <iostream>
#include <thread>
#include <openssl/sha.h>
#include <openssl/bio.h>
#include <openssl/evp.h>
#include <arpa/inet.h>

using std::cout;
using std::cerr;
using std::endl;
using std::map;
using std::string;
using std::thread;

typedef void (*recv_data_callback)(int, unsigned char *, unsigned int);

CppWeb::CppWeb( recv_data_callback cb ) {
	OpenSSL_add_all_algorithms();
	
	onData = cb;

	packetParser   = new PacketParserImpl();
	asyncTransport = new AsyncTransport( packetParser );
	upgraded       = new map<unsigned int, bool>();
}

void
CppWeb::start( int port ) {
	asyncTransport->init( port );
	asyncTransport->start();

	thread t( RecvThread, this );
	t.detach();
}

CppWeb::~CppWeb() {
	//TODO stop thread
	delete packetParser;
	delete asyncTransport;
	delete upgraded;
}

string
CppWeb::base64_encode( unsigned char* data, int size )
{
	// bio is simply a class that wraps BIO* and it free the BIO in the destructor.

	BIO *b64 = BIO_new(BIO_f_base64()); // create BIO to perform base64
	BIO_set_flags(b64,BIO_FLAGS_BASE64_NO_NL);

	BIO *mem = BIO_new(BIO_s_mem()); // create BIO that holds the result

	// chain base64 with mem, so writing to b64 will encode base64 and write to mem.
	BIO_push(b64, mem);

	// write data
	bool done = false;
	while(!done)
	{
		int res = BIO_write(b64, data, (int)size);

		if(res <= 0) // if failed
		{
			if(BIO_should_retry(b64)){
				continue;
			}
			else // encoding failed
			{
				/* Handle Error!!! */
				cerr << "base64_encode BIO_should_retry error" << endl;
				done = true;
			}
		}
		else // success!
			done = true;
	}

	int flushRet = BIO_flush(b64);
	if (flushRet != 1) {
		cerr << "base64_encode BIO_flush error" << endl;
		return "";
	}

	// get a pointer to mem's data
	char* dt;
	long len = BIO_get_mem_data(mem, &dt);

	// assign data to output
	std::string s(dt, len);
	return s;
}

unsigned int
CppWeb::Hash(const char *mode, const char* dataToHash, size_t dataSize, unsigned char* outHashed) {

	unsigned int md_len = -1;
	const EVP_MD *md = EVP_get_digestbyname(mode);
	if(NULL != md) {
		EVP_MD_CTX mdctx;
		EVP_MD_CTX_init(&mdctx);
		EVP_DigestInit_ex(&mdctx, md, NULL);
		EVP_DigestUpdate(&mdctx, dataToHash, dataSize);
		EVP_DigestFinal_ex(&mdctx, outHashed, &md_len);
		EVP_MD_CTX_cleanup(&mdctx);
	}
	return md_len;
}

void
CppWeb::send( int fd, unsigned char *data, unsigned int length ) {

	PacketImpl *packet = new PacketImpl();

	packet->data = new unsigned char[length];
	packet->size = length;
	memcpy(packet->data, data, length);
	packet->fd = fd;

	asyncTransport->sendPacket(packet);
}

void
CppWeb::RecvThread( CppWeb *instance ) {
	recv_data_callback onData  = instance->onData;
	AsyncTransport *asyncTransport = instance->asyncTransport;
	map<unsigned int, bool> *upgraded = instance->upgraded;
	
	while( 1 ) {
		PacketImpl *packet = (PacketImpl*) asyncTransport->getPacket();
		int fd = packet->fd;

		if(       packet->type == DISCONNECT ) {
			upgraded->erase(fd);
		} else if(upgraded->count(fd) > 0) {
			onData(packet->fd, packet->data, packet->size);
		} else {
			//need handshake first
			//If upgrade requested, send reply
			map<string, string> &headers = packet->headers;
			
			if( headers.count("Upgrade") > 0 && headers.count("Sec-WebSocket-Key") > 0 
					&& headers["Upgrade"] == "websocket" ) {
				
				string key = headers["Sec-WebSocket-Key"] + SecMagic;
				unsigned char hashed[SHA_DIGEST_LENGTH];
				Hash("SHA1", (const char *)key.c_str(), key.length(), hashed);
				string encodedKey = base64_encode( hashed, SHA_DIGEST_LENGTH );

				PacketImpl *response = new PacketImpl();
				response->setOrigin((Packet*) packet);
				response->setResponseCode(101);
				map<string, string> &responseHeaders = response->headers;
				responseHeaders["Upgrade"]    = "websocket";
				responseHeaders["Connection"] = "Upgrade";
				responseHeaders["Sec-WebSocket-Accept"] = encodedKey;
				(*upgraded)[fd] = true;
				asyncTransport->sendPacket(response);
				
			} else {
				//Not a websocket request, send 400
				PacketImpl *response = new PacketImpl();
				response->setOrigin((Packet*) packet);
				response->setResponseCode(400);
				asyncTransport->sendPacket(response);

				Packet *disconnect = new Packet();
				disconnect->setOrigin( (Packet *)packet );
				disconnect->type = DISCONNECT;
				asyncTransport->sendPacket( disconnect );
			}

		}
		
		delete packet;
	}	
}

const string CppWeb::SecMagic = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
