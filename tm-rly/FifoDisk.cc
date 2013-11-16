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

// $Id: FifoDisk.cc 253 2009-02-04 10:32:17Z gregor $
//
#include <assert.h>
#include <unistd.h>
#include <pcap.h>
#include <string>
#include <pthread.h>

#include "config.h"
#ifdef HAVE_LIBPCAPNAV
#include <pcapnav.h>
#endif

#include "tm.h"
#include "FifoDisk.hh"
#include "Query.hh"


/***************************************************************************
 * FifoDisk
 */


/* Query and Pkt eviction interaction 
 *
 * There is a concurrency between running queries and ongoing packet
 * eviction from FifoMem to FifoDisk. 
 *
 * a) The FifoDiskFiles std::list must not be changed while a query is 
 *    in progress, because it may corrupt the list (XXX: check this).
 *    Therefore adding or deleteing files is inhibited by chcking
 *    queryInProgress()
 * b) There is a race condition between writing/flushing evicted packets
 *    to disk and querying them. It may happen that the query thread has
 *    finished searching the most recent file in FifoDisk and is
 *    therefore going to search the FifoMem. When a packet is evicted
 *    from FifoMem during this transition period that packet will not
 *    be found be the query. 
 *    To prevent this, Fifo::query will acquire the FifoMem lock
 *    before searching the last file
 *    NOTE: It may well be, that the query thread will hold
 *    the lock for too long so that the capture thread will loose
 *    packets. A better solution must be found/.
 *
 *    These protections an be enabled/disabled at compile time. 
 *    iby Default  it is DISABLED. 
 *    See tm.h 
 */

FifoDisk::FifoDisk(const std::string& classname, u_fifosize_t size,
				   u_fifosize_t file_size, pcap_t* pcap_handle):
		classname(classname), size(size), file_size(file_size),
		tot_bytes(0), tot_pkts(0),
		file_number(0), pcap_handle(pcap_handle),
held_bytes(0), held_pkts(0), oldestTimestamp(0), newestTimestamp(0), queries(0) {

	pthread_mutex_init(&query_in_progress_mutex, NULL);

}


FifoDisk::~FifoDisk() {
	pthread_mutex_destroy(&query_in_progress_mutex);
	while (!files.empty()) {
		delete(files.back());
		files.pop_back();
	}
}


void FifoDisk::addPkt(const pkt_ptr p) {
	if (size>0) {
		if (files.empty() ||
				files.back()->getCurFileSize()
				+ sizeof(struct pcap_file_header)
				+ sizeof(struct pcap_pkthdr)
				+ ((struct pcap_pkthdr*)p)->caplen > (int)file_size) {  /* Why do we have to be THAT precise?!?!? */
			// Do not add or delete files while a query is in progress, because 
			// the file iterator of the query might get fucked up. 
			// XXX: This my starve the rotation of files or generate files that
			// are way too large. Check it. 
			lockQueryInProgress();
			if (!queries) {  /* no queries in progress at the moment */
				// need new file
				if (!files.empty()) {
					// close current file which just ran full
					files.back()->close();
					if (held_bytes+file_size > size) {
						// delete oldest file
						held_bytes-=files.front()->getHeldBytes();
						held_pkts-=files.front()->getHeldPkts();
						files.front()->remove();
						delete(files.front());
						files.pop_front();
						oldestTimestamp = files.front()->getOldestTimestamp();
					}
				}
				file_number++;
				const int strsz=classname.length()+1+8+1; // "_" + "number" + "\0"
				char *new_file_name=(char*)malloc(strsz);
				snprintf(new_file_name, strsz, "%s_%08x",
						 classname.c_str(), file_number);
				files.push_back(new FifoDiskFile(new_file_name, pcap_handle));
				free(new_file_name);
			}
			unlockQueryInProgress();
		}
		newestTimestamp = to_tm_time(&(((struct pcap_pkthdr*)p)->ts));
		files.back()->addPkt(p);
		if (oldestTimestamp < 1e-3)
			oldestTimestamp = files.front()->getOldestTimestamp();
		held_bytes+=sizeof(struct pcap_pkthdr)+((struct pcap_pkthdr*)p)->caplen;
		held_pkts++;
		tot_bytes+=sizeof(struct pcap_pkthdr)+((struct pcap_pkthdr*)p)->caplen;
		tot_pkts++;
	}
}

tm_time_t FifoDisk::getOldestTimestamp() const {
	return oldestTimestamp;
}
tm_time_t FifoDisk::getNewestTimestamp() const {
	return newestTimestamp;
}

