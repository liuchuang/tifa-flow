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

// $Id: Fifo.hh 220 2007-12-02 20:19:42Z gregor $

#ifndef FIFO_HH
#define FIFO_HH

//#include <pthread.h>
//#define _REENTRANT
#include <string>

/* resolve circular header file dependency by forward declarations
 */

//class Fifo;
class FifoDisk;

#include "FifoMem.hh"
//#include "Index.hh"
#include "Connection.hh"
#include "tm.h"

/*
class Fifo::Conf {
  Fifo::Conf() {
    classname="default";
    fifo_mem_sz=5000000;
    fifo_disk_sz=50000000;
    cutoff=10000;
  }
protected:
  std::string classname;
  u_fifosize_t fifo_mem_sz;
  u_fifosize_t fifo_disk_sz;
  byte_count_t cutoff;
  pcap_t* ph;
}
*/

class Fifo: FifoMemEvictionHandler {
public:
	Fifo();
	Fifo(const std::string& classname, u_fifosize_t fifo_mem_sz,
		 u_fifosize_t fifo_disk_sz, pcap_t*);
	void start();
	virtual ~Fifo();
	byte_count_t pktEviction();
	bool addPkt(const struct pcap_pkthdr* header, const unsigned char* packet,
				Connection*);
	const FifoMem* getFm();
	const FifoDisk* getFd();
	void setCutoff(byte_count_t n) {
		cutoff=n;
	}
	bool doCutoff() {
		return do_cutoff;
	}
	void enableCutoff() {
		do_cutoff=true;
	}
	void disableCutoff() {
		do_cutoff=false;
	}
	byte_count_t getCutoff() {
		return cutoff;
	}
	void setClassname(std::string s) {
		classname=s;
	}
	std::string getClassname() {
		return classname;
	}
	void setFifoMemSz(u_fifosize_t s) {
		fifo_mem_sz=s;
	}
	void setFifoDiskSz(u_fifosize_t s) {
		fifo_disk_sz=s;
	}
	void setFifoDiskFileSz(u_fifosize_t s) {
		fifo_disk_filesz=s;
	}
	void setPcapHandle(pcap_t* ph) {
		this->ph=ph;
	}
	void setFilter(std::string f) {
		filter=f;
	}
	void setPrecedence(int i) {
		precedence=i;
	}
	void setPktsToDisk(int i) {
		pkts_to_disk=i;
	}
	void setDynTimeout(tm_time_t t) {
		dynTimeout = t;
	}
	int getPrecedence() {
		return precedence;
	}
	tm_time_t getDynTimeout() {
		return dynTimeout;
	}
	bool matchPkt(const struct pcap_pkthdr*, const unsigned char*);
	byte_count_t getStoredBytes() {
		return stored_bytes;
	}
	byte_count_t getCutofBbytes() {
		return cutoff_bytes;
	}
	pkt_count_t getStoredPkts()   {
		return stored_pkts;
	}
	pkt_count_t getCutoffPkts()   {
		return cutoff_pkts;
	}
	std::string getStatsStr();
	static std::string getStatsStrHeader();
	pkt_count_t query(QueryRequest*, QueryResult*, IntervalSet*);
protected:
	bool do_cutoff;
	byte_count_t cutoff;
	std::string classname;
	std::string filter;
	u_fifosize_t fifo_mem_sz;
	u_fifosize_t fifo_disk_sz;
	u_fifosize_t fifo_disk_filesz;
	int precedence;
	tm_time_t dynTimeout;
	pcap_t* ph;
	bool started;
	struct bpf_program fp;

	pkt_count_t pkts_to_disk;

	// statistics
	byte_count_t stored_bytes;
	byte_count_t cutoff_bytes;
	pkt_count_t stored_pkts;
	pkt_count_t cutoff_pkts;

	FifoMem* fm;
	FifoDisk* fd;
	//   pthread_t m2d_thread_id;
	//   volatile int m2d_thread_stop;
	//  int i;

	void init();
	//  static void* m2d_thread(void* arg);

};

#endif /* FIFO_HH */
