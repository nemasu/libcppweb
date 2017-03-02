#include "PacketParserImpl.h" 

#include "PacketImpl.h"
#include <string.h>
#include <iostream>
#include <string>

using std::cout;
using std::endl;
using std::string;


PacketParserImpl::~PacketParserImpl() { }

int
PacketParserImpl::isHTTPTerminated ( unsigned char *buffer, unsigned int bufferSize ) {
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
PacketParserImpl::parseHTTP ( unsigned char *buffer, unsigned int size ) {
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


Packet* 
PacketParserImpl::deserialize ( unsigned char *buffer, unsigned int bufferSize, unsigned int *bufferUsed ) {
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
        unsigned char finOp = buffer[idx++];

        /*for ( unsigned int i = 0; i < bufferSize; ++i ) {
            printf("%02X ", buffer[i] & 0xFF);
        }
        cout << endl;*/

        uint64_t length = buffer[idx++] & 0x7F;

        if ( length == 126 ) {
            length = 0;
            length = buffer[idx++];
            length = length << 8;
            length = length | buffer[idx++];

        } else if ( length == 127 ) {
            length = 0;
            length |= (((long long)buffer[idx++]) << 56);
            length |= (((long long)buffer[idx++]) << 48);
            length |= (((long long)buffer[idx++]) << 40);
            length |= (((long long)buffer[idx++]) << 32);
            length |= (((long long)buffer[idx++]) << 24);
            length |= (((long long)buffer[idx++]) << 16);
            length |= (((long long)buffer[idx++]) << 8);
            length |= (((long long)buffer[idx++]));
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

        //Special close socket code
        if( length == 2 && dest[0] == 0x03 && dest[1] == 0xe9 ) {
            packet->type = DISCONNECT;
        } else if ( finOp == 137 ) {
            //Fin & Ping
            packet->isPing = true;
        } else if ( finOp == 138 ) {
            //Fin & Pong, ignore
            delete packet;
            return NULL;
        }

        return packet;
    }

    return NULL;
}

char *
PacketParserImpl::serialize ( Packet *pkt, unsigned int *out_size ) {
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
        unsigned char *rawData = packet->data;

        int frameCount  = 0;
        char frame[10];

        //Ping packet. Put back into queue, make it a pong
        if( packet->isPing ) {
            frame[0] = (char) 138; // Fin & pong
        } else {
            if ( isBinary ) {
                frame[0] = (char) 130; //Fin and Binary data
            } else {
                frame[0] = (char) 129; //Fin and text data
            }
        }
            
        long long packetSize = packet->size;
        if( packetSize <= 125 ) {
            frame[1] = (char) packetSize;
            frameCount = 2;
        } else if( packetSize >= 126 && packetSize <= 65535 ) {
            frame[1] = (char) 126;
            int len = packetSize;
            frame[2] = (char) ((len >> 8 ) & (char) 255);
            frame[3] = (char) (len & (char) 255); 
            frameCount = 4;
        } else {
            frame[1] = (char) 127;
            long long len = packetSize;
            frame[2] = (char) ((len >> 56 ) & (char)255);
            frame[3] = (char) ((len >> 48 ) & (char)255);
            frame[4] = (char) ((len >> 40 ) & (char)255);
            frame[5] = (char) ((len >> 32 ) & (char)255);
            frame[6] = (char) ((len >> 24 ) & (char)255);
            frame[7] = (char) ((len >> 16 ) & (char)255);
            frame[8] = (char) ((len >> 8  ) & (char)255);
            frame[9] = (char) (len & (char)255);
            frameCount = 10;
        }

        char *out = new char[frameCount + packetSize];

        memcpy(out,              frame,   frameCount);
        memcpy(out + frameCount, rawData, packetSize);
        
        (*out_size) = frameCount + packetSize;
        return out;
    }
    
    return NULL;
}
