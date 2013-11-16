/*
Timemachine
Copyright (c) 2006 Technische Universitaet Muenchen,
                   Technische Universitaet Berlin,
                   The Regents of the University of California
All rights reserved.


Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions
are met:

1. Redistributions of source code must retain the above copyright
   notice, this list of conditions and the following disclaimer.
2. Redistributions in binary form must reproduce the above copyright
   notice, this list of conditions and the following disclaimer in the
   documentation and/or other materials provided with the distribution.
3. Neither the names of the copyright owners nor the names of its
   contributors may be used to endorse or promote products derived from
   this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESS OR IMPLIED
WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include "types.h"

#include <pcap.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sstream>
#include <pcrecpp.h>

#include "Connection.hh"
#include "packet_headers.h"
#include "Fifo.hh"
#include "Query.hh"
#include "tm.h"

static std::string pattern_ip ("(\\d+\\.\\d+\\.\\d+\\.\\d+)");
static std::string pattern_ipport ("(\\d+\\.\\d+\\.\\d+\\.\\d+):(\\d+)");

inline uint32_t revert_uint32(uint32_t i) {
	uint32_t r;
	((uint8_t*)&r)[0]=((uint8_t*)&i)[3];
	((uint8_t*)&r)[1]=((uint8_t*)&i)[2];
	((uint8_t*)&r)[2]=((uint8_t*)&i)[1];
	((uint8_t*)&r)[3]=((uint8_t*)&i)[0];

	return r;
}

inline uint16_t revert_uint16(uint16_t i) {
	uint16_t r;
	((uint8_t*)&r)[0]=((uint8_t*)&i)[1];
	((uint8_t*)&r)[1]=((uint8_t*)&i)[0];

	return r;
}

inline bool addr_port_canon_lt(uint32_t s_ip, uint32_t d_ip,
							   uint16_t s_port, uint16_t d_port) {
	if (s_ip == d_ip)
		return (s_port < d_port);
	else
		return (s_ip < d_ip);
}

void ConnectionID4::init(proto_t proto,
						 uint32_t s_ip, uint32_t d_ip,
						 uint16_t s_port, uint16_t d_port) {
	v.proto=proto;
	if (addr_port_canon_lt(s_ip,d_ip,s_port,d_port)) {
		//    v.is_canonified=true;
		v.ip1=d_ip;
		v.ip2=s_ip;
		v.port1=d_port;
		v.port2=s_port;
	} else {
		//    v.is_canonified=false;
		v.ip1=s_ip;
		v.ip2=d_ip;
		v.port1=s_port;
		v.port2=d_port;
	}
}

void ConnectionID3::init(proto_t proto,
						 uint32_t ip1, uint32_t ip2,
						 uint16_t port2) {
	v.proto=proto;
	v.ip1=ip1;
	v.ip2=ip2;
	v.port2=port2;
}

void ConnectionID2::init( uint32_t s_ip, uint32_t d_ip) {
	if (addr_port_canon_lt(s_ip,d_ip,0,0)) {
		//    v.is_canonified=true;
		v.ip1=d_ip;
		v.ip2=s_ip;
	} else {
		//    v.is_canonified=false;
		v.ip1=s_ip;
		v.ip2=d_ip;
	}
}

ConnectionID4::ConnectionID4(const u_char* packet) {
	switch (IP(packet)->ip_p) {
	case IPPROTO_UDP:
		init(IP(packet)->ip_p,
			 IP(packet)->ip_src.s_addr, IP(packet)->ip_dst.s_addr,
			 UDP(packet)->uh_sport, UDP(packet)->uh_dport);
		break;
	case IPPROTO_TCP:
		init(IP(packet)->ip_p,
			 IP(packet)->ip_src.s_addr, IP(packet)->ip_dst.s_addr,
			 TCP(packet)->th_sport, TCP(packet)->th_dport);
		break;
	default:
		init(IP(packet)->ip_p,
			 IP(packet)->ip_src.s_addr, IP(packet)->ip_dst.s_addr,
			 0, 0);
		break;
	}
}


ConnectionID3::ConnectionID3(const u_char* packet,
							 int wildcard_port) {
	switch (IP(packet)->ip_p) {
	case IPPROTO_UDP:
		if (wildcard_port) 
			init(IP(packet)->ip_p,
				 IP(packet)->ip_src.s_addr, IP(packet)->ip_dst.s_addr,
				 UDP(packet)->uh_dport);
		else
			init(IP(packet)->ip_p,
				 IP(packet)->ip_dst.s_addr, IP(packet)->ip_src.s_addr,
				 UDP(packet)->uh_sport);
		break;
	case IPPROTO_TCP:
		if (wildcard_port) 
			init(IP(packet)->ip_p,
				 IP(packet)->ip_src.s_addr, IP(packet)->ip_dst.s_addr,
				 TCP(packet)->th_dport);
		else
			init(IP(packet)->ip_p,
				 IP(packet)->ip_dst.s_addr, IP(packet)->ip_src.s_addr,
				 TCP(packet)->th_sport);
		break;
	default:
		if (wildcard_port) 
			init(IP(packet)->ip_p,
				 IP(packet)->ip_src.s_addr, IP(packet)->ip_dst.s_addr,
				 0);
		else
			init(IP(packet)->ip_p,
				 IP(packet)->ip_dst.s_addr, IP(packet)->ip_src.s_addr,
				 0);
		break;
	}
}


ConnectionID2::ConnectionID2(const u_char* packet) {
	init(IP(packet)->ip_src.s_addr, IP(packet)->ip_dst.s_addr);
}


//TODO: MAke this inline (i.e. move to Connection.hh so that it is
//consistent with ConnectionID4
bool ConnectionID3::operator==(const ConnectionID& other) const {
	return (v.proto == ((ConnectionID3*)&other)->v.proto)
		   && (v.ip1 == ((ConnectionID3*)&other)->v.ip1)
		   && (v.ip2 == ((ConnectionID3*)&other)->v.ip2)
		   && (v.port2 == ((ConnectionID3*)&other)->v.port2);
}

//TODO: MAke this inline (i.e. move to Connection.hh so that it is
//consistent with ConnectionID4
bool ConnectionID2::operator==(const ConnectionID& other) const {
	return (v.ip1 == ((ConnectionID2*)&other)->v.ip1)
		   && (v.ip2 == ((ConnectionID2*)&other)->v.ip2);
}

void ConnectionID4::getStr(char* s, int maxsize) const {
	getStr().copy(s, maxsize);

}

void ConnectionID3::getStr(char* s, int maxsize) const {
	getStr().copy(s, maxsize);
}

void ConnectionID2::getStr(char* s, int maxsize) const {
	getStr().copy(s, maxsize);
}

std::string ConnectionID4::getStr() const {
#define UCP(x) ((unsigned char *)&x)

	std::stringstream ss;

	uint32_t s_ip=v.ip1; //get_s_ip();
	uint32_t d_ip=v.ip2; //get_d_ip();

	ss << " ConnectionID4 "
	/*
	 << " Proto " << 0+get_proto()
	 << " canonified " << get_is_canonified() << " "
	*/
	<< (UCP(s_ip)[0] & 0xff) << "."
	<< (UCP(s_ip)[1] & 0xff) << "."
	<< (UCP(s_ip)[2] & 0xff) << "."
	<< (UCP(s_ip)[3] & 0xff)
	<< ":"
	<< ntohs(get_port1())
	<< " - "
	<< (UCP(d_ip)[0] & 0xff) << "."
	<< (UCP(d_ip)[1] & 0xff) << "."
	<< (UCP(d_ip)[2] & 0xff) << "."
	<< (UCP(d_ip)[3] & 0xff)
	<< ":"
	<< ntohs(get_port2());
	return ss.str();
}


