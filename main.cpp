#include <unistd.h>
#include <string.h>
#include <iostream>
#include <thread>
#include <openssl/sha.h>
#include <openssl/bio.h>
#include <openssl/evp.h>
#include <arpa/inet.h>
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
				data = NULL;
				size = 0;
			}
		}

		void
		setResponseCode(int code) {
			switch(code) {
				case 101:
					headers["Code"] = "101";
					break;
				case 400:
					headers["Code"] = "400";
					break;
			}
		}

		unsigned char *data;
		int size;
		map<string, string> headers;
};

class PacketParserImpl : public PacketParser {


	~PacketParserImpl() { }

	int
	isHTTPTerminated ( unsigned char *buffer, unsigned int bufferSize ) {
		for( unsigned int i = 3; i < bufferSize; ++i ) {
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

		for( unsigned int i = 1; i < size; ++i ) {
		
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
					string value = line.substr(mid+2, line.size());
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
			(*bufferUsed) = index+1;
			return (Packet*) newPacket;
		} else {
			int idx = 0;
			//Don't care if its done or not, just want data
			//unsigned char fin = buffer[idx++] & 0x01;
			idx++;
			
			/*for ( unsigned int i = 0; i < bufferSize; ++i ) {
				printf("%02X ", buffer[i] & 0xFF);
			}
			cout << endl;*/

			uint64_t length = buffer[idx++] & 0x7F;

			if ( length == 126 ) {
				length = *((uint16_t *)&buffer[idx]);
				idx += sizeof(uint16_t);
			} else if ( length == 127 ) {
				length = *((uint64_t *)&buffer[idx]);
				idx += sizeof(uint64_t);
			}
			
			char mask[4];
			memcpy( mask, &buffer[idx], 4 );
			idx += 4;
			
			PacketImpl *packet = new PacketImpl();
			packet->data = new unsigned char[length];
			packet->size = length;

			unsigned char *dest = packet->data;
			for ( unsigned int i = 0; i < length; i++ ) {
				dest[i] = buffer[i+idx] ^ mask[i % 4];
			}

			(*bufferUsed) = idx + length;
			return packet;
		}

		return NULL;
	}

	char * serialize ( Packet *pkt, unsigned int *out_size ) {
		PacketImpl *packet = (PacketImpl*) pkt;
		if ( packet->headers.size() > 0 ) {
			map<string, string> &headers = packet->headers;
			//HTTP response
			int size = 0;
			
			//CMD
			static char cmd101[] = "HTTP/1.1 101 Switching Protocols\r\n";
			static char cmd400[] = "HTTP/1.1 400 Bad Request\r\n";
			
			char *cmd = NULL;
			int cmdSize = 0;

			if (       headers["Code"] == "101") {
				cmd = cmd101;
				cmdSize = sizeof(cmd101);
			} else if (headers["Code"] == "400") {
				cmd = cmd400;
				cmdSize = sizeof(cmd400);
			}
			
			headers.erase("Code");
			size += cmdSize;
			
			//Headers size
			for ( auto e : headers ) {
				size += (e.first.length() + e.second.length() + 4); //4 = colon + space + \r\n
			}
			size += 2;//ending \r\n
			
			char *outs = new char[size];
			int idx = 0;

			//Write the CMD
			memcpy(outs+idx, cmd, cmdSize);
			idx += cmdSize;

			for ( auto e : headers ) {
				memcpy(outs+idx, e.first.c_str(), e.first.length());
				idx += e.first.length();
				
				memcpy(outs+idx, ": ", 2);
				idx += 2;

				memcpy(outs+idx, e.second.c_str(), e.second.length());
				idx += e.second.length();
				
				memcpy(outs+idx, "\r\n", 2);
				idx += 2;

			}
			
			memcpy(outs+idx, "\r\n", 2);
			idx += 2;

			//idx should equal size here
			if ( idx != size ) {
				std::cerr << "PacketParserImpl::serialize error, sizes do not match" << endl;
			}

			(*out_size) = size;
			return outs;
		} else {
			//Normal Data
			//TODO Encode into frame
			char *out = new char[packet->size];
			memcpy(out, packet->data, packet->size);
			(*out_size) = packet->size;
			return out;
		}
		
		return NULL;
	}
};

typedef void (*recv_data_callback)(int, unsigned char *, unsigned int);

class cppweb {
	public:
		cppweb( recv_data_callback cb ) {
			OpenSSL_add_all_algorithms();
			
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
		static string base64_encode( unsigned char* data, int size )
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
						done = true;
					}
				}
				else // success!
					done = true;
			}

			BIO_flush(b64);

			// get a pointer to mem's data
			char* dt;
			long len = BIO_get_mem_data(mem, &dt);

			// assign data to output
			std::string s(dt, len);
			return s;
		}
	
		static unsigned int
		Hash(const char *mode, const char* dataToHash, size_t dataSize, unsigned char* outHashed) {
		
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

		PacketParser *packetParser;
		AsyncTransport *asyncTransport;
		map<unsigned int, bool> *upgraded;
		recv_data_callback onData;

		static const string SecMagic;
		
		static void RecvThread( cppweb *instance ) {
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
};

const string cppweb::SecMagic = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

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
	cppweb cw(onData);
	cw.start(8000);

	while(1) {
		usleep(1000000);
	}

	return 0;
}
