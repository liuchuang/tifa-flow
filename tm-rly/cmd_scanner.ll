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
/* cmd_scanner.ll        (emacs mode for this is --*-indented-text-*--)
 * $Id: cmd_scanner.ll 253 2009-02-04 10:32:17Z gregor $
 *
 * Lex online commands.
 */

%option prefix="cmd"

/* autotools ylwrap seem to like this filename */
%option outfile="lex.yy.c"

%option noyywrap
%option nounput

%{

  #include <string.h>

  #include "IndexField.hh"
  #include "Query.hh"

  #include "cmd_parser.h"

  void cmderror(const char*);
  extern int cmd_parse_lineno;

  #define YY_NO_UNPUT

%}

ID      [a-zA-Z][a-zA-Z0-9\_\-\.]+
INT     -?[0-9]+
DBL     -?[0-9]*\.[0-9]*
WHITE   [ \t]+
COMMENT \#.*
NEWLINE \n
IP	[0-9]+\.[0-9]+\.[0-9]+\.[0-9]+

%%
"{"		return LBRACE;
"}"		return RBRACE;
"("		return LPAREN;
")"		return RPAREN;
"["		return LBRACK;
"]"		return RBRACK;
";"		return SEMICOLON;
":"		return COLON;
"-"		return DASH;
"*"		return ASTERISK;
"class"		return TOK_CLASS;
"mem"		return TOK_MEM;
"disk"		return TOK_DISK;
"filesize"	return TOK_FILESIZE;
"K"|"k"		return TOK_K;
"M"|"m"		return TOK_M;
"G"|"g"		return TOK_G;
"cutoff"	return TOK_CUTOFF;
"filter"	return TOK_FILTER;
"precedence"	return TOK_PRECEDENCE;
"main"		return TOK_MAIN;
"log_interval"	return TOK_LOG_INTERVAL;
"device"	return TOK_DEVICE;
"read_tracefile"	return TOK_READ_TRACEFILE;
"no"		return TOK_NO;
"workdir"	return TOK_WORKDIR;
"logfile"	return TOK_LOGFILE;
"pkts_to_disk"	return TOK_PKTS_TO_DISK;
"console"	return TOK_CONSOLE;
"max_index_entries"	return TOK_MAX_INDEX_ENTRIES;
"conn_timeout"	return TOK_CONN_TIMEOUT;
"query"		return TOK_QUERY;
"feed"		return TOK_FEED;
"to_file"	return TOK_TO_FILE;
"conn"		return TOK_CONN;
"proto"		return TOK_PROTO;
"start"		return TOK_START;
"end"		return TOK_END;
"print"		return TOK_PRINT;
"nop"		return TOK_NOP;
"tag"		return TOK_TAG;
"ts"		return TOK_TS;
"bro_connect_str"	return TOK_BRO_CONNECT_STR;
"bro_connect"	return TOK_BRO_CONNECT;
"suspend_cutoff"	return TOK_SUSPEND_CUTOFF;
"unsuspend_cutoff"	return TOK_UNSUSPEND_CUTOFF;
"suspend_timeout"	return TOK_SUSPEND_TIMEOUT;
"unsuspend_timeout"	return TOK_UNSUSPEND_TIMEOUT;
"set_dyn_class"	return TOK_SET_DYN_CLASS;
"unset_dyn_class"	return TOK_UNSET_DYN_CLASS;
"orig"      return TOK_ORIG;
"resp"      return TOK_RESP;
"show"		return TOK_SHOW;
"sh"		return TOK_SHOW;
"stats"		return TOK_STATS;
"index"		return TOK_INDEX;
"mem_only"	return TOK_MEM_ONLY;
"subscribe"	return TOK_SUBSCRIBE;
"debug_fifo"	return TOK_DEBUG_FIFOMEM;
"sample"	return TOK_SAMPLE;
"help"		return TOK_HELP;
{INT}		 { cmdlval.i64=atol(cmdtext); return TOK_INTEGER; }
{ID}		 { cmdlval.s=strdup(cmdtext); return TOK_ID; }
{DBL}		 { cmdlval.d=strtod(cmdtext, NULL); return TOK_DOUBLE; }
{IP}		 { cmdlval.ipaddress_p = new IPAddress(cmdtext); return TOK_IP_ADDR; } 
\"[^"]*\"		 { cmdlval.s=strdup(cmdtext+1);
		   cmdlval.s[strlen(cmdlval.s)-1]=0;
		   return TOK_STRING;
		 }
{WHITE}
{COMMENT}
{NEWLINE}	 { cmd_parse_lineno++; }
.		 cmderror("Illegal character in command string");

%%