/*
pkt_count_t FifoDisk::query(QueryRequest *qreq, QueryResult *qres,
					 IntervalSet *interval_set) {
	FifoDiskFile *cur_file;
	pkt_count_t matches=0;
	IntervalSet::iterator i_i=interval_set->begin();
	std::list <FifoDiskFile*>::iterator f_i=files.begin();
	while ( f_i!=files.end() && i_i != interval_set->end() ) {
		cur_file = *f_i;
		f_i++;
		if (f_i == files.end()) {
			lockLastFile();
		}
		matches += cur_file->query(interval_set, qreq, qres);
	}
	return matches;
}
*/


/***************************************************************************
 * FifoDiskFile
 */

FifoDiskFile::FifoDiskFile(const std::string& filename, pcap_t* pcap_handle):
		filename(filename), is_open(false), cur_file_size(0), held_bytes(0), held_pkts(0),
		pcap_handle(pcap_handle),
		oldest_timestamp(0),
newest_timestamp(0) {
	open();
}


void FifoDiskFile::open() {
	pcap_dumper_handle=pcap_dump_open(pcap_handle, filename.c_str());
	if (!pcap_dumper_handle) {
		char *pcap_errstr = pcap_geterr(pcap_handle);
		tmlog(TM_LOG_ERROR, "storage", "could not open file %s: %s",
				filename.c_str(), pcap_errstr);
	} else {
		is_open=true;
	}
}

FifoDiskFile::~FifoDiskFile() {
	if (is_open) close();
}


void FifoDiskFile::remove() {
	if (is_open) close();
	unlink(filename.c_str());
}


void FifoDiskFile::close() {
	pcap_dump_close(pcap_dumper_handle);
	is_open=false;
}

void FifoDiskFile::addPkt(const struct pcap_pkthdr *header,
						  const unsigned char *packet) {
	assert(is_open==true);
	pcap_dump((u_char*)pcap_dumper_handle,
			  header,                         // pcap header
			  packet);                        // packet
	if (held_pkts==0) oldest_timestamp=to_tm_time(&header->ts);
	else newest_timestamp=to_tm_time(&header->ts);
	held_pkts++;
	held_bytes+=sizeof(struct pcap_pkthdr)+header->caplen;
	//held_bytes+=header->caplen;
	cur_file_size += sizeof(struct pcap_pkthdr)+header->caplen;
}

void FifoDiskFile::addPkt(pkt_ptr p) {
	addPkt((struct pcap_pkthdr*)p,         // pcap header
		   p+sizeof(struct pcap_pkthdr));  // packet
}

