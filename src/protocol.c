#include "csapp.h"
#include "protocol.h"
#include "debug.h"

/*
 * Send a packet, which consists of a fixed-size header followed by an
 * optional associated data payload.
 *
 * @param fd  The file descriptor on which packet is to be sent.
 * @param hdr  The fixed-size packet header, with multi-byte fields
 *   in network byte order
 * @param data  The data payload, or NULL, if there is none.
 * @return  0 in case of successful transmission, -1 otherwise.
 *   In the latter case, errno is set to indicate the error.
 *
 * All multi-byte fields in the packet are assumed to be in network byte order.
 */
int proto_send_packet(int fd, JEUX_PACKET_HEADER *hdr, void *data){
	ssize_t n;
	if((n = rio_writen(fd, (void *) hdr, sizeof(JEUX_PACKET_HEADER)) ) == -1){
        return -1;
    }
    uint16_t payload_size = ntohs(hdr->size);
	if(payload_size > 0 && data != NULL){
		if((n = rio_writen(fd, data, payload_size)) == -1){
            return -1;
        }
        debug("=> %d.%d: type=%d size=%d id=%d role=%d, payload=[%p]", ntohl(hdr->timestamp_sec), ntohl(hdr->timestamp_nsec), hdr->type, ntohs(hdr->size), hdr->id, hdr->role, (char *) data);
	} else {
        debug("=> %d.%d: type=%d size=%d id=%d role=%d (no payload)", ntohl(hdr->timestamp_sec), ntohl(hdr->timestamp_nsec), hdr->type, ntohs(hdr->size), hdr->id, hdr->role);
    }
	return 0;
}

/*
 * Receive a packet, blocking until one is available.
 *
 * @param fd  The file descriptor from which the packet is to be received.
 * @param hdr  Pointer to caller-supplied storage for the fixed-size
 *   packet header.
 * @param datap  Pointer to a variable into which to store a pointer to any
 *   payload received.
 * @return  0 in case of successful reception, -1 otherwise.  In the
 *   latter case, errno is set to indicate the error.
 *
 * The returned packet has all multi-byte fields in network byte order.
 * If the returned payload pointer is non-NULL, then the caller has the
 * responsibility of freeing that storage.
 */
int proto_recv_packet(int fd, JEUX_PACKET_HEADER *hdr, void **payloadp){
    // // read header
    ssize_t n;
    if((n = rio_readn(fd, (void *) hdr, sizeof(JEUX_PACKET_HEADER))) == -1){
        *payloadp = NULL;
        return -1;
    }
    int header_size = ntohs(hdr->size);
    char *payload;
    if(header_size != 0){
        payload = (char *) Malloc(header_size + 1);
        if((n = rio_readn(fd, (void *) payload, header_size)) == -1){
            *payloadp = NULL;
            return -1;
        }
        payload[header_size] = '\0';
        *payloadp = payload;
        debug("<= %d.%d: type=%d, size=%d, id=%d, role=%d, payload=%s", ntohl(hdr->timestamp_sec), ntohl(hdr->timestamp_nsec), hdr->type, ntohs(hdr->size), hdr->id, hdr->role, payload);
    } else {
        *payloadp = NULL;
        debug("<= %d.%d: type=%d, size=%d, id=%d, role=%d (no payload)", ntohl(hdr->timestamp_sec), ntohl(hdr->timestamp_nsec), hdr->type, ntohs(hdr->size), hdr->id, hdr->role);
    }

    return 0;
}

    // size_t nleft = sizeof(JEUX_PACKET_HEADER);
    // int counter = 0;
    // // in case of short count
    // JEUX_PACKET_HEADER *hdrp = hdr;
    // while(counter < sizeof(JEUX_PACKET_HEADER)){
    //     nread = read(fd, hdrp, nleft);
    //     if(nread <= 0){
    //         return -1;
    //     }
    //     counter += nread;
    //     nleft -= nread;
    //     hdrp += nread;
    // }


    // // read payload
    // uint16_t datasize = ntohs(hdr->size);
    // (*hdr).size = datasize;
    // (*hdr).timestamp_sec = ntohl(hdr->timestamp_sec);
    // (*hdr).timestamp_nsec = ntohl(hdr->timestamp_nsec);

    // if(datasize > 0){
    //     void *payloadptr = calloc(1, datasize);
    //     if(payloadptr == NULL){
    //         return -1;
    //     }
    //     void *startofPayload = payloadptr;
    //     int counter2 = 0;
    //     size_t nleft2 = datasize;
    //     while(counter2 < datasize){
    //         nread = read(fd, payloadptr, nleft2);
    //         if(nread <= 0){
    //             return -1;
    //         }
    //         payloadptr += nread;
    //         counter2 += nread;
    //         nleft -= nread;
    //     }
    //     *payloadp = startofPayload;
    // } else {
    //     payloadp = NULL;
    // }

    // return 0;


    // rio_t rio;
    // memset(hdr, 0, sizeof(JEUX_PACKET_HEADER));
    // Rio_readinitb(&rio, fd);
    // Rio_readlineb(&rio, (void *) hdr, sizeof(JEUX_PACKET_HEADER));
    // if(hdr->type == 0){
    //     return -1;
    // }

    // if(hdr->size > 0){
    //     // There's payload
    //     // Convert from NBO to HBO for size of payload data
    //     uint16_t s = ntohs(hdr->size);
    //     void *data;
    //     if ( (data=calloc(s, 1)) == NULL){
    //         debug("Malloc error in proto_recv_packet.");
    //     }
    //     void *startofpayload = data;
    //     int n = rio_readlineb(&rio, data, s);
    //     if(n < 0){
    //         debug("rio_readlineb error in photo_r1cv_packet.");
    //         return -1;
    //     }
    //     // printf("I read in %d bytes for payload: %s\n", n, data);
    //     *payloadp = startofpayload;
    // } else {
    //     payloadp = NULL;
    // }
    // debug("=> %d.%d: type=%d size=%d id=%d role=%d payload: %s", ntohl(hdr->timestamp_sec), ntohl(hdr->timestamp_nsec), hdr->type, ntohs(hdr->size), hdr->id, hdr->role, (char *) *payloadp);
    // return 0;
// }

