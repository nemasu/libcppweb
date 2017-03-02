#include "CppWeb.h"

#include <unistd.h>
#include <string.h>
#include <iostream>
#include <thread>
#include <openssl/sha.h>
#include <openssl/bio.h>
#include <openssl/evp.h>
#include <arpa/inet.h>

using std::cerr;
using std::endl;
using std::map;
using std::string;
using std::thread;

CppWeb::CppWeb( WebListener &listener )
    : webListener( listener ) {
    OpenSSL_add_all_algorithms();
    isRunning = false;
	packetParser.setBinaryFrames(true);
}

void
CppWeb::start( int port ) {
    asyncTransport = new AsyncTransport( packetParser );

	isRunning = true;
	asyncTransport->init( port );
	asyncTransport->start();

	recvThread = thread( RecvThread, std::ref(*this) );
	recvThread.detach();
}

void
CppWeb::startSecure( int port, string certificateFile, string privateKeyFile ) {
    asyncTransport = new TLSTransport( packetParser, certificateFile, privateKeyFile );

    isRunning = true;
	asyncTransport->init( port );
	asyncTransport->start();

	recvThread = thread( RecvThread, std::ref(*this) );
	recvThread.detach();
}

void
CppWeb::stop() {
	isRunning = false;
	asyncTransport->stop();
}

CppWeb::~CppWeb() {
	if( isRunning ) {
		stop();
	}
}

void
CppWeb::close( int fd ) {
	asyncTransport->closeFd(fd);
}

string
CppWeb::base64_encode( unsigned char* data, int size )
{
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
	
	//Free BIO
	BIO_free(b64);
	BIO_free(mem);
	
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
CppWeb::RecvThread( CppWeb &instance ) {
	WebListener &webListener          = instance.webListener;
	AsyncTransport *asyncTransport    = instance.asyncTransport;
	map<unsigned int, bool> &upgraded = instance.upgraded;

	while( instance.isRunning ) {
		//Will block till packet is recvd.
		//If shutting down, NULL packet will be received.
		Packet *packet = asyncTransport->getPacket();
		if( packet != NULL ) {	
			int fd = packet->fd;
			
			if(        packet->type == PacketType::DISCONNECT ) {
				if(DEBUG) std::cerr << "libcppweb: PacketType::DISCONNECT received" << std::endl;
				upgraded.erase(fd);
				webListener.onClose(packet->fd);
			} else if( packet->type == PacketType::CONNECT ) { 
				if(DEBUG) std::cerr << "libcppweb: PacketType::CONNECT received" << std::endl;
				//If connect recv'd on upgraded fd, invalidate.
				upgraded.erase(fd);

				webListener.onConnect(packet->fd);
			} else if ( packet->type == PacketType::NORMAL ) {
				PacketImpl *packetImpl = (PacketImpl *) packet;	
				if (packetImpl->isPing == true) {
					//Put back in transport, will serialize with proper opcode
					asyncTransport->sendPacket(packet);
					continue;
				} else if( upgraded.count(fd) > 0) {
					webListener.onData(fd, packetImpl->data, packetImpl->size);
				} else {
					if(DEBUG) std::cerr << "libcppweb: attempting websocket handshake." << std::endl;
					//need handshake first
					//If upgrade requested, send reply
					map<string, string> &headers = packetImpl->headers;
					
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
						upgraded[fd] = true;
						asyncTransport->sendPacket(response);
						
					} else {
						//Not a websocket request, send 400
						PacketImpl *response = new PacketImpl();
						response->setOrigin((Packet*) packet);
						response->setResponseCode(400);
						asyncTransport->sendPacket(response);

						Packet *disconnect = new Packet();
						disconnect->setOrigin( packet );
						disconnect->type = DISCONNECT;
						asyncTransport->sendPacket( disconnect );
					}
				}
			}
			delete packet;
		}
	}
}

const string CppWeb::SecMagic = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
