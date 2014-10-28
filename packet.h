#pragma once

//
// RF protocol related definitions
//

#include "debug.h"

#define CTL_CHANNEL 0x40

// Packet types
enum {
	pkt_setup = 1,
	pkt_setup_resp,
	pkt_start,
	pkt_finish,
	pkt_ping   = 0x20,
	pkt_status = 0x40,
	pkt_reset  = 0x80,
};

// Error flags
enum {
	err_proto   = 0x1,
	err_session = 0x4,
	err_crc     = 0x8,
	err_timeout = 0x40,
	err_remote  = 0x80,
};

// Status flags
enum {
	sta_no_ir = 0x1,
};

struct link_info {
	unsigned char rssi;
	struct {
		unsigned char lqi:7;
		unsigned char crc_ok:1;
	};
};

BUILD_BUG_ON(sizeof(struct link_info) != 2);

struct packet {
	unsigned char type; // Packet type
	unsigned char sn;   // Packet seq number incremented in each packet
	unsigned char se;   // Session ID (random number) common for all messages
	unsigned char err;  // Remote side error mask
	// Packet data
	union {
		// pkt_setup
		// Sent from start to finish on channel 0 to setup working channel
		struct {
			unsigned char chan; // Working channel
			unsigned char flags;// Flags (SETUP_F_XXX)
		} setup;
#define SETUP_F_TEST 1
#define SETUP_RESP_DELAY 8
		// pkt_setup_resp
		// Sent from finish to start in response to the pkt_setup
		struct {
			struct link_info li; // Link quality info as seen by remote side
		} setup_resp;
		// pkt_start
		// Sent from start to finish to start timer
		struct {
			unsigned offset; // Time offset due to transmission delay
		} start;
		// pkt_finish
		// Sent from finish to start in response to start command after finish crossing detection
		// In case of timeout the message will have invalid time and err_timeout bit set.
		struct {
			unsigned time; // The time in 1/100 sec (BCD code).
		} finish;
		// pkt_status
		// Sent from finish to start to alert operator
		struct {
			unsigned flags;
		} status;
		// Other packets don't have data
	};
};

BUILD_BUG_ON(sizeof(struct packet) != 6);

struct packet_buff {
	struct packet p;
	struct link_info li;
};

BUILD_BUG_ON(sizeof(struct packet_buff) != 8);