std::string ConnectionID3::getStr() const {
#define UCP(x) ((unsigned char *)&x)

	std::stringstream ss;

	uint32_t s_ip=get_ip1();//get_s_ip();
	uint32_t d_ip=get_ip2();//get_d_ip();

	ss << " ConnectionID3 "
	<< (UCP(s_ip)[0] & 0xff) << "."
	<< (UCP(s_ip)[1] & 0xff) << "."
	<< (UCP(s_ip)[2] & 0xff) << "."
	<< (UCP(s_ip)[3] & 0xff)
	<< " - "
	<< (UCP(d_ip)[0] & 0xff) << "."
	<< (UCP(d_ip)[1] & 0xff) << "."
	<< (UCP(d_ip)[2] & 0xff) << "."
	<< (UCP(d_ip)[3] & 0xff)
	<< ":"
	<< get_port();
	return ss.str();
}

std::string ConnectionID2::getStr() const {
#define UCP(x) ((unsigned char *)&x)

	std::stringstream ss;

	uint32_t s_ip=get_ip1();//get_s_ip();
	uint32_t d_ip=get_ip2();//get_d_ip();

	ss << " ConnectionID2 "
	<< (UCP(s_ip)[0] & 0xff) << "."
	<< (UCP(s_ip)[1] & 0xff) << "."
	<< (UCP(s_ip)[2] & 0xff) << "."
	<< (UCP(s_ip)[3] & 0xff)
	<< " - "
	<< (UCP(d_ip)[0] & 0xff) << "."
	<< (UCP(d_ip)[1] & 0xff) << "."
	<< (UCP(d_ip)[2] & 0xff) << "."
	<< (UCP(d_ip)[3] & 0xff);
	return ss.str();
}



