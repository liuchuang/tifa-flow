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

//$Id: Fifo.cc 227 2008-01-29 22:40:02Z gregor $
#include <assert.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <sstream>
#include <pcap.h>

#include "types.h"
#include "Fifo.hh"
#include "Connection.hh"
#include "FifoDisk.hh"
#include "tm.h"

Fifo::Fifo() {
	init();
}


Fifo::~Fifo() {
	tmlog(TM_LOG_DEBUG, "storage",  "Fifo::~Fifo");
	if (started) {
		pcap_freecode(&fp);
	}
	started=false;
	if (fm) 
		delete fm;
	if (fd) 
		delete fd;
}


Fifo::Fifo(const std::string& classname, u_fifosize_t fifo_mem_sz, u_fifosize_t fifo_disk_sz, pcap_t* pcap_handle) {
	init();
	this->classname=classname;
	this->fifo_mem_sz=fifo_mem_sz;
	this->fifo_disk_sz=fifo_disk_sz;
	this->ph=pcap_handle;
	this->started=false;
}


void Fifo::init() {
	classname="default";
	filter="";
	fifo_mem_sz=5000000;
	fifo_disk_sz=50000000;
	fifo_disk_filesz=50000000;
	do_cutoff=true;
	cutoff=10000;
	ph=NULL;
	dynTimeout  = 3600 * 4; /* 4 hrs */
	precedence=0;
	pkts_to_disk=1;

	stored_bytes=cutoff_bytes=0;
	stored_pkts=cutoff_pkts=0;

	fm=NULL;
	fd=NULL;
}


void Fifo::start() {
	assert(ph);
	fm=new FifoMem(fifo_mem_sz);
	fd=new FifoDisk(classname, fifo_disk_sz, fifo_disk_filesz, ph);
	fm->setEvictionHandler(this);

	// compile BPF filter
	char* cp=strdup(filter.c_str());
	if(pcap_compile(ph, &fp, cp, 0, 0) == -1) {
		tmlog(TM_LOG_ERROR, "storage", "pcap_compile() error filter string \"%s\"", cp);
		//// error handling!!!!!!!!!!!!!!!!!!!
		exit(1);
	}
	free(cp);

	started=true;
}


bool Fifo::matchPkt(const struct pcap_pkthdr* header,
					const unsigned char* packet) {
	return bpf_filter(fp.bf_insns, (unsigned char*)packet,
					  header->len,
					  header->caplen);
}


byte_count_t Fifo::pktEviction() {
	byte_count_t n=0;
	pkt_count_t i;
	for (i=0; i<pkts_to_disk && fm->getHeldPkts()>0; i++) {
		fd->addPkt(fm->getS());
		n+=fm->popPkt();
	}
	if (i<pkts_to_disk) {
		tmlog(TM_LOG_WARN, "storage", "Strange, only evicted %"PRINTF_UINT64" packets", i);
	}
	return n;
}

// When the connection is subscribed or when a tcp control flag
// is set, then addPkt is called with a connection NULL pointer
bool Fifo::addPkt(const struct pcap_pkthdr* header,
				  const unsigned char* packet, Connection* c) {
	if (started) {
		if (c && doCutoff() && c->getTotPktbytes()>getCutoff()) {
			// cut-off
			cutoff_pkts++;
			cutoff_bytes+=header->len;
			return false;
		} else {
			// normal addition to Memory Fifo
			fm->addPkt(header, packet);
			stored_pkts++;
			stored_bytes+=header->len;
			return true;
		}
	} // if (started)
	else return false;
}


std::string Fifo::getStatsStr() {
	// TODO: This is very ugle: c_str -> string -> c_str. 
	// Should fix this soon!
#define STR_SIZE 1000
	char s[STR_SIZE];
	if (started) {
		snprintf(s, STR_SIZE, 
				"%"PRINTF_UINT64" "
				"%"PRINTF_UINT64" "
				"%"PRINTF_UINT64" "
				"%"PRINTF_UINT64" "
				"%"PRINTF_UINT64" "
				"%"PRINTF_UINT64" "
				"%.2lf "
				"%"PRINTF_UINT64" "
				"%"PRINTF_UINT64" "
				"%.2lf ",
			 stored_bytes,
			 stored_pkts,
			 cutoff_bytes,
			 cutoff_pkts,
			 fm->getHeldBytes(),
			 fm->getHeldPkts(),
			 fm->getNewestTimestamp()-fm->getOldestTimestamp(),
			 fd->getHeldBytes(),
			 fd->getHeldPkts(),
			 fd->getNewestTimestamp()-fd->getOldestTimestamp()
			 );
		return std::string(s);
	} else return "(Fifo "+getClassname()+" not running)";
}

std::string Fifo::getStatsStrHeader() {
	return std::string(
			"stored_bytes "
			"stored_pkts "
			"cut_bytes "
			"cut_pkts "
			"mem_bytes "
			"mem_pkts "
			"mem_dt "
			"disk_bytes "
			"disk_pkts "
			"disk_dt "
		);
}




const FifoMem* Fifo::getFm() {
	return fm;
}


const FifoDisk* Fifo::getFd() {
	return fd;
}


pkt_count_t Fifo::query(QueryRequest *qreq, QueryResult *qres,
				 IntervalSet *interval_set) {
	pkt_count_t matches = 0;
	FifoDiskFile *cur_file;

	if (!qreq->isMemOnly()) {
		fd->incQueryInProgress();
		IntervalSet::iterator i_i = interval_set->begin();
		std::list <FifoDiskFile*>::iterator f_i=fd->filesBeginIt();
		while ( f_i!=fd->filesEndIt() && i_i != interval_set->end() ) {
			cur_file = *f_i;
			f_i++;
#ifdef QUERY_RACE_PROTECT
			if (f_i == fd->filesEndIt())
				fm->lock();
#endif
			/* Check time interval */ 
			tmlog(TM_LOG_WARN, "query", "%d Fifo::query: start-end=[%lf,%lf] * curfile=[%lf,%lf] * fn=%s",
					qres->getQueryID(), qreq->getT0(), qreq->getT1(), 
					cur_file->getOldestTimestamp(), cur_file->getNewestTimestamp(),
					cur_file->getFilename().c_str());
			if ( (qreq->getT1()+1e-3 >= cur_file->getOldestTimestamp()) &&
					(qreq->getT0()-1e-3 <= cur_file->getNewestTimestamp()) ) {
				matches+= cur_file->query(qreq, qres, interval_set);
			}
		}
		fd->decQueryInProgress();
	}
#ifdef QUERY_RACE_PROTECT
	else
		fm->lock();
#endif

#ifndef QUERY_RACE_PROTECT
	/* If QUERY_RACE_PROTECT is set, then fm will be locked at this
	 * point. So we only lock it here if QUERY_RACE_PROTECT is undef
	 */
	fm->lock();
#endif
	matches+= fm->query(qreq, qres, interval_set);
	fm->unlock();
	return matches;
}


