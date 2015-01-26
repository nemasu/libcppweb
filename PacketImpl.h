#ifndef __PACKETIMPL_H__
#define __PACKETIMPL_H__

#include <AsyncTransport.h>

//Simple packet that just contains byte array or HTTP headers
class PacketImpl : public Packet {
    public:
        PacketImpl() {
            size   = 0;
            data   = NULL;
            isPing = false;
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
		bool isPing;
        map<string, string> headers;
};

#endif