// Static Member initialization
std::string ConnectionID4::pattern_connection4 = "\\s*(\\w+)\\s+"
	+ pattern_ipport + "\\s+" + pattern_ipport + "\\s*";
pcrecpp::RE ConnectionID4::re(ConnectionID4::pattern_connection4);

ConnectionID4* ConnectionID4::parse(const char *str) {
	std::string protostr, src_ip, dst_ip;
	unsigned src_port, dst_port;
	proto_t proto;


	if (!re.FullMatch(str, &protostr, &src_ip, &src_port, &dst_ip, &dst_port)) {
		return NULL;
	}
	if (protostr == std::string("tcp"))
		proto = IPPROTO_TCP;
	else 
		proto = IPPROTO_UDP;
		
	return new ConnectionID4(proto, inet_addr(src_ip.c_str()), inet_addr(dst_ip.c_str()),
			htons(src_port), htons(dst_port));
}

void Connection::addPkt(const struct pcap_pkthdr* header, const u_char* packet) {
	last_ts=to_tm_time(&header->ts);
	tot_pkts++;
	tot_pktbytes+=header->caplen;
}

int Connection::deleteSubscription() {
	//fprintf(stderr, "DEBUG deleteSubscription called\n");
	if (subscription) {
		subscription->decUsage();
		if (subscription->getUsage() == 0)  {
			delete(subscription);
			//fprintf(stderr, "DEBUG subscription deleted\n");
		}
		return 1;
	}
	return 0;
}


void Connection::init(ConnectionID4 *id) {
	last_ts=tot_pktbytes=tot_pkts=0;
	subscription=NULL;
	fifo=NULL;
	suspend_cutoff=suspend_timeout=false;

	col_next = col_prev = NULL;
	q_older = q_newer = NULL;
	c_id = id;
}

Connection::Connection(Connection *c) {
	last_ts = c->last_ts;
	tot_pktbytes = c->tot_pktbytes;
	tot_pkts = c->tot_pkts;
	fifo = c->fifo;
	//FIXME: TODO: should we make a deep copy here??
	subscription = c->subscription;
	suspend_cutoff = c->suspend_cutoff;
	suspend_timeout = c->suspend_timeout;

	col_next = col_prev = NULL;
	q_older = q_newer = NULL;

	c_id = new ConnectionID4(c->c_id);
}


std::string Connection::getStr() const {
	std::stringstream ss;
	ss.setf(std::ios::fixed);
	ss << tot_pkts << " pkts, " << tot_pktbytes << " bytes"
	<< ", last packet at " << last_ts
	<< std::endl
	<< (fifo ? "class "+fifo->getClassname() :
		"no class associated")
	<< (suspend_cutoff ? ", cutoff suspended" : "")
	<< (suspend_timeout ? ", timeout suspended" : "")
	<< (subscription ? ", subscription to "+subscription->getStr() : "")
	;
	return c_id->getStr() + " " + ss.str();
}