pkt_count_t FifoDiskFile::query( QueryRequest *qreq, QueryResult *qres, IntervalSet *set) {
	pkt_count_t matches = 0;
	unsigned scanned_packets=0;
#ifdef HAVE_LIBPCAPNAV
	ConnectionID4 *c_id;
	struct timeval tv1, tv2;
	struct timeval tv;
	int res;
	int intcnt=0;
	int first_pkt_for_this_int;


	// FIXME: Protect the pcap_dumper_handle from capture thread!!
	if (is_open)
		flush();

	pcapnav_t *ph=pcapnav_open_offline(filename.c_str());
	if (!ph) {
		char *pcap_errstr = pcapnav_geterr(ph);
		tmlog(TM_LOG_ERROR, "query", "%d FifoDiskFile::query: could not open file %s: %s",
				qres->getQueryID(), filename.c_str(), pcap_errstr);
	} else {
		struct pcap_pkthdr hdr;
		const u_char *pkt;

		if (pcapnav_get_timespan(ph, &tv1, &tv2) != 0) {
			tmlog(TM_LOG_WARN, "query",  "%d pcapnav could not obtain timespan.",
					qres->getQueryID());
			  /* Rest of error handling */
		}
		tmlog(TM_LOG_DEBUG, "query", "%d FifoDiskFile::query: opened file %s. timespan is [%lf,%lf]",
				qres->getQueryID(), filename.c_str(), to_tm_time(&tv1), to_tm_time(&tv2));

		for (IntervalSet::iterator it=set->begin(); it!=set->end(); it++) {
			// FIXME: get rid of this assert
			assert(getNewestTimestamp() >= getOldestTimestamp());
			/* XXX: this should be handled by pcapnav_goto_timestamp.... 
			if (getOldestTimestamp() > (*it).getLast() ||
					getNewestTimestamp() < (*it).getStart() ) {
				fprintf(stderr, "Nicht im File: [%lf, %lf] <> [%lf,%lf]\n", 
						getOldestTimestamp(), getNewestTimestamp(), 
						(*it).getStart(), (*it).getLast());
				continue;
			}
			*/
			tmlog(TM_LOG_DEBUG, "query", "%d FifoDiskFile: New Int %i of %i: [%lf, %lf]", intcnt, set->getNumIntervals(),
					qres->getQueryID(), it->getStart(), it->getLast());
			
			tv.tv_sec=(int)(*it).getStart();
			tv.tv_usec=(int)(1000000*((*it).getStart()-tv.tv_sec));
			
			// Check if interval overlaps trace start
			// FIXME: Don't hardcode the security margin with 1ms!!
			if ( (*it).getLast()+1e-3 >= to_tm_time(&tv1) &&
					(*it).getStart() <= to_tm_time(&tv1)) {
				res = PCAPNAV_DEFINITELY;
				pcapnav_goto_offset(ph, 0, PCAPNAV_CMP_LEQ);
				tmlog(TM_LOG_DEBUG, "query", "%d Interval overlapped trace start. Goto 0",
						qres->getQueryID());
			}
			else 
				res = pcapnav_goto_timestamp(ph, &tv);
			switch(res) {
				case PCAPNAV_ERROR:
					tmlog(TM_LOG_ERROR, "query", " %d pcapnav_goto_timestamp ERROR", qres->getQueryID()); 
					break;
				case PCAPNAV_NONE:
					tmlog(TM_LOG_DEBUG, "query", "%d pcapnav_goto_timestamp NONE", qres->getQueryID()); 
					break;
				case PCAPNAV_CLASH:
					tmlog(TM_LOG_ERROR, "query", "%d pcapnav_goto_timestamp CLASH", qres->getQueryID()); 
					break;
				case PCAPNAV_PERHAPS:
					tmlog(TM_LOG_ERROR, "query", "%d pcapnav_goto_timestamp PERHAPS", qres->getQueryID()); 
					break;
				default:
					break;
			}
			if (res != PCAPNAV_DEFINITELY) {
				continue;
			}
			first_pkt_for_this_int = 1;
			do {
				pkt = pcapnav_next(ph, &hdr);
				scanned_packets++;
				if (!pkt)
					break;
				tm_time_t t=to_tm_time(&hdr.ts);
				if (first_pkt_for_this_int) {
					tmlog(TM_LOG_DEBUG, "query", "First packet ts for this int: %lf", t);
					first_pkt_for_this_int=0;
				}
				if (t>(*it).getLast())
					break;
				if (t>qreq->getT1())
					break;
				if (t<qreq->getT0())
					continue;
				if (qreq->matchPkt(&hdr, pkt))  {
					matches++;
					qres->sendPkt(&hdr, pkt);
					if (qreq->isSubscribe()) {
						c_id = new ConnectionID4(pkt);
						storage->getConns().subscribe(c_id, qres);
						delete c_id;
					}
				}
			} while (pkt);
		}
	}
	//DEBUG
	tmlog(TM_LOG_DEBUG, "query", "%d FifoDiskFile::query [HAVE_LIBPCAPNAV] finished; matches %u; examined %u", 
			qres->getQueryID(), (unsigned)matches, scanned_packets);

	pcapnav_close(ph);
#else
	//TODO: Check Timespan!
	printf("FifoDiskFile::query file %s\n", getFilename().c_str());

	char errbuf[PCAP_ERRBUF_SIZE]="";
	pcap_t *ph=pcap_open_offline(filename.c_str(), errbuf);

	if (!ph) {
		char *pcap_errstr = pcap_geterr(ph);
		fprintf(stderr, "FifoDiskFile::query: could not open file %s: %s\n",
		        filename.c_str(), pcap_errstr);
	} else {
		int rc; /* pcap_next_ex rc meanings: 1 no problem, -2 no more packets */
		do {
			struct pcap_pkthdr *hdr;
			const u_char *pkt;
			rc=pcap_next_ex(ph, &hdr, &pkt);
			//printf("pcap_next_ex; rc=%d\n", rc);
			if (rc==1) {
				/*
				tm_time_t t=to_tm_time(&hdr->ts);
				while ( (*i)->getLast() < t && *i != set->end() ) *i++;
				if (t >= (*i)->getStart() && t <= (*i)->getLast()) {
				*/
				if (qreq->matchPkt(hdr, pkt)) {
					qres->sendPkt(hdr, pkt);
					matches++;
				}
				//	}
			}
		} while (rc==1);
	}
	//DEBUG
	fprintf(stderr, "FifoDiskFile::query [HAVE_LIBPCAPNAV] finished; matches %u\n", matches);
#endif
	return matches;
}
