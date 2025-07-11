#include <iomanip>
#include <iostream>
#include <algorithm>
#include <sstream>
#include <syslog.h>
#include <sys/syscall.h>

#include "tcpreassembly.h"
#include "webrtc.h"
#include "ssldata.h"
#include "sip_tcp_data.h"
#include "diameter.h"
#include "sql_db.h"
#include "tools.h"
#include "sniff_inline.h"
#include "ssl_dssl.h"
#include "websocket.h"


using namespace std;


#define USE_PACKET_DATALEN true
#define PACKET_DATALEN(datalen, datacaplen) (USE_PACKET_DATALEN ? datalen : datacaplen)

extern int check_sip20(char *data, unsigned long len, ParsePacket::ppContentsX *parseContents, bool isTcp);

extern char opt_tcpreassembly_http_log[1024];
extern char opt_tcpreassembly_webrtc_log[1024];
extern char opt_tcpreassembly_ssl_log[1024];
extern char opt_tcpreassembly_sip_log[1024];
extern char opt_tcpreassembly_diameter_log[1024];
extern char opt_pb_read_from_file[256];
extern int verbosity;
extern bool opt_ssl_reassembly_ipport_reverse_enable;

#define ENABLE_DEBUG(type, subEnable) (_ENABLE_DEBUG(type, subEnable) && _debug_stream)
#define _ENABLE_DEBUG(type, subEnable) ((type == TcpReassembly::http ? sverb.tcpreassembly_http : \
					 type == TcpReassembly::webrtc ? sverb.tcpreassembly_webrtc : \
					 type == TcpReassembly::ssl ? sverb.tcpreassembly_ssl : \
					 type == TcpReassembly::sip ? sverb.tcpreassembly_sip : 0) && (subEnable))
#define ENABLE_CLEANUP_LOG(type) (type == TcpReassembly::sip ? sverb.tcpreassembly_sip_cleanup : 0)
bool _debug_packet = true;
bool _debug_rslt = true;
bool _debug_data = true;
bool _debug_check_ok = true;
bool _debug_check_ok_process = true;
bool _debug_save = true;
bool _debug_cleanup = true;
bool _debug_print_content_summary = true;
bool _debug_print_content = false;
u_int16_t debug_counter = 0;
u_int16_t debug_limit_counter = 0;
u_int32_t debug_seq = 0;

static std::ostream *_debug_stream = NULL;


bool TcpReassemblyData::isFill() {
	return(this->request.size());
}


void TcpReassemblyStream_packet_var::push(TcpReassemblyStream_packet packet) {
	map<uint32_t, TcpReassemblyStream_packet>::iterator iter;
	iter = this->queuePackets.find(packet.next_seq);
	if(iter == this->queuePackets.end() ||
	   (iter->second.datalen && iter->second.data[0] == 0 && packet.datalen && packet.data[0] != 0)) {
		this->queuePackets[packet.next_seq];
		this->queuePackets[packet.next_seq] = packet;
	}
	this->last_packet_at_from_header = getTimeMS(&packet.time);
}

void TcpReassemblyStream::push(TcpReassemblyStream_packet packet) {
	if(link->reassembly->enableSmartCompleteData) {
		this->clearCompleteData();
		this->is_ok = false;
	}
	if(debug_seq && packet.header_tcp.seq == debug_seq) {
		cout << " -- XXX DEBUG SEQ XXX" << endl;
	}
	this->queuePacketVars[packet.header_tcp.seq].push(packet);
	if(PACKET_DATALEN(packet.datalen, packet.datacaplen)) {
		exists_data = true;
	}
	this->last_packet_at_from_header = getTimeMS(&packet.time);
}

int TcpReassemblyStream::ok(bool crazySequence, bool enableSimpleCmpMaxNextSeq, u_int32_t maxNextSeq,
			    int enableValidateDataViaCheckData, int needValidateDataViaCheckData, int unlimitedReassemblyAttempts,
			    TcpReassemblyStream *prevHttpStream, bool enableDebug,
			    u_int32_t forceFirstSeq, int ignorePsh) {
	if(enableValidateDataViaCheckData == -1) {
		enableValidateDataViaCheckData = link->reassembly->enableValidateDataViaCheckData || link->reassembly->enableStrictValidateDataViaCheckData;
	}
	if(needValidateDataViaCheckData == -1) {
		needValidateDataViaCheckData = link->reassembly->needValidateDataViaCheckData;
	}
	if(unlimitedReassemblyAttempts == -1) {
		unlimitedReassemblyAttempts = link->reassembly->unlimitedReassemblyAttempts;
	}
	if(ignorePsh == -1) {
		ignorePsh = link->reassembly->ignorePshInCheckOkData;
	}
	if(this->is_ok) {
		return(1);
	}
	if(link->reassembly->getType() != TcpReassembly::http && !unlimitedReassemblyAttempts && counterTryOk > link->reassembly->maxReassemblyAttempts) {
		if(sverb.tcpreassembly_ext) {
			static u_int64_t lastTimeErrorLog_ms = 0;
			u_int64_t actTimeMS = getTimeMS_rdtsc();
			if(!lastTimeErrorLog_ms ||
			   actTimeMS > lastTimeErrorLog_ms + 10000) {
				cLogSensor::log(cLogSensor::info,
						"limiting configuration value sip_tcp_reassembly_stream_max_attempts was reached during tcp reassembly");
				lastTimeErrorLog_ms = actTimeMS;
			}
		}
		if(enableDebug && _debug_stream) {
			(*_debug_stream)
				<< " --- reassembly failed ack: " << this->ack
				<< " (sip_tcp_reassembly_stream_max_attempts was reached)"
				<< " (" << __FILE__ << ":" << __LINE__ << ")"
				<< endl;
		}
		return(1);
	}
	++counterTryOk;
	this->cleanPacketsState();
	if(!this->queuePacketVars.begin()->second.getNextSeqCheck()) {
		if(enableDebug && _debug_stream) {
			(*_debug_stream)
				<< " --- reassembly failed ack: " << this->ack
				<< " (getNextSeqCheck return 0)"
				<< " (" << __FILE__ << ":" << __LINE__ << ")"
				<< endl;
		}
		return(0);
	}
	map<uint32_t, TcpReassemblyStream_packet_var>::iterator iter_var;
	int _counter = 0;
	bool waitForPsh = this->_only_check_psh ? true : false;
	while(true) {
		u_int32_t seq = this->ok_packets.size() ? 
					this->ok_packets.back()[1] : 
					(forceFirstSeq ?
					  forceFirstSeq :
					  (crazySequence ? this->min_seq : 
					  (this->first_seq ? this->first_seq : this->min_seq)));
		iter_var = this->queuePacketVars.find(seq);
		if(!this->ok_packets.size() &&
		   iter_var == this->queuePacketVars.end() && seq && seq == this->first_seq &&
		   this->min_seq && this->min_seq != this->first_seq) {
			seq = this->min_seq;
			iter_var = this->queuePacketVars.find(seq);
		}
		if(link->reassembly->completeMod == 1) {
			if(iter_var != this->queuePacketVars.end() && iter_var->second.isFail()) {
				if(this->ok_packets.size()) {
					this->queuePacketVars[this->ok_packets.back()[0]].queuePackets[this->ok_packets.back()[1]].state = TcpReassemblyStream_packet::FAIL;
					if(enableDebug && _debug_stream) {
						(*_debug_stream)
							<< " --- remove last seq: " << this->ok_packets.back()[0] << " / " << this->ok_packets.back()[1]
							<< " (" << __FILE__ << ":" << __LINE__ << ")"
							<< endl;
					}
					this->ok_packets.pop_back();
				}
				if(!this->ok_packets.size()) {
					while(iter_var != this->queuePacketVars.end() && iter_var->second.isFail()) {
						++iter_var;
					}
					forceFirstSeq = iter_var->first;
				}
				continue;
			}
		} else {
			while(iter_var != this->queuePacketVars.end() && iter_var->second.isFail()) {
				++iter_var;
			}
		}
		if(iter_var == this->queuePacketVars.end() && this->ok_packets.size()) {
			u_int32_t prev_seq = this->ok_packets.back()[0];
			map<uint32_t, TcpReassemblyStream_packet_var>::iterator temp_iter;
			for(temp_iter = this->queuePacketVars.begin(); temp_iter != this->queuePacketVars.end(); temp_iter++) {
				if(temp_iter->first > prev_seq && temp_iter->first < seq) {
					iter_var = temp_iter;
					break;
				}
			}
		}
		if(iter_var == this->queuePacketVars.end()) {
			if(!this->ok_packets.size()) {
				if(_counter) {
					if(enableDebug && _debug_stream) {
						(*_debug_stream)
							<< " --- reassembly failed ack: " << this->ack
							<< " (unknown seq: " << seq << ")"
							<< " (" << __FILE__ << ":" << __LINE__ << ")"
							<< endl;
					} 
					return(0);
				} else {
					if(enableDebug && _debug_stream) {
						(*_debug_stream)
							<< " --- skip incorrect ack: " << this->ack
							<< " (unknown seq: " << seq << ")"
							<< " (" << __FILE__ << ":" << __LINE__ << ")"
							<< endl;
					} 
					return(1);
				}
			} else {
				this->queuePacketVars[this->ok_packets.back()[0]].queuePackets[this->ok_packets.back()[1]].state = TcpReassemblyStream_packet::FAIL;
				if(enableDebug && _debug_stream) {
					(*_debug_stream)
						<< " --- remove last seq: " << this->ok_packets.back()[0] << " / " << this->ok_packets.back()[1]
						<< " (" << __FILE__ << ":" << __LINE__ << ")"
						<< endl;
				}
				this->ok_packets.pop_back();
			}
		} else {
			u_int32_t next_seq = iter_var->second.getNextSeqCheck();
			if(next_seq) {
				this->ok_packets.push_back(d_u_int32_t(iter_var->first, next_seq));
				if(enableDebug && _debug_stream) {
					(*_debug_stream)
						<< " +++ add seq: " << iter_var->first << " / " << next_seq
						<< " (" << __FILE__ << ":" << __LINE__ << ")"
						<< endl;
				}
				if(enableValidateDataViaCheckData) {
					if(!this->completed_finally || 
					   link->reassembly->getType() == TcpReassembly::http) {
						this->saveCompleteData(true, prevHttpStream);
					}
					switch(link->reassembly->getType()) {
					case TcpReassembly::http:
						if(this->http_ok) {
							this->is_ok = true;
							this->detect_ok_max_next_seq = next_seq;
							return(1);
						} else {
							u_int32_t datalen = this->complete_data.getDatalen();
							this->clearCompleteData();
							if(this->http_content_length > 100000 ||
							   (!this->http_content_length && datalen > 100000)) {
								if(enableDebug && _debug_stream) {
									(*_debug_stream)
										<< " --- reassembly failed ack: " << this->ack
										<< " (maximum size of the data exceeded)"
										<< " (" << __FILE__ << ":" << __LINE__ << ")"
										<< endl;
								}
								return(0);
							}
						}
						break;
					default:
						if(link->reassembly->checkOkData(this->complete_data.getData(), this->complete_data.getDatalen(), 
										 link->reassembly->enableStrictValidateDataViaCheckData ? TcpReassemblySip::_chssm_strict : TcpReassemblySip::_chssm_na,
										 &link->sip_offsets)) {
							this->detect_ok_max_next_seq = next_seq;
							return(1);
						} else {
							this->clearCompleteData();
						}
						break;
					}
				}
				this->queuePacketVars[this->ok_packets.back()[0]].queuePackets[this->ok_packets.back()[1]].state = TcpReassemblyStream_packet::CHECK;
				if(waitForPsh ?
				    this->queuePacketVars[this->ok_packets.back()[0]].queuePackets[this->ok_packets.back()[1]].header_tcp.flags_bit.psh :
				    ((maxNextSeq && next_seq == maxNextSeq) ||
				     (!enableSimpleCmpMaxNextSeq && maxNextSeq && next_seq == maxNextSeq - 1) ||
				     (this->last_seq && next_seq == this->last_seq) ||
				     (this->last_seq && next_seq == this->last_seq - 1) ||
				     (enableSimpleCmpMaxNextSeq && next_seq == this->max_next_seq) ||
				     (!crazySequence && next_seq == this->max_next_seq && next_seq == this->getLastSeqFromNextStream()))) {
					if(!this->queuePacketVars[this->ok_packets.back()[0]].queuePackets[this->ok_packets.back()[1]].header_tcp.flags_bit.psh && 
					   !ignorePsh) {
						waitForPsh = true;
					} else {
						if(!waitForPsh && this->_force_wait_for_next_psh) {
							waitForPsh = true;
						} else {
							this->is_ok = true;
							if(!this->completed_finally || 
							   link->reassembly->getType() == TcpReassembly::http) {
								this->saveCompleteData();
							}
							if(!this->_force_wait_for_next_psh) {
								this->detect_ok_max_next_seq = next_seq;
							}
							if(needValidateDataViaCheckData) {
								if(!link->reassembly->checkOkData(this->complete_data.getData(), this->complete_data.getDatalen(), 
												  TcpReassemblySip::_chssm_na, &link->sip_offsets)) {
									this->is_ok = false;
									this->clearCompleteData();
									return(0);
								}
							}
							return(1);
						}
					}
				} else if(enableDebug && ENABLE_DEBUG(link->reassembly->getType(), _debug_check_ok_process)) {
					(*_debug_stream)
						<< " --- failed cmp seq"
						<< " (next_seq: " << next_seq << " !== " << ("last_seq / max_seq / max_next_seq") << " : " << this->last_seq << " / " << maxNextSeq << " / " << this->max_next_seq << ")"
						<< " (" << __FILE__ << ":" << __LINE__ << ")"
						<< endl;
				}
			} else if(this->ok_packets.size()) {
				this->queuePacketVars[this->ok_packets.back()[0]].queuePackets[this->ok_packets.back()[1]].state = TcpReassemblyStream_packet::FAIL;
				if(enableDebug && _debug_stream) {
					(*_debug_stream)
						<< " --- remove last seq: " << this->ok_packets.back()[0] << " / " << this->ok_packets.back()[1]
						<< " (" << __FILE__ << ":" << __LINE__ << ")"
						<< endl;
				}
				this->ok_packets.pop_back();
			} else {
				if(enableDebug && _debug_stream) {
					(*_debug_stream)
						<< " --- reassembly failed ack: " << this->ack
						<< " (unexpected last seq for required: " << this->last_seq << "/" << maxNextSeq << " last_seq/maxNextSeq)"
						<< " (" << __FILE__ << ":" << __LINE__ << ")"
						<< endl;
				}
				return(0);
			}
		}
		if(++_counter > 500) {
			break;
		}
	}
	if(enableDebug) {
		(*_debug_stream)
			<< " --- reassembly failed ack: " << this->ack << " "
			<< "(unknown error)"
			<< endl;
	}
	return(0);
}

bool TcpReassemblyStream::ok2_ec(u_int32_t nextAck, bool enableDebug) {
        map<uint32_t, TcpReassemblyStream*>::iterator iter;
	iter = this->link->queue_by_ack.find(nextAck);
	if(iter == this->link->queue_by_ack.end()) {
		return(false);
	}
	TcpReassemblyStream *nextStream = iter->second;
	
	/*
	if(this->ack == 766596997) {
		cout << "-- ***** --";
	}
	*/
 
	nextStream->_only_check_psh = true;
	if(!nextStream->ok(true, false, 0,
			   0, 0, 0,
			   NULL, enableDebug)) {
		return(false);
	}
	this->_force_wait_for_next_psh = true;
	if(!this->ok(true, false, this->detect_ok_max_next_seq,
		     0, 0, 0,
		     NULL, enableDebug)) {
		nextStream->is_ok = false;
		nextStream->clearCompleteData();
		return(false);
	}
	if(this->checkOkPost(nextStream)) {
		this->http_ok_expect_continue_post = true;
		nextStream->http_ok_expect_continue_data = true;
		return(true);
	} else {
		nextStream->is_ok = false;
		nextStream->clearCompleteData();
		this->is_ok = false;
		this->clearCompleteData();
		return(false);
	}
	return(false);
}

u_char *TcpReassemblyStream::complete(u_int32_t *datalen, deque<s_index_item> *data_index, timeval *time, u_int32_t *seq, bool check,
				      size_t startIndex, size_t *endIndex, bool breakIfPsh) {
	if(!check && !this->is_ok) {
		*datalen = 0;
		return(NULL);
	}
	u_char *data = NULL;
	*datalen = 0;
	if(seq) {
		*seq = 0;
	}
	time->tv_sec = 0;
	time->tv_usec = 0;
	u_int32_t databuff_len = 0;
	u_int32_t lastNextSeq = 0;
	size_t i;
	for(i = startIndex; i < this->ok_packets.size(); i++) {
		TcpReassemblyStream_packet packet = this->queuePacketVars[this->ok_packets[i][0]].queuePackets[this->ok_packets[i][1]];
		if(PACKET_DATALEN(packet.datalen, packet.datacaplen) && 
		   !(link->reassembly->ignoreZeroData && packet.data[0] == 0)) {
			if(seq && !*seq) {
				*seq = packet.header_tcp.seq;
			}
			if(lastNextSeq > this->ok_packets[i][0] && lastNextSeq < this->ok_packets[i][1] &&
			   *datalen >= (lastNextSeq - this->ok_packets[i][0])) {
				int diff = lastNextSeq - this->ok_packets[i][0];
				*datalen -= diff;
				if(data_index) {
					for(int i = data_index->size() - 1; i >= 0 && diff > 0; i--) {
						if((int)(*data_index)[i].len < diff) {
							diff -= (*data_index)[i].len;
							(*data_index)[i].len = 0;
						} else {
							(*data_index)[i].len -= diff;
							diff = 0;
						}
					}
				}
			}
			if(!time->tv_sec) {
				*time = packet.time;
			}
			u_int32_t packet_len = PACKET_DATALEN(packet.datalen, packet.datacaplen);
			u_int32_t packet_datalen = min(packet_len, packet.datacaplen);
			u_int32_t packet_offset = 0;
			if(link->reassembly->getType() == TcpReassembly::sip) {
				if(i == startIndex) {
					if(this->queuePacketVars[this->ok_packets[i][0]].offset > 0 &&
					   this->queuePacketVars[this->ok_packets[i][0]].offset < packet_datalen) {
						packet_offset = this->queuePacketVars[this->ok_packets[i][0]].offset;
					}
					/* blocked - failed when connecting prevStreams if the following starts with characters \r or \n
					while(packet_offset < packet_datalen &&
					      (packet.data[packet_offset] == '\r' || packet.data[packet_offset] == '\n')) {
						++packet_offset;
					}
					*/
					if(packet_offset) {
						packet_len -= packet_offset;
						packet_datalen -= packet_offset;
					}
				}
			}
			if(packet_len > 0) {
				if(!data) {
					databuff_len = max(packet_len + 1, 10000u);
					data = new FILE_LINE(36001) u_char[databuff_len];
				} else if(databuff_len < *datalen + packet_len + 1) {
					databuff_len += max(packet_len + 1, 10000u);
					u_char* newdata = new FILE_LINE(36002) u_char[databuff_len];
					memcpy_heapsafe(newdata, data, *datalen, 
							__FILE__, __LINE__);
					delete [] data;
					data = newdata;
				}
				if(packet_datalen > 0) {
					/*
					if(link->reassembly->getType() == TcpReassembly::sip && packet.data[0] == 0)
					memset_heapsafe(data + *datalen, data, 
							'_',
							packet_datalen,
							__FILE__, __LINE__);
					else 
					*/
					memcpy_heapsafe(data + *datalen, data, 
							packet.data + packet_offset, packet.data, 
							packet_datalen,
							__FILE__, __LINE__);
				}
				if(packet_datalen < packet_len) {
					memset_heapsafe(data + *datalen + packet_datalen, data, 
							' ', 
							packet_len - packet_datalen, 
							__FILE__, __LINE__);
				}
				if(data_index) {
					data_index->push_back(s_index_item(this->ok_packets[i][0], this->ok_packets[i][1], packet_len));
				}
				*datalen += packet_len;
			}
			lastNextSeq = this->ok_packets[i][1];
		}
		bool _break = false;
		switch(link->reassembly->getType()) {
		case TcpReassembly::http:
			if(breakIfPsh && packet.header_tcp.flags_bit.psh) {
				_break = true;
			}
			break;
		default:
			if(breakIfPsh && packet.header_tcp.flags_bit.psh &&
			   link->reassembly->checkOkData(data, *datalen, TcpReassemblySip::_chssm_na, &link->sip_offsets)) {
				// TODO: remove next items from ok_packets ?
				_break = true;
			}
			break;
		}
		if(_break) {
			break;
		}
	}
	if(endIndex) {
		*endIndex = i;
	}
	if(*datalen) {
		data[*datalen] = 0;
	}
	return(data);
}

bool TcpReassemblyStream::saveCompleteData(bool check, TcpReassemblyStream *prevHttpStream) {
	if(this->is_ok || check) {
		if(this->complete_data.isFill()) {
			return(true);
		} else {
			u_char *data;
			u_int32_t datalen;
			u_int32_t seq;
			timeval time;
			switch(this->link->reassembly->getType()) {
			case TcpReassembly::http:
				data = this->complete(&datalen, NULL, &time, NULL, check);
				if(data) {
					this->complete_data.setDataTime(data, datalen, time, false);
					if(datalen > 5 && !memcmp(data, "POST ", 5)) {
						this->http_type = HTTP_TYPE_POST;
					} else if(datalen > 4 && !memcmp(data, "GET ", 4)) {
						this->http_type = HTTP_TYPE_GET;
					} else if(datalen > 5 && !memcmp(data, "HEAD ", 5)) {
						this->http_type = HTTP_TYPE_HEAD;
					} else if(datalen > 4 && !memcmp(data, "HTTP", 4)) {
						this->http_type = HTTP_TYPE_HTTP;
					}
					this->http_header_length = 0;
					this->http_content_length = 0;
					this->http_ok = false;
					this->http_ok_data_complete = false;
					this->http_expect_continue = false;
					if(this->http_type) {
						char *pointToContentLength = strcasestr((char*)data, "Content-Length:");
						if(pointToContentLength) {
							this->http_content_length = atol(pointToContentLength + 15);
						}
						char *pointToEndHeader = strstr((char*)data, "\r\n\r\n");
						if(pointToEndHeader) {
							this->http_header_length = (u_char*)pointToEndHeader - data;
							if(this->http_content_length) {
								if(!this->_ignore_expect_continue &&
								   strcasestr((char*)data, "Expect: 100-continue")) {
									if(((u_char*)pointToEndHeader - data) + 4 == datalen) {
										this->http_ok = true;
									} else if(((u_char*)pointToEndHeader - data) + 4 + http_content_length == datalen) {
										this->http_ok = true;
										this->http_ok_data_complete = true;
									}
									this->http_expect_continue = true;
								} else if(((u_char*)pointToEndHeader - data) + 4 + http_content_length == datalen) {
									this->http_ok = true;
								}
							} else {
								if(((u_char*)pointToEndHeader - data) + 4 == datalen) {
									this->http_ok = true;
								}
							}
						}
					} else if(prevHttpStream && prevHttpStream->http_type == HTTP_TYPE_POST && prevHttpStream->http_expect_continue) {
						if(datalen == prevHttpStream->http_content_length) {
							this->http_ok = true;
							this->http_ok_data_complete = true;
						}
					}
					return(true);
				}
				break;
			case TcpReassembly::webrtc:
			case TcpReassembly::ssl:
			case TcpReassembly::sip:
			case TcpReassembly::diameter:
				{
				deque<s_index_item> data_index;
				data = this->complete(&datalen, &data_index, &time, &seq, check);
				if(data) {
					this->complete_data.setDataTime(data, datalen, time, false);
					this->complete_data.setSeq(seq);
					this->complete_data_index = data_index;
					return(true);
				}
				}
				break;
			}
		}
	}
	return(false);
}

bool TcpReassemblyStream::isSetCompleteData() {
	return(this->complete_data.getData() != NULL);
}

void TcpReassemblyStream::clearCompleteData() {
	this->complete_data.clearData();
	this->complete_data_index.clear();
}

void TcpReassemblyStream::cleanupCompleteData() {
	if(ok_packets.size()) {
		for(unsigned i = 0; i < ok_packets.size(); i++) {
			map<uint32_t, TcpReassemblyStream_packet_var>::iterator iter = queuePacketVars.find(ok_packets[i].val[0]);
			if(iter != queuePacketVars.end() &&
			   (i < ok_packets.size() - 1 || !iter->second.offset)) {
				queuePacketVars.erase(iter);
			}
		}
		for(map<uint32_t, TcpReassemblyStream_packet_var>::iterator iter = queuePacketVars.begin(); iter != queuePacketVars.end(); iter++) {
			iter->second.cleanState();
		}
		if(queuePacketVars.size()) {
			min_seq = queuePacketVars.begin()->first;
			max_next_seq = 0;
			bool max_next_seq_set = false;
			for(map<uint32_t, TcpReassemblyStream_packet_var>::iterator iter = queuePacketVars.begin(); iter != queuePacketVars.end(); iter++) {
				u_int32_t _max_next_seq = iter->second.getMaxNextSeq();
				if(!max_next_seq_set ||
				   TCP_SEQ_CMP(_max_next_seq, max_next_seq) > 0) {
					max_next_seq = _max_next_seq;
					max_next_seq_set = true;
				}
			}
		}
		ok_packets.clear();
		is_ok = false;
	}
	clearCompleteData();
}

void TcpReassemblyStream::confirmCompleteData(u_int32_t datalen_confirmed, u_int32_t *last_seq) {
	if(datalen_confirmed &&
	   datalen_confirmed < complete_data.getDatalen()) {
		u_int32_t drop_packets_length = 0;
		while(complete_data_index.size() &&
		      complete_data.getDatalen() - drop_packets_length > complete_data_index.back().len &&
		      datalen_confirmed <= complete_data.getDatalen() - drop_packets_length - complete_data_index.back().len) {
			s_index_item *last_index_item = &complete_data_index.back();
			for(int i = ok_packets.size() - 1; i >= 0 ; i--) {
				if(ok_packets[i][0] == last_index_item->seq && ok_packets[i][1] == last_index_item->next_seq) {
					ok_packets.erase(ok_packets.begin() + i);
					break;
				}
			}
			drop_packets_length += last_index_item->len;
			complete_data_index.pop_back();
		}
		if(complete_data_index.size()) {
			if(datalen_confirmed < complete_data.getDatalen() - drop_packets_length &&
			   complete_data.getDatalen() - drop_packets_length - datalen_confirmed < complete_data_index.back().len &&
			   queuePacketVars.find(complete_data_index.back().seq) != queuePacketVars.end()) {
				queuePacketVars[complete_data_index.back().seq].offset = complete_data_index.back().len - (complete_data.getDatalen() - drop_packets_length - datalen_confirmed);
				if(last_seq) {
					*last_seq = complete_data_index.back().next_seq - (complete_data.getDatalen() - drop_packets_length - datalen_confirmed);
				}
			} else {
				if(last_seq) {
					*last_seq = complete_data_index.back().next_seq;
				}
			}
		}
	}
}

void TcpReassemblyStream::printContent(int level) {
	std::ostream *__debug_stream = _debug_stream ? _debug_stream : &cout;
	map<uint32_t, TcpReassemblyStream_packet_var>::iterator iter;
	int counter = 0;
	for(iter = this->queuePacketVars.begin(); iter != this->queuePacketVars.end(); iter++) {
		(*__debug_stream)
			<< fixed 
			<< setw(level * 5) << ""
			<< setw(3) << (++counter) << "   " 
			<< "ack: " << iter->first
			<< " items: " << iter->second.queuePackets.size()
			<< endl;
	}
}

bool TcpReassemblyStream::checkOkPost(TcpReassemblyStream *nextStream) {
	if(!this->complete_data.getData()) {
		return(false);
	}
	bool rslt = false;
	u_int32_t datalen = this->complete_data.getDatalen();
	bool useNextStream = false;
	if(nextStream && nextStream->complete_data.getData()) {
		datalen += nextStream->complete_data.getDatalen();
		useNextStream = true;
	}
	char *data = new FILE_LINE(36003) char[datalen + 1];
	memcpy_heapsafe(data, this->complete_data.getData(), this->complete_data.getDatalen(), 
			__FILE__, __LINE__);
	if(useNextStream) {
		memcpy_heapsafe(data + this->complete_data.getDatalen(), data, 
				nextStream->complete_data.getData(), nextStream->complete_data.getData(), 
				nextStream->complete_data.getDatalen(),
				__FILE__, __LINE__);
	}
	data[datalen] = 0;
	if(datalen > 5 && !memcmp(data, "POST ", 5)) {
		this->http_type = HTTP_TYPE_POST;
		char *pointToContentLength = strcasestr((char*)data, "Content-Length:");
		this->http_content_length = pointToContentLength ? atol(pointToContentLength + 15) : 0;
		char *pointToEndHeader = strstr((char*)data, "\r\n\r\n");
		if(pointToEndHeader &&
		   (pointToEndHeader - data) + 4 + this->http_content_length == datalen) {
			this->http_ok = true;
			rslt = true;
		}
	}
	delete [] data;
	return(rslt);
	
}

/*
bool TcpReassemblyStream::checkCompleteContent() {
	if(!this->complete_data) {
		return(false);
	}
	u_char *data = this->complete_data->data;
	u_int32_t datalen = this->complete_data->datalen;
	bool http = (datalen > 5 && !memcmp(data, "POST ", 5)) ||
		    (datalen > 4 && !memcmp(data, "GET ", 4)) ||
		    (datalen > 5 && !memcmp(data, "HEAD ", 5));
	if(http) {
		if(!memcmp(data + datalen - 4, "\r\n\r\n", 4)) {
			return(true);
		}
	}
	return(false);
}

bool TcpReassemblyStream::checkContentIsHttpRequest() {
	if(!this->complete_data) {
		return(false);
	}
	u_char *data = this->complete_data->data;
	u_int32_t datalen = this->complete_data->datalen;
	bool http = (datalen > 5 && !memcmp(data, "POST ", 5)) ||
		    (datalen > 4 && !memcmp(data, "GET ", 4)) ||
		    (datalen > 5 && !memcmp(data, "HEAD ", 5));
	if(http) {
		return(true);
	}
	return(false);
}
*/

u_int32_t TcpReassemblyStream::getLastSeqFromNextStream() {
	TcpReassemblyStream *stream = this->link->findStreamBySeq(this->ack);
	if(stream) {
		return(stream->ack);
	}
	return(0);
}


bool TcpReassemblyLink::streamIterator::init() {
	this->stream = NULL;
	this->state = STATE_NA;
	if(this->findSynSent()) {
		return(true);
	}
	return(this->findFirstDataToDest());
}

bool TcpReassemblyLink::streamIterator::next() {
	map<uint32_t, TcpReassemblyStream*>::iterator iter;
	TcpReassemblyStream *stream;
	switch(this->state) {
	case STATE_SYN_SENT:
		iter = link->queue_flags_by_ack.find(this->stream->min_seq + 1);
		if(iter != link->queue_flags_by_ack.end()) {
			this->stream = iter->second;
			this->state = STATE_SYN_RECV;
			return(true);
		} else {
			return(this->findFirstDataToDest());
		}
		break;
	case STATE_SYN_RECV:
		iter = link->queue_by_ack.find(this->stream->min_seq + 1);
		if(iter != link->queue_by_ack.end()) {
			this->stream = iter->second;
			this->state = STATE_SYN_OK;
			return(true);
		} else {
			return(this->findFirstDataToDest());
		}
		break;
	case STATE_SYN_OK:
	case STATE_SYN_FORCE_OK:
		stream = link->findStreamByMinSeq(this->stream->ack);
		if(stream &&
		   stream->ack != this->stream->min_seq) {
			this->stream = stream;
			this->state = STATE_SYN_OK;
			return(true);
		}
		break;
	default:
		break;
	}
	return(false);
}

bool TcpReassemblyLink::streamIterator::nextAckInDirection() {
	map<uint32_t, TcpReassemblyStream*>::iterator iter;
	for(iter = link->queue_by_ack.begin(); iter != link->queue_by_ack.end(); iter++) {
		if(iter->second->direction == this->stream->direction &&
		   iter->second->ack > this->stream->ack) {
			this->stream = iter->second;
			return(true);
		}
	}
	return(false);
}

bool TcpReassemblyLink::streamIterator::nextAckInReverseDirection() {
	map<uint32_t, TcpReassemblyStream*>::iterator iter;
	for(iter = link->queue_by_ack.begin(); iter != link->queue_by_ack.end(); iter++) {
		if(iter->second->direction != this->stream->direction &&
		   iter->second->ack > this->stream->max_next_seq) {
			this->stream = iter->second;
			return(true);
		}
	}
	return(false);
}

bool TcpReassemblyLink::streamIterator::nextSeqInDirection() {
	TcpReassemblyStream *stream = this->link->findStreamByMinSeq(this->stream->max_next_seq, true);
	if(stream && 
	   stream->direction == this->stream->direction) {
		this->stream = stream;
		return(true);
	}
	return(false);
}

bool TcpReassemblyLink::streamIterator::nextAckByMaxSeqInReverseDirection() {
	map<uint32_t, TcpReassemblyStream*>::iterator iter = link->queue_by_ack.find(this->stream->max_next_seq);
	if(iter != link->queue_by_ack.end()) {
		TcpReassemblyStream *stream = iter->second;
		if(stream->direction != this->stream->direction) {
			this->stream = stream;
			return(true);
		}
	}
	return(false);
}

void TcpReassemblyLink::streamIterator::print() {
	std::ostream *__debug_stream = _debug_stream ? _debug_stream : &cout;
	(*__debug_stream)
		<< "iterator " 
		<< this->link->ip_src.getString() << " / " << this->link->port_src << " -> "
		<< this->link->ip_dst.getString() << " / " << this->link->port_dst << " ";
	if(this->stream) {
		(*__debug_stream)
			<< "  ack: " << this->stream->ack
			<< "  state: " << this->state;
	} else {
		(*__debug_stream) << " - no stream";
	}
}

u_int32_t TcpReassemblyLink::streamIterator::getMaxNextSeq() {
	TcpReassemblyStream *stream = link->findStreamByMinSeq(this->stream->ack);
	if(stream) {
		return(stream->ack);
	}
	stream = link->findStreamByMinSeq(this->stream->max_next_seq, true, this->stream->ack, this->stream->direction);
	if(stream) {
		return(stream->min_seq);
	}
	stream = link->findFlagStreamByAck(this->stream->ack);
	if(stream) {
		return(stream->min_seq);
	}
	/* disabled for crazy
	stream = link->findFinalFlagStreamByAck(this->stream->max_next_seq, 
						this->stream->direction == TcpReassemblyStream::DIRECTION_TO_DEST ? 
							TcpReassemblyStream::DIRECTION_TO_SOURCE : 
							TcpReassemblyStream::DIRECTION_TO_DEST);
	if(stream) {
		return(this->stream->max_next_seq);
	}
	stream = link->findFinalFlagStreamBySeq(this->stream->min_seq, this->stream->direction);
	if(stream) {
		return(stream->min_seq);
	}
	*/
	return(0);
}

bool TcpReassemblyLink::streamIterator::findSynSent() {
	map<uint32_t, TcpReassemblyStream*>::iterator iter;
	for(iter = link->queue_flags_by_ack.begin(); iter != link->queue_flags_by_ack.end(); iter++) {
		if(iter->second->type == TcpReassemblyStream::TYPE_SYN_SENT) {
			this->stream = iter->second;
			this->state = STATE_SYN_SENT;
			return(true);
		}
	}
	return(false);
}

bool TcpReassemblyLink::streamIterator::findFirstDataToDest() {
	map<uint32_t, TcpReassemblyStream*>::iterator iter;
	for(iter = link->queue_by_ack.begin(); iter != link->queue_by_ack.end(); iter++) {
		if(iter->second->direction == TcpReassemblyStream::DIRECTION_TO_DEST &&
		   iter->second->type == TcpReassemblyStream::TYPE_DATA) {
			this->stream = iter->second;
			this->state = STATE_SYN_FORCE_OK;
			return(true);
		}
	}
	return(false);
}


#ifdef HAVE_LIBGNUTLS
extern void end_decrypt_ssl(unsigned int saddr, unsigned int daddr, int sport, int dport);
#endif


TcpReassemblyLink::~TcpReassemblyLink() {
	while(this->queueStreams.size()) {
		TcpReassemblyStream *stream = this->queueStreams.front();
		this->queueStreams.pop_front();
		this->queue_by_ack.erase(stream->ack);
		if(ENABLE_DEBUG(reassembly->getType(), _debug_packet)) {
			(*_debug_stream) << " destroy (" << stream->ack << ")" << endl;
		}
		delete stream;
	}
	map<uint32_t, TcpReassemblyStream*>::iterator iter;
	for(iter = this->queue_by_ack.begin(); iter != this->queue_by_ack.end(); ) {
		delete iter->second;
		this->queue_by_ack.erase(iter++);
	}
	for(iter = this->queue_flags_by_ack.begin(); iter != this->queue_flags_by_ack.end(); ) {
		delete iter->second;
		this->queue_flags_by_ack.erase(iter++);
	}
	for(iter = this->queue_nul_by_ack.begin(); iter != this->queue_nul_by_ack.end(); ) {
		delete iter->second;
		this->queue_nul_by_ack.erase(iter++);
	}
	if(this->ethHeader) {
		delete [] this->ethHeader;
	}
	clearRemainData(TcpReassemblyDataItem::DIRECTION_NA);
	if(reassembly->getType() == TcpReassembly::ssl) {
		if(opt_enable_ssl == 10) {
			#if defined(HAVE_LIBGNUTLS) and defined(HAVE_SSL_WS)
			end_decrypt_ssl(htonl(ip_src), htonl(ip_dst), port_src, port_dst);
			#endif
		} else {
			end_decrypt_ssl_dssl(ip_src, ip_dst, port_src, port_dst);
		}
	}
	if(this->check_duplicity_seq) {
		delete [] this->check_duplicity_seq;
	}
}

bool TcpReassemblyLink::push_normal(
			TcpReassemblyStream::eDirection direction,
			timeval time, tcphdr2 header_tcp, 
			u_char *data, u_int32_t datalen, u_int32_t datacaplen,
			pcap_block_store *block_store, int block_store_index,
			bool isSip) {
	if(reassembly->simpleByAck) {
		if(!datalen) {
			this->last_packet_at_from_header = getTimeMS(&time);
			return(false);
		}
		if(reassembly->skipZeroData && data[0] == 0) {
			bool zeroData = true;
			for(unsigned i = 1; i < min(4u, datalen); i++) {
				if(data[i]) {
					zeroData = false;
					break;
				}
			}
			if(zeroData) {
				this->last_packet_at_from_header = getTimeMS(&time);
				return(false);
			}
		}
	}
	bool rslt = false;
	switch(this->state) {
	case STATE_NA:
		if(direction == TcpReassemblyStream::DIRECTION_TO_DEST &&
		   header_tcp.flags_bit.syn && !header_tcp.flags_bit.ack) {
			this->first_seq_to_dest = header_tcp.seq + 1;
			this->state = STATE_SYN_SENT;
			rslt = true;
		}
		break;
	case STATE_SYN_SENT:
		if(direction == TcpReassemblyStream::DIRECTION_TO_SOURCE &&
		   header_tcp.flags_bit.syn && header_tcp.flags_bit.ack) {
			this->first_seq_to_source = header_tcp.seq + 1;
			this->state = STATE_SYN_RECV;
			rslt = true;
		}
		break;
	case STATE_SYN_RECV:
		if(direction == TcpReassemblyStream::DIRECTION_TO_DEST &&
		   !header_tcp.flags_bit.syn && header_tcp.flags_bit.ack) {
			this->state = STATE_SYN_OK;
			rslt = true;
		}
		break;
	case STATE_SYN_OK:
	case STATE_SYN_FORCE_OK:
		if(header_tcp.flags_bit.rst) {
			this->rst = true;
			this->state = STATE_RESET;
			if(reassembly->getType() == TcpReassembly::ssl) {
				extern bool opt_ssl_destroy_ssl_session_on_rst;
				if(opt_ssl_destroy_ssl_session_on_rst) {
					if(opt_enable_ssl == 10) {
						#if defined(HAVE_LIBGNUTLS) and defined(HAVE_SSL_WS)
						end_decrypt_ssl(htonl(ip_src), htonl(ip_dst), port_src, port_dst);
						#endif
					} else {
						end_decrypt_ssl_dssl(ip_src, ip_dst, port_src, port_dst);
					}
				}
			}
			rslt = true;
		}
	case STATE_RESET:
		if(header_tcp.flags_bit.fin) {
			if(direction == TcpReassemblyStream::DIRECTION_TO_DEST) {
				this->fin_to_dest = true;
				this->setLastSeq(TcpReassemblyStream::DIRECTION_TO_SOURCE, 
						 header_tcp.ack_seq);
			} else {
				this->fin_to_source = true;
				if(!datalen) {
					this->setLastSeq(TcpReassemblyStream::DIRECTION_TO_SOURCE, 
							 header_tcp.seq);
				} else {
					this->setLastSeq(TcpReassemblyStream::DIRECTION_TO_DEST, 
							 header_tcp.ack_seq);
				}
			}
			if(this->fin_to_dest && this->fin_to_source) {
				this->state = STATE_CLOSE;
			}
			rslt = true;
		}
		break;
	case STATE_CLOSE:
	case STATE_CLOSED:
		if(this->rst && header_tcp.flags_bit.fin &&
		   direction == TcpReassemblyStream::DIRECTION_TO_SOURCE) {
			this->setLastSeq(TcpReassemblyStream::DIRECTION_TO_SOURCE, 
					 header_tcp.seq);
		}
		rslt = true;
		break;
	case STATE_CRAZY:
		return(false);
	}
	bool runCompleteAfterZerodataAck = false;
	if(state == STATE_SYN_OK || 
	   state == STATE_SYN_FORCE_OK ||
	   (state >= STATE_RESET &&
	    !header_tcp.flags_bit.fin && !header_tcp.flags_bit.rst) || 
	   reassembly->ignoreTcpHandshake) {
		if(datalen > 0) {
			if(!this->queueStreams.size() && (data[0] == '\n' || data[0] == '\r' || data[0] == 0)) {
				bool ok = false;
				for(unsigned i = 0; i < datalen; i++) {
					if(!(data[i] == '\n' || data[i] == '\r' || data[i] == 0)) {
						ok = true;
						break;
					}
				}
				if(!ok) {
					return(false);
				}
			}
			TcpReassemblyStream_packet packet;
			packet.setData(time, header_tcp,
				       data, datalen, datacaplen,
				       block_store, block_store_index,
				       isSip);
			this->pushpacket(direction, packet, isSip);
			if(!reassembly->simpleByAck) {
				this->setLastSeq(direction == TcpReassemblyStream::DIRECTION_TO_DEST ?
							TcpReassemblyStream::DIRECTION_TO_DEST :
							TcpReassemblyStream::DIRECTION_TO_SOURCE, 
						 header_tcp.seq);
			}
		} else {
			TcpReassemblyStream *prevStreamByLastAck = NULL;
			if(this->queue_by_ack.find(this->last_ack) != this->queue_by_ack.end()) {
				prevStreamByLastAck = this->queue_by_ack[this->last_ack];
			}
			if(this->last_ack && header_tcp.ack_seq != this->last_ack) {
				if(prevStreamByLastAck && !prevStreamByLastAck->last_seq &&
				   prevStreamByLastAck->direction == direction) {
					prevStreamByLastAck->last_seq = header_tcp.seq;
				}
			}
			if(reassembly->enableAllCompleteAfterZerodataAck) {
				if(!header_tcp.flags_bit.psh && header_tcp.flags_bit.ack) {
					this->setLastSeq(direction == TcpReassemblyStream::DIRECTION_TO_DEST ?
								TcpReassemblyStream::DIRECTION_TO_SOURCE :
								TcpReassemblyStream::DIRECTION_TO_DEST, 
							 header_tcp.ack_seq);
					this->setLastSeq(direction == TcpReassemblyStream::DIRECTION_TO_DEST ?
								TcpReassemblyStream::DIRECTION_TO_DEST :
								TcpReassemblyStream::DIRECTION_TO_SOURCE, 
							 header_tcp.seq);
					runCompleteAfterZerodataAck = true;
				} else if(prevStreamByLastAck &&
					  prevStreamByLastAck->direction != direction &&
					  header_tcp.flags_bit.ack) {
					runCompleteAfterZerodataAck = true;
				}
			}
			this->last_packet_at_from_header = getTimeMS(&time);
		}
		rslt = true;
	} else {
		this->last_packet_at_from_header = getTimeMS(&time);
	}
	if(!reassembly->enableCleanupThread) {
		bool final = !reassembly->simpleByAck &&
			     (this->state == STATE_RESET || this->state == STATE_CLOSE);
		if(this->queueStreams.size() &&
		   !(reassembly->getType() == TcpReassembly::sip && !exists_sip)) {
			if(ENABLE_DEBUG(reassembly->getType(), _debug_check_ok)) {
				(*_debug_stream) << " -- TRY ack " << header_tcp.ack_seq << endl;
			}
			int countDataStream = this->okQueue(final || runCompleteAfterZerodataAck ? 2 : 1,
							    header_tcp.seq, header_tcp.seq + datalen,
							    reassembly->simpleByAck ? header_tcp.ack_seq : 0,
							    header_tcp.flags_bit.psh,
							    ENABLE_DEBUG(reassembly->type, _debug_check_ok));
			if(ENABLE_DEBUG(reassembly->getType(), _debug_rslt)) {
				(*_debug_stream) << " -- RSLT: ";
				if(countDataStream == 0) {
					if(!this->queueStreams.size()) {
						(*_debug_stream) << "EMPTY";
					} else {
						(*_debug_stream) << "ERROR ";
						if(this->rst) {
							(*_debug_stream) << " - RST";
						}
					}
				} else if(countDataStream < 0) {
					(*_debug_stream) << "empty";
				} else {
					(*_debug_stream) << "OK (" << countDataStream << ")";
				}
				(*_debug_stream) << " " << this->port_src << " / " << this->port_dst;
				(*_debug_stream) << endl;
			}
			extern cBuffersControl buffersControl;
			if(countDataStream > 0) {
				this->complete(final || runCompleteAfterZerodataAck, true);
				if(final) {
					this->state = STATE_CLOSED;
				}
			} else if(reassembly->enableAutoCleanup &&
				  reassembly->extCleanupStreamsLimitStreams &&
				  reassembly->extCleanupStreamsLimitHeap &&
				  this->queueStreams.size() > reassembly->extCleanupStreamsLimitStreams &&
				  buffersControl.getPerc_pb_used() > reassembly->extCleanupStreamsLimitHeap) {
				this->extCleanup(2, true);
			}
		}
	}
	return(rslt);
}

bool TcpReassemblyLink::push_crazy(
			TcpReassemblyStream::eDirection direction,
			timeval time, tcphdr2 header_tcp, 
			u_char *data, u_int32_t datalen, u_int32_t datacaplen,
			pcap_block_store *block_store, int block_store_index,
			bool isSip) {
	/*if(!(datalen > 0 ||
	     header_tcp.flags_bit.syn || header_tcp.flags_bit.fin || header_tcp.flags_bit.rst)) {
		return(false);
	}*/
	direction = header_tcp.get_dest() == this->port_dst ?
		     TcpReassemblyStream::DIRECTION_TO_DEST :
		     TcpReassemblyStream::DIRECTION_TO_SOURCE;
	if(this->direction_confirm < 2) {
		TcpReassemblyStream::eDirection checked_direction = direction;
		if(this->direction_confirm < 2 && header_tcp.flags_bit.syn) {
			if(header_tcp.flags_bit.ack) {
				checked_direction = TcpReassemblyStream::DIRECTION_TO_SOURCE;
			} else {
				checked_direction = TcpReassemblyStream::DIRECTION_TO_DEST;
			}
			this->direction_confirm = 2;
		}
		if(!this->direction_confirm &&
		   ((datalen > 5 && !memcmp(data, "POST ", 5)) ||
		    (datalen > 4 && !memcmp(data, "GET ", 4)) ||
		    (datalen > 5 && !memcmp(data, "HEAD ", 5)))) {
			checked_direction = TcpReassemblyStream::DIRECTION_TO_DEST;
			this->direction_confirm = 1;
		}
		if(checked_direction != direction) {
			direction = checked_direction;
			this->switchDirection();
		}
	}
	TcpReassemblyStream_packet packet;
	packet.setData(time, header_tcp,
		       data, datalen, datacaplen,
		       block_store, block_store_index,
		       isSip);
	TcpReassemblyStream *stream;
	map<uint32_t, TcpReassemblyStream*>::iterator iter;
	for(int i = 0; i < 3; i++) {
		if(i == 0 ? datalen > 0 : 
		   i == 1 ? header_tcp.flags_bit.syn || header_tcp.flags_bit.fin || header_tcp.flags_bit.rst : 
			    datalen == 0 && !(header_tcp.flags_bit.syn || header_tcp.flags_bit.fin || header_tcp.flags_bit.rst)) {
			map<uint32_t, TcpReassemblyStream*> *queue = i == 0 ? &this->queue_by_ack : 
								     i == 1 ? &this->queue_flags_by_ack :
									      &this->queue_nul_by_ack;
			iter = queue->find(packet.header_tcp.ack_seq);
			if(iter == queue->end()) {
				stream = new FILE_LINE(36004) TcpReassemblyStream(this);
				stream->direction = direction;
				stream->ack = packet.header_tcp.ack_seq;
				if(i == 1) {
					stream->type = header_tcp.flags_bit.syn ? 
								(header_tcp.flags_bit.ack ? 
									TcpReassemblyStream::TYPE_SYN_RECV :
									TcpReassemblyStream::TYPE_SYN_SENT) :
						       header_tcp.flags_bit.fin ? 
								TcpReassemblyStream::TYPE_FIN :
								TcpReassemblyStream::TYPE_RST;
				}
				(*queue)[stream->ack] = stream;
				if(header_tcp.flags_bit.rst) {
					this->rst = true;
				}
				if(header_tcp.flags_bit.fin) {
					if(direction == TcpReassemblyStream::DIRECTION_TO_DEST) {
						this->fin_to_dest = true;
					} else {
						this->fin_to_source = true;
					}
				}
			} else {
				stream = iter->second;
			}
			stream->push(packet);
			if(!stream->min_seq ||
			   TCP_SEQ_CMP(packet.header_tcp.seq, stream->min_seq) < 0) {
				stream->min_seq = packet.header_tcp.seq;
			}
			if(!stream->max_next_seq ||
			   TCP_SEQ_CMP(packet.next_seq, stream->max_next_seq) > 0) {
				stream->max_next_seq = packet.next_seq;
			}
		}
	}
	//this->last_packet_at = getTimeMS();
	this->last_packet_at_from_header = getTimeMS(&time);
	if(!this->created_at_from_header) {
		this->created_at_from_header = this->last_packet_at_from_header;
	}
	if(!reassembly->enableCleanupThread &&
	   (this->rst || this->fin_to_dest || this->fin_to_source) &&
	   !this->link_is_ok) {
		bool _debug_output = false;
		if(this->exists_data) {
			int countDataStream = this->okQueue(false, 0, 0, 0, false, ENABLE_DEBUG(reassembly->getType(), _debug_check_ok));
			if(countDataStream > 1) {
				this->complete(false, true);
				if(ENABLE_DEBUG(reassembly->getType(), _debug_rslt)) {
					(*_debug_stream) << "RSLT: OK (" << countDataStream << ")";
					_debug_output = true;
				}
				this->link_is_ok = 1;
				// - 1 - prošlo tímto
				// - 2 - není už co k vyřízení - zatím se nastavuje jen po complete all
			}
		}
		if(_debug_output) {
			if(ENABLE_DEBUG(reassembly->getType(), _debug_packet)) {
				(*_debug_stream) 
					<< " / "
					<< this->ip_src.getString() << " / " << this->port_src
					<< " -> "
					<< this->ip_dst.getString() << " / " << this->port_dst;
			}
			(*_debug_stream) << endl;
		}
	}
	return(true);
}

void TcpReassemblyLink::pushpacket(TcpReassemblyStream::eDirection direction,
				   TcpReassemblyStream_packet packet,
				   bool isSip) {
	TcpReassemblyStream *stream;
	map<uint32_t, TcpReassemblyStream*>::iterator iter;
	iter = this->queue_by_ack.find(packet.header_tcp.ack_seq);
	if(iter == this->queue_by_ack.end() || !iter->second) {
		TcpReassemblyStream *prevStreamByLastAck = NULL;
		if(this->queueStreams.size() && this->queue_by_ack.find(this->last_ack) != this->queue_by_ack.end()) {
			prevStreamByLastAck = this->queue_by_ack[this->last_ack];
		}
		stream = new FILE_LINE(36005) TcpReassemblyStream(this);
		stream->direction = direction;
		stream->ack = packet.header_tcp.ack_seq;
		if(!reassembly->simpleByAck) {
			if(prevStreamByLastAck && direction == prevStreamByLastAck->direction) {
				prevStreamByLastAck->last_seq = packet.header_tcp.seq;
				stream->first_seq = prevStreamByLastAck->last_seq;
			} else {
				stream->first_seq = prevStreamByLastAck ? 
							prevStreamByLastAck->ack : 
							(direction == TcpReassemblyStream::DIRECTION_TO_DEST ?
								this->first_seq_to_dest :
								this->first_seq_to_source);
				this->setLastSeq(direction == TcpReassemblyStream::DIRECTION_TO_DEST ?
							TcpReassemblyStream::DIRECTION_TO_SOURCE :
							TcpReassemblyStream::DIRECTION_TO_DEST,
						 packet.header_tcp.ack_seq);
			}
		}
		this->queue_by_ack[stream->ack] = stream;
		this->queueStreams.push_back(stream);
		if(ENABLE_DEBUG(reassembly->getType(), _debug_packet)) {
			(*_debug_stream) 
				<< endl
				<< " -- NEW STREAM (" << stream->ack << ")"
				<< " - first_seq: " << stream->first_seq
				<< endl;
		}
	} else {
		stream = iter->second;
	}
	stream->push(packet);
	if(!stream->min_seq ||
	   TCP_SEQ_CMP(packet.header_tcp.seq, stream->min_seq) < 0) {
		stream->min_seq = packet.header_tcp.seq;
	}
	if(!stream->max_next_seq ||
	   TCP_SEQ_CMP(packet.next_seq, stream->max_next_seq) > 0) {
		stream->max_next_seq = packet.next_seq;
	}
	this->last_ack = stream->ack;
	this->last_packet_at_from_header = getTimeMS(&packet.time);
}

void TcpReassemblyLink::printContent(int level) {
	std::ostream *__debug_stream = _debug_stream ? _debug_stream : &cout;
	map<uint32_t, TcpReassemblyStream*>::iterator iter;
	int counter = 0;
	for(iter = this->queue_by_ack.begin(); iter != this->queue_by_ack.end(); iter++) {
		(*__debug_stream)
			<< fixed 
			<< setw(level * 5) << ""
			<< setw(3) << (++counter) << "   " 
			<< setw(15) << this->ip_src.getString() << "/" << setw(6) << this->port_src
			<< " -> " 
			<< setw(15) << this->ip_dst.getString() << "/" << setw(6) << this->port_dst
			<< endl;
		iter->second->printContent(level + 1);
	}
}

void TcpReassemblyLink::cleanup(u_int64_t act_time) {
	map<uint32_t, TcpReassemblyStream*>::iterator iter;
	
	/*
	cout << "*** call cleanup " 
	     << fixed
	     << setw(15) << this->ip_src.getString() << "/" << setw(6) << this->port_src
	     << " -> " 
	     << setw(15) << this->ip_dst.getString() << "/" << setw(6) << this->port_dst
	     << endl;
	*/
	
	if(reassembly->type == TcpReassembly::http) {
		if(reassembly->enableHttpCleanupExt) {
			unsigned queue_by_ack_size = this->queue_by_ack.size();
			float divLinkTimeout = queue_by_ack_size > 500 ? 20 :
					       queue_by_ack_size > 300 ? 10 :
					       queue_by_ack_size > 200 ? 2 : 0.5;
			for(iter = this->queue_by_ack.begin(); iter != this->queue_by_ack.end(); ) {
				bool erase_qpv = false;
				map<uint32_t, TcpReassemblyStream_packet_var>::iterator iter_qpv;
				for(iter_qpv = iter->second->queuePacketVars.begin(); iter_qpv != iter->second->queuePacketVars.end(); ) {
					if(iter_qpv->second.last_packet_at_from_header &&
					   iter_qpv->second.last_packet_at_from_header < act_time - (reassembly->linkTimeout/divLinkTimeout) * 1000 &&
					   iter_qpv->second.last_packet_at_from_header < this->last_packet_process_cleanup_at) {
						iter->second->queuePacketVars.erase(iter_qpv++);
						erase_qpv = true;
					} else {
						iter_qpv++;
					}
				}
				if(!iter->second->queuePacketVars.size()) {
					delete iter->second;
					this->queue_by_ack.erase(iter++);
				} else {
					if(erase_qpv) {
						iter->second->clearCompleteData();
					}
					iter++;
				}
			}
		} else {
			for(iter = this->queue_by_ack.begin(); iter != this->queue_by_ack.end(); ) {
				if(iter->second->queuePacketVars.size() > 500) {
					if(this->reassembly->isActiveLog() || ENABLE_DEBUG(reassembly->getType(), _debug_cleanup)) {
						ostringstream outStr;
						outStr << fixed 
						       << "cleanup " 
						       << reassembly->getTypeString()
						       << " - remove ack " << iter->first 
						       << " (too much seq - " << iter->second->queuePacketVars.size() << ") "
						       << setw(15) << this->ip_src.getString() << "/" << setw(6) << this->port_src
						       << " -> " 
						       << setw(15) << this->ip_dst.getString() << "/" << setw(6) << this->port_dst;
						if(ENABLE_DEBUG(reassembly->getType(), _debug_cleanup)) {
							(*_debug_stream) << outStr.str() << endl;
						}
						this->reassembly->addLog(outStr.str().c_str());
					}
					delete iter->second;
					this->queue_by_ack.erase(iter++);
				} else {
					++iter;
				}
			}
		}
	}
	if(!reassembly->enableCrazySequence) {
		while(this->queueStreams.size() && this->queueStreams[0]->completed_finally) {
			if(ENABLE_DEBUG(reassembly->getType(), _debug_cleanup)) {
				(*_debug_stream)
					<< fixed 
					<< "cleanup " 
					<< reassembly->getTypeString()
					<< " - remove ack " << this->queueStreams[0]->ack 
					<< setw(15) << this->ip_src.getString() << "/" << setw(6) << this->port_src
					<< " -> " 
					<< setw(15) << this->ip_dst.getString() << "/" << setw(6) << this->port_dst
					<< endl;
			}
			
			/*
			cout << "*** cleanup finally ack " 
			     << this->queue[0]->ack
			     << endl;
			*/
			
			iter = this->queue_by_ack.find(this->queueStreams[0]->ack);
			if(iter != this->queue_by_ack.end()) {
				this->queue_by_ack.erase(iter);
			}
			delete this->queueStreams[0];
			this->queueStreams.erase(this->queueStreams.begin());
		}
	}
}

void TcpReassemblyLink::setLastSeq(TcpReassemblyStream::eDirection direction, 
				   u_int32_t lastSeq) {
	int index = -1;
	for(int i = this->queueStreams.size() - 1; i >=0; i--) {
		if(this->queueStreams[i]->direction == direction &&
		   this->queueStreams[i]->max_next_seq == lastSeq) {
			index = i;
		}
	}
	if(index < 0) {
		return;
	}
	this->queueStreams[index]->last_seq = lastSeq;
	if(ENABLE_DEBUG(reassembly->getType(), _debug_packet)) {
		(*_debug_stream) << " -- set last seq: " << lastSeq << " for ack: " << this->queueStreams[index]->ack << endl; 
	}
}

int TcpReassemblyLink::okQueue(int final, u_int32_t seq, u_int32_t next_seq, u_int32_t ack, bool psh, bool enableDebug) {
	if(this->state == STATE_CRAZY) {
		return(this->okQueue_crazy(final, enableDebug));
	} else {
		if(reassembly->simpleByAck) {
			return(this->okQueue_simple_by_ack(seq, next_seq, ack, psh, enableDebug));
		} else {
			return(this->okQueue_normal(final, enableDebug));
		}
	}
}

int TcpReassemblyLink::okQueue_normal(int final, bool enableDebug) {
	if(enableDebug) {
		(*_debug_stream)
			<< "call okQueue_normal - port: " << this->port_src 
			<< " / size: " << this->queueStreams.size()
			<< (final == 2 ? " FINAL" : "")
			<< endl;
		if(!this->queueStreams.size()) {
			(*_debug_stream) << "empty" << endl;
			return(0);
		} else {
			for(size_t i = 0; i < this->queueStreams.size(); i++) {
				(*_debug_stream) << " - ack : " << this->queueStreams[i]->ack << endl;
			}
		}
	}
	int countDataStream = 0;
	this->ok_streams.clear();
	size_t size = this->queueStreams.size();
	if(!size) {
		return(-1);
	}
	bool finOrRst = this->fin_to_dest || this->fin_to_source || this->rst;
	bool allQueueStreams = finOrRst || final == 2 || reassembly->enableValidateLastQueueDataViaCheckData || reassembly->enableStrictValidateDataViaCheckData;
	int countIter = 0;
	for(size_t i = 0; i < (allQueueStreams ? size : size - 1); i++) {
		++countIter;
		if(enableDebug && _debug_stream && i) {
			(*_debug_stream) << endl;
		}
		int rslt = this->queueStreams[i]->ok(false, 
						     i == size - 1 && (finOrRst || final == 2), 
						     i == size - 1 ? 0 : this->queueStreams[i]->max_next_seq,
						     reassembly->enableValidateLastQueueDataViaCheckData && i == size - 1 ? 1 : -1, 
						     -1, 
						     -1,
						     NULL,
						     enableDebug,
						     this->forceOk && i == 0 ? this->queueStreams[0]->min_seq : 0);
		if(rslt <= 0) {
			if(i == 0 && this->forceOk) {
				// skip bad first stream
			} else {
				break;
			}
		} else {
			this->ok_streams.push_back(this->queueStreams[i]);
			++countDataStream;
		}
	}
	return(countIter ? countDataStream : -1);
}

int TcpReassemblyLink::okQueue_simple_by_ack(u_int32_t seq, u_int32_t next_seq, u_int32_t ack, bool psh, bool enableDebug) {
	this->ok_streams.clear();
	if(this->queue_by_ack.find(ack) != this->queue_by_ack.end()) {
		TcpReassemblyStream *stream = this->queue_by_ack[ack];
		if(stream) {
			unsigned okStreams = 0;
			u_int32_t max_seq = 0;
			for(int mainPass = 0; mainPass <= (reassembly->smartMaxSeq || reassembly->smartMaxSeqByPsh ? 1 : 0); mainPass++) {
				// mainPass:
				//  - 0 - use next_seq if psh
				//  - 1 - use max_next_seq
				if(reassembly->smartMaxSeq || reassembly->smartMaxSeqByPsh) {
					u_int32_t _max_seq = 0;
					switch(mainPass) {
					case 0:
						if(reassembly->smartMaxSeq || (reassembly->smartMaxSeqByPsh && psh)) {
							_max_seq = next_seq;
							break;
						}
					case 1:
						_max_seq = stream->max_next_seq;
						if(mainPass < 1) {
							mainPass = 1;
						}
						break;
					}
					if(_max_seq && _max_seq != max_seq) {
						max_seq = _max_seq;
					} else {
						continue;
					}
				} else {
					max_seq = stream->max_next_seq;
				}
				okStreams = 0;
				bool useSeq = false;
				unsigned streamsSizePass0 = 0;
				for(int pass = 0; pass < 2 && !okStreams; pass++) {
					// pass:
					//  - 0 - get prev streams first
					//  - 1 - check ack stream; if !checkOkData, then use prev streams
					//  - 2 - suppress use prev streams and end loop
					if(ENABLE_DEBUG(reassembly->getType(), _debug_check_ok)) {
						(*_debug_stream) << " -- try max seq " << max_seq << endl;
					}
					if(mainPass > 0) {
						stream->is_ok = false;
						stream->ok_packets.clear();
						stream->clearCompleteData();
					}
					if(!stream->ok(false, true, max_seq,
						       0, 0, 0,
						       NULL, enableDebug,
						       stream->min_seq)) {
						break;
					}
					for(unsigned i = 0; i < stream->ok_packets.size(); i++) {
						if(seq == stream->ok_packets[i][0]) {
							useSeq = true;
							break;
						}
					}
					vector<TcpReassemblyStream*> streams;
					streams.push_back(stream);
					if(pass == 0) {
						if(stream->queuePacketVars.find(stream->ok_packets[0][0]) != stream->queuePacketVars.end() &&
						   !stream->queuePacketVars[stream->ok_packets[0][0]].offset) {
							TcpReassemblyStream *prevStream = NULL;
							do {
								TcpReassemblyStream_packet *packet_end = NULL;
								if(reassembly->completeMod == 1) {
									prevStream = findStreamByMaxNextSeq(streams[streams.size() - 1]->ok_packets[0][0], streams[streams.size() - 1]->ok_packets[0][1] - 1);
									if(!prevStream) {
										prevStream = findStreamByNextSeq(streams[streams.size() - 1]->ok_packets[0][0], 0, &packet_end);
									}
								} else {
									prevStream = findStreamByMaxNextSeq(streams[streams.size() - 1]->min_seq);
								}
								if(prevStream) {
									bool exists = false;
									for(unsigned i = 0; i < streams.size(); i++) {
										if(prevStream->ack == streams[i]->ack) {
											exists = true;
											break;
										}
									}
									if(exists) {
										break;
									}
									if(ENABLE_DEBUG(reassembly->getType(), _debug_rslt)) {
										(*_debug_stream)
											<< " ?? prev stream ack " << prevStream->ack
											<< " (" << __FILE__ << ":" << __LINE__ << ")"
											<< endl;
									}
									if(packet_end) {
										prevStream->clearCompleteData();
										prevStream->is_ok = false;
									}
									if((reassembly->enableSmartCompleteData &&
									    prevStream->isSetCompleteData() && prevStream->is_ok) ||
									   prevStream->ok(false, true, 
											  packet_end ? packet_end->next_seq : prevStream->max_next_seq,
											  0, 0, 0,
											  NULL, enableDebug,
											  prevStream->min_seq)) {
										bool ok = false;
										if(prevStream->ok_packets.size() && streams[streams.size() - 1]->ok_packets.size()) {
											if(streams[streams.size() - 1]->ok_packets[0][0] == prevStream->ok_packets.back()[1]) {
												if(reassembly->ignoreZeroData) {
													TcpReassemblyStream_packet *packet = prevStream->getPacket(prevStream->ok_packets.back()[0], prevStream->ok_packets.back()[1]);
													if(packet && packet->datacaplen && packet->data[0] != 0) {
														ok = true;
													}
												} else {
													ok = true;
												}
											}
											if(!ok) {
												TcpReassemblyStream_packet *packet = streams[streams.size() - 1]->getPacket(streams[streams.size() - 1]->ok_packets[0][0], streams[streams.size() - 1]->ok_packets[0][1]);
												extern int process_packet__parse_sip_method_ext(char *data, unsigned int datalen, bool check_end_space, bool *sip_response);
												if(packet && !process_packet__parse_sip_method_ext((char*)packet->data, packet->datacaplen, true, NULL)) {
													ok = true;
												}
											}
										}
										if(!ok) {
											prevStream->clearCompleteData();
											prevStream->is_ok = false;
											break;
										}
										streams.push_back(prevStream);
										if(ENABLE_DEBUG(reassembly->getType(), _debug_rslt)) {
											(*_debug_stream)
												<< " ++ prev stream ack " << prevStream->ack
												<< " (" << __FILE__ << ":" << __LINE__ << ")"
												<< endl;
										}
									} else {
										prevStream->clearCompleteData();
										prevStream->is_ok = false;
										break;
									}
								}
							} while(prevStream && streams.size() < 20);
						}
						streamsSizePass0 = streams.size();
						if(streamsSizePass0 == 1) {
							pass = 2;
						}
					}

					#if TCP_REASSEMBLY_STREAMS_MAX_LOG
					static unsigned __max = 0;
					if(streams.size() > __max) {
						__max = streams.size();
						if(__max > 1) {
							string link_id = ip_src.getString(true) + ":" + port_src.getString() + "->" +
									 ip_dst.getString(true) + ":" + port_dst.getString();
							syslog(LOG_NOTICE, " *** tcp reassembly streams max: %u (%s)", __max, link_id.c_str());
						}
					}
					#endif
					
					while(true) {
						if(streams.size() == 1) {
							u_int32_t datalen_confirmed;
							if(reassembly->checkOkData(stream->complete_data.getData(), stream->complete_data.getDatalen(), 
										   reassembly->completeMod == 1 ? TcpReassemblySip::_chssm_na : TcpReassemblySip::_chssm_strict,
										   &sip_offsets, &datalen_confirmed)) {
								if(ENABLE_DEBUG(reassembly->getType(), _debug_rslt)) {
									(*_debug_stream)
										<< " -- checkOkData: OK "
										<< " (datalen: " << stream->complete_data.getDatalen() << " confirmed: " << datalen_confirmed << ")"
										<< " (" << __FILE__ << ":" << __LINE__ << ")"
										<< endl;
								}
								if(reassembly->completeMod == 1 && datalen_confirmed) {
									if(datalen_confirmed < stream->complete_data.getDatalen()) {
										u_int32_t last_seq = 0;
										stream->confirmCompleteData(datalen_confirmed, &last_seq);
										stream->complete_data.setDatalen(datalen_confirmed);
										last_ok_seq_direction[stream->direction == TcpReassemblyStream::DIRECTION_TO_DEST] = last_seq;
									} else {
										last_ok_seq_direction[stream->direction == TcpReassemblyStream::DIRECTION_TO_DEST] = stream->ok_packets[stream->ok_packets.size() - 1][1];
									}
								} else {
									last_ok_seq_direction[stream->direction == TcpReassemblyStream::DIRECTION_TO_DEST] = 0;
								}
								this->ok_streams.push_back(streams[0]);
								streams[0]->counterTryOk = 0;
								okStreams = 1;
								break;
							} else {
								if(ENABLE_DEBUG(reassembly->getType(), _debug_rslt)) {
									(*_debug_stream)
										<< " -- checkOkData: FAILED "
										<< " (" << __FILE__ << ":" << __LINE__ << ")"
										<< endl;
								}
							}
						} else {
							for(unsigned checkOkStreams = streams.size(); checkOkStreams >= (reassembly->completeMod == 1 ? 1 : streams.size()) && !okStreams; checkOkStreams--) {
								if(checkOkStreams > 1) {
									bool okBeginSip = false;
									TcpReassemblyStream *lastStream = streams[checkOkStreams - 1];
									TcpReassemblyStream_packet *packet = lastStream->getPacket(lastStream->ok_packets[0][0], lastStream->ok_packets[0][1]);
									if(packet && packet->datacaplen) {
										u_int32_t offset_packet = lastStream->getPacketOffset(lastStream->ok_packets[0][0]);
										if(offset_packet < packet->datacaplen) {
											if(!offset_packet) {
												okBeginSip = packet->flags.flags_bit.is_sip;
											} else if(packet->datacaplen - offset_packet >= 11) {
												okBeginSip = check_sip20((char*)packet->data + offset_packet,
															 packet->datacaplen - offset_packet,
															 NULL, true);
											} else {
												okBeginSip = check_sip20((char*)lastStream->complete_data.getData(), 
															 lastStream->complete_data.getDatalen(),
															 NULL, true);
											}
										}
									}
									while(!okBeginSip && lastStream->ok_packets.size() > 1) {
										lastStream->ok_packets.pop_front();
										TcpReassemblyStream_packet *packet = lastStream->getPacket(lastStream->ok_packets[0][0], lastStream->ok_packets[0][1]);
										if(packet && packet->datacaplen) {
											u_int32_t offset_packet = lastStream->getPacketOffset(lastStream->ok_packets[0][0]);
											if(offset_packet < packet->datacaplen) {
												bool needRefreshCompleteData = false;
												if(!offset_packet) {
													okBeginSip = packet->flags.flags_bit.is_sip;
													if(okBeginSip) {
														needRefreshCompleteData = true;
													}
												} else if(packet->datacaplen - offset_packet >= 11) {
													okBeginSip = check_sip20((char*)packet->data + offset_packet,
																 packet->datacaplen - offset_packet,
																 NULL, true);
													if(okBeginSip) {
														needRefreshCompleteData = true;
													}
												} else {
													lastStream->clearCompleteData();
													lastStream->saveCompleteData();
													okBeginSip = check_sip20((char*)lastStream->complete_data.getData(), 
																 lastStream->complete_data.getDatalen(),
																 NULL, true);
												}
												if(needRefreshCompleteData) {
													lastStream->clearCompleteData();
													lastStream->saveCompleteData();
												}
											}
										}
									}
									if(!okBeginSip) {
										continue;
									}
								}
								SimpleBuffer data;
								for(unsigned i = 0; i < checkOkStreams; i++) {
									unsigned data_overlay = 0;
									TcpReassemblyStream *actStream = streams[checkOkStreams - 1 - i];
									if(actStream->ok_packets.size()) {
										TcpReassemblyStream *prevStream = checkOkStreams > 1 && i < checkOkStreams - 1 ? streams[checkOkStreams - 1 - i - 1] : NULL;
										if(prevStream && prevStream->ok_packets.size()) {
											unsigned max_seq = actStream->ok_packets[actStream->ok_packets.size() - 1][1];
											unsigned next_seq = prevStream->ok_packets[0][0];
											if(TCP_SEQ_CMP(max_seq, next_seq) > 0 &&
											   TCP_SEQ_SUB(max_seq, next_seq) < actStream->complete_data.getDatalen()) {
												data_overlay = TCP_SEQ_SUB(max_seq, next_seq);
											}
										}
									}
									data.add(actStream->complete_data.getData(), actStream->complete_data.getDatalen() - data_overlay);
								}
								u_int32_t datalen_confirmed;
								if(reassembly->checkOkData(data.data(), data.size(), 
											   (reassembly->completeMod == 1 ? TcpReassemblySip::_chssm_na : TcpReassemblySip::_chssm_strict) |
											   (checkOkStreams > 1 ? TcpReassemblySip::_chssm_ext : TcpReassemblySip::_chssm_na),
											   &sip_offsets, &datalen_confirmed)) {
									if(ENABLE_DEBUG(reassembly->getType(), _debug_rslt)) {
										(*_debug_stream)
											<< " -- checkOkData (streams: " << checkOkStreams << "): OK "
											<< " (datalen: " << data.size() << " confirmed: " << datalen_confirmed << ")"
											<< " (" << __FILE__ << ":" << __LINE__ << ")"
											<< endl;
									}
									unsigned fromStream = 0;
									if(reassembly->completeMod == 1 && datalen_confirmed) {
										if(datalen_confirmed < data.size()) {
											unsigned fromStreamSize = 0;
											while(checkOkStreams - 1 > fromStream &&
											      (data.size() - datalen_confirmed - fromStreamSize) >= streams[fromStream]->complete_data.getDatalen()) {
												fromStreamSize += streams[fromStream]->complete_data.getDatalen();
												++fromStream;
											}
											if(datalen_confirmed < data.size() - fromStreamSize &&
											   (data.size() - datalen_confirmed - fromStreamSize) < streams[fromStream]->complete_data.getDatalen()) {
												if((checkOkStreams - fromStream) == 1) {
													u_int32_t last_seq = 0;
													streams[fromStream]->confirmCompleteData(datalen_confirmed, &last_seq);
													streams[fromStream]->complete_data.setDatalen(datalen_confirmed);
													last_ok_seq_direction[stream->direction == TcpReassemblyStream::DIRECTION_TO_DEST] = last_seq;
												} else {
													streams[fromStream]->confirmCompleteData(streams[fromStream]->complete_data.getDatalen() - (data.size() - datalen_confirmed));
													last_ok_seq_direction[stream->direction == TcpReassemblyStream::DIRECTION_TO_DEST] = 0;
													for(unsigned i = fromStream + 1; i < checkOkStreams; i++) {
														streams[i]->clearPacketOffset(streams[i]->ok_packets.back()[0]);
													}
												}
											} else {
												last_ok_seq_direction[stream->direction == TcpReassemblyStream::DIRECTION_TO_DEST] = 0;
											}
										} else {
											last_ok_seq_direction[stream->direction == TcpReassemblyStream::DIRECTION_TO_DEST] = streams[fromStream]->ok_packets[streams[fromStream]->ok_packets.size() - 1][1];;
										}
									} else {
										last_ok_seq_direction[stream->direction == TcpReassemblyStream::DIRECTION_TO_DEST] = 0;
									}
									for(int i = checkOkStreams - 1; i >= (int)fromStream; i--) {
										this->ok_streams.push_back(streams[i]);
										streams[i]->counterTryOk = 0;
									}
									okStreams = checkOkStreams;
									break;
								}
							}
							if(okStreams) {
								break;
							}
						}
						if(pass == 1) {
							if(streams.size() == streamsSizePass0 - 1) {
								break;
							}
							TcpReassemblyStream *prevStream = findStreamByMaxNextSeq(streams[streams.size() - 1]->min_seq);
							if(prevStream) {
								if((reassembly->enableSmartCompleteData &&
								    prevStream->isSetCompleteData() && prevStream->is_ok) ||
								   prevStream->ok(false, true, prevStream->max_next_seq,
										  0, 0, 0,
										  NULL, enableDebug,
										  prevStream->min_seq)) {
									streams.push_back(prevStream);
								} else {
									prevStream->clearCompleteData();
									prevStream->is_ok = false;
									break;
								}
							} else {
								break;
							}
						} else {
							break;
						}
					}
					if(!okStreams && !reassembly->enableSmartCompleteData) {
						for(unsigned i = 0; i < streams.size();i++) {
							streams[i]->clearCompleteData();
							streams[i]->is_ok = false;
						}
					}
				}
				if(reassembly->getType() == TcpReassembly::sip &&
				   !okStreams && stream->ok_packets.size() > 0) {
					bool possibleStartSipInLastOkSeqPos = false;
					if(last_ok_seq_direction[stream->direction == TcpReassemblyStream::DIRECTION_TO_DEST] &&
					   last_ok_seq_direction[stream->direction == TcpReassemblyStream::DIRECTION_TO_DEST] > stream->ok_packets[0][0] &&
					   last_ok_seq_direction[stream->direction == TcpReassemblyStream::DIRECTION_TO_DEST] < stream->ok_packets[0][1]) {
						TcpReassemblyStream_packet *packet = stream->getPacket(stream->ok_packets[0][0], stream->ok_packets[0][1]);
						u_int32_t offset = last_ok_seq_direction[stream->direction == TcpReassemblyStream::DIRECTION_TO_DEST] - stream->ok_packets[0][0];
						if(packet && packet->datacaplen > offset) {
							possibleStartSipInLastOkSeqPos = check_sip20((char*)packet->data + offset, 
												     packet->datacaplen - offset,
												     NULL, true);
						}
					}
					if(possibleStartSipInLastOkSeqPos) {
						TcpReassemblyStream_packet_var *packet_var = stream->getPacketVars(stream->ok_packets[0][0]);
						u_int32_t offset_old = packet_var->offset;
						packet_var->offset = last_ok_seq_direction[stream->direction == TcpReassemblyStream::DIRECTION_TO_DEST] - stream->ok_packets[0][0];
						stream->clearCompleteData();
						stream->saveCompleteData();
						u_int32_t datalen_confirmed;
						if(reassembly->checkOkData(stream->complete_data.getData(), stream->complete_data.getDatalen(), 
									   reassembly->completeMod == 1 ? TcpReassemblySip::_chssm_na : TcpReassemblySip::_chssm_strict, 
									   &sip_offsets, &datalen_confirmed)) {
							if(ENABLE_DEBUG(reassembly->getType(), _debug_rslt)) {
								(*_debug_stream)
									<< " -- checkOkData: OK "
									<< " (datalen: " << stream->complete_data.getDatalen() << " confirmed: " << datalen_confirmed << ")"
									<< " (" << __FILE__ << ":" << __LINE__ << ")"
									<< endl;
							}
							if(reassembly->completeMod == 1 && datalen_confirmed) {
								if(datalen_confirmed < stream->complete_data.getDatalen()) {
									u_int32_t last_seq = 0;
									stream->confirmCompleteData(datalen_confirmed, &last_seq);
									stream->complete_data.setDatalen(datalen_confirmed);
									last_ok_seq_direction[stream->direction == TcpReassemblyStream::DIRECTION_TO_DEST] = last_seq;
								} else {
									last_ok_seq_direction[stream->direction == TcpReassemblyStream::DIRECTION_TO_DEST] = stream->ok_packets[stream->ok_packets.size() - 1][1];
								}
							} else {
								last_ok_seq_direction[stream->direction == TcpReassemblyStream::DIRECTION_TO_DEST] = 0;
							}
							this->ok_streams.push_back(stream);
							stream->counterTryOk = 0;
							okStreams = 1;
						} else {
							stream->clearCompleteData();
							stream->is_ok = false;
						}
						packet_var->offset = offset_old;
					} else {
						bool possibleStartSipInNextPacket = false;
						if(stream->ok_packets.size() > 1 && stream->complete_data_index.size() > 1) {
							u_int32_t offset = stream->complete_data_index[0].len;
							for(unsigned i = 1; i < stream->complete_data_index.size(); i++) {
								u_int32_t offset_packet = stream->getPacketOffset(stream->complete_data_index[i].seq);
								if(check_sip20((char*)(stream->complete_data.getData() + offset + offset_packet), 
									       stream->complete_data.getDatalen() - offset - offset_packet,
									       NULL, true)) {
									possibleStartSipInNextPacket = true;
									break;
								}
								offset += stream->complete_data_index[i].len;
							}
						}
						if(possibleStartSipInNextPacket) {
							while(stream->ok_packets.size() > 1 && !okStreams) {
								stream->ok_packets.pop_front();
								stream->clearCompleteData();
								stream->saveCompleteData();
								u_int32_t datalen_confirmed;
								if(reassembly->checkOkData(stream->complete_data.getData(), stream->complete_data.getDatalen(), 
											   reassembly->completeMod == 1 ? TcpReassemblySip::_chssm_na : TcpReassemblySip::_chssm_strict, 
											   &sip_offsets, &datalen_confirmed)) {
									if(ENABLE_DEBUG(reassembly->getType(), _debug_rslt)) {
										(*_debug_stream)
											<< " -- checkOkData: OK "
											<< " (datalen: " << stream->complete_data.getDatalen() << " confirmed: " << datalen_confirmed << ")"
											<< " (" << __FILE__ << ":" << __LINE__ << ")"
											<< endl;
									}
									if(reassembly->completeMod == 1 && datalen_confirmed) {
										if(datalen_confirmed < stream->complete_data.getDatalen()) {
											u_int32_t last_seq = 0;
											stream->confirmCompleteData(datalen_confirmed, &last_seq);
											stream->complete_data.setDatalen(datalen_confirmed);
											last_ok_seq_direction[stream->direction == TcpReassemblyStream::DIRECTION_TO_DEST] = last_seq;
										} else {
											last_ok_seq_direction[stream->direction == TcpReassemblyStream::DIRECTION_TO_DEST] = stream->ok_packets[stream->ok_packets.size() - 1][1];
										}
									} else {
										last_ok_seq_direction[stream->direction == TcpReassemblyStream::DIRECTION_TO_DEST] = 0;
									}
									this->ok_streams.push_back(stream);
									stream->counterTryOk = 0;
									okStreams = 1;
								} else {
									if(ENABLE_DEBUG(reassembly->getType(), _debug_rslt)) {
										(*_debug_stream)
											<< " -- checkOkData: FAILED "
											<< " (" << __FILE__ << ":" << __LINE__ << ")"
											<< endl;
									}
								}
							}
							if(!okStreams) {
								stream->clearCompleteData();
								stream->is_ok = false;
							}
						}
					}
				}
				if(reassembly->getType() == TcpReassembly::sip &&
				   !okStreams && seq && !useSeq) {
					TcpReassemblyStream_packet *packet = stream->getPacket(seq);
					if(packet && packet->datacaplen > 0 && 
					   check_sip20((char*)packet->data, packet->datacaplen, NULL, true)) {
						stream->ok_packets.clear();
						stream->ok_packets.push_back(d_u_int32_t(packet->header_tcp.seq, packet->next_seq));
						stream->clearCompleteData();
						stream->saveCompleteData();
						u_int32_t datalen_confirmed;
						if(reassembly->checkOkData(stream->complete_data.getData(), stream->complete_data.getDatalen(), 
									   reassembly->completeMod == 1 ? TcpReassemblySip::_chssm_na : TcpReassemblySip::_chssm_strict, 
									   &sip_offsets, &datalen_confirmed)) {
							if(ENABLE_DEBUG(reassembly->getType(), _debug_rslt)) {
								(*_debug_stream)
									<< " -- checkOkData: OK "
									<< " (datalen: " << stream->complete_data.getDatalen() << " confirmed: " << datalen_confirmed << ")"
									<< " (" << __FILE__ << ":" << __LINE__ << ")"
									<< endl;
							}
							if(reassembly->completeMod == 1 && datalen_confirmed) {
								if(datalen_confirmed < stream->complete_data.getDatalen()) {
									u_int32_t last_seq = 0;
									stream->confirmCompleteData(datalen_confirmed, &last_seq);
									stream->complete_data.setDatalen(datalen_confirmed);
									last_ok_seq_direction[stream->direction == TcpReassemblyStream::DIRECTION_TO_DEST] = last_seq;
								} else {
									last_ok_seq_direction[stream->direction == TcpReassemblyStream::DIRECTION_TO_DEST] = stream->ok_packets[stream->ok_packets.size() - 1][1];
								}
							} else {
								last_ok_seq_direction[stream->direction == TcpReassemblyStream::DIRECTION_TO_DEST] = 0;
							}
							this->ok_streams.push_back(stream);
							stream->counterTryOk = 0;
							okStreams = 1;
						} else {
							stream->clearCompleteData();
							stream->is_ok = false;
						}
					}
				}
				if(okStreams > 0) {
					return(okStreams);
				}
			}
			return(okStreams);
		}
	}
	return(-1);
}

int TcpReassemblyLink::okQueue_crazy(int final, bool enableDebug) {
	streamIterator iter = this->createIterator();
	if(!this->direction_confirm) {
		return(-2);
	}
	if(!iter.stream) {
		return(-10);
	}
	this->ok_streams.clear();
	int countDataStream = 0;
	for(int pass = 0; pass < (final ? 2 : 1) && !countDataStream; pass++) {
		vector<u_int32_t> processedAck;
		if(pass > 0) {
			iter.init();
		}
		TcpReassemblyStream *lastHttpStream = NULL;
		while(true) {
			/* disable - probably obsolete / caused infinite loop
			if(pass == 1 &&
			   iter.state == STATE_SYN_FORCE_OK) {
				if(!iter.nextAckInDirection()) {
					break;
				}
			}
			*/
			
			/*
			if(iter.stream->ack == 4180930954) {
				cout << " -- ***** -- ";
			}
			*/
			
			if(enableDebug && ENABLE_DEBUG(reassembly->getType(), _debug_check_ok_process)) {
				iter.print();
				(*_debug_stream) << "   ";
			}
			if(iter.state >= STATE_SYN_OK) {
				processedAck.push_back(iter.stream->ack);
				u_int32_t maxNextSeq = iter.getMaxNextSeq();
				if(iter.stream->exists_data) {
					if(enableDebug && _debug_stream) {
						(*_debug_stream) << endl;
					}
					if(iter.stream->ok(true, maxNextSeq == 0, maxNextSeq,
							   -1, -1, -1,
							   lastHttpStream, enableDebug)) {
						bool existsAckInStream = false;
						for(size_t i  = 0; i < this->ok_streams.size(); i++) {
							if(this->ok_streams[i]->ack == iter.stream->ack) {
								existsAckInStream = true;
								break;
							}
						}
						if(!existsAckInStream) {
							this->ok_streams.push_back(iter.stream);
							++countDataStream;
							if(iter.stream->http_ok) {
								lastHttpStream = iter.stream;
							}
						}
					} else if(pass == 1) {
						if(iter.nextSeqInDirection()) {
							continue;
						}
					}
				}
			}
			
			/*
			if(iter.stream->ack == 4180930954) {
				cout << " -- ***** -- ";
			}
			*/
			
			if(enableDebug && ENABLE_DEBUG(reassembly->getType(), _debug_check_ok_process)) {
				(*_debug_stream) << endl;
			}
			if(!iter.next()) {
				bool completeExpectContinue = false;
				if(iter.stream->direction == TcpReassemblyStream::DIRECTION_TO_SOURCE &&
				   iter.stream->complete_data.getData() &&
				   iter.stream->complete_data.getDatalen() == 25 &&
				   !memcmp(iter.stream->complete_data.getData(), "HTTP/1.1 100 Continue\r\n\r\n", 25) &&
				   this->ok_streams.size() > 1 &&
				   this->ok_streams[this->ok_streams.size() - 2]->http_expect_continue &&
				   this->ok_streams[this->ok_streams.size() - 2]->http_content_length &&
				   iter.stream->ack > this->ok_streams[this->ok_streams.size() - 2]->min_seq &&
				   iter.stream->ack < this->ok_streams[this->ok_streams.size() - 2]->max_next_seq) {
					TcpReassemblyDataItem dataItem = this->ok_streams[this->ok_streams.size() - 2]->complete_data;
					this->ok_streams[this->ok_streams.size() - 2]->complete_data.clearData();
					this->ok_streams[this->ok_streams.size() - 2]->is_ok = false;
					this->ok_streams[this->ok_streams.size() - 2]->_ignore_expect_continue = true;
					if(this->ok_streams[this->ok_streams.size() - 2]->ok(true, false, 0,
											     -1, -1, -1,
											     NULL, false)) {
						completeExpectContinue = true;
						iter.stream = this->ok_streams[this->ok_streams.size() - 2];
						if(!iter.nextAckInDirection()) {
							break;
						}
					}
					if(!completeExpectContinue &&
					   this->ok_streams[this->ok_streams.size() - 2]->detect_ok_max_next_seq) {
						if(this->ok_streams[this->ok_streams.size() - 2]->ok2_ec(iter.stream->max_next_seq)) {
							this->ok_streams.push_back(this->queue_by_ack[iter.stream->max_next_seq]);
							completeExpectContinue = true;
							iter.stream = this->ok_streams[this->ok_streams.size() - 1];
							iter.next();
						}
					}
					if(!completeExpectContinue) {
						this->ok_streams[this->ok_streams.size() - 2]->is_ok = true;
						this->ok_streams[this->ok_streams.size() - 2]->_ignore_expect_continue = false;
						this->ok_streams[this->ok_streams.size() - 2]->complete_data = dataItem;
					}
				} else if(this->ok_streams.size() > 0 &&
					  this->ok_streams[this->ok_streams.size() - 1]->http_expect_continue &&
					  this->ok_streams[this->ok_streams.size() - 1]->complete_data.getData() && 
					  this->ok_streams[this->ok_streams.size() - 1]->complete_data.getDatalen() < this->ok_streams[this->ok_streams.size() - 1]->http_content_length) {
					TcpReassemblyDataItem dataItem = this->ok_streams[this->ok_streams.size() - 1]->complete_data;
					this->ok_streams[this->ok_streams.size() - 1]->complete_data.clearData();
					this->ok_streams[this->ok_streams.size() - 1]->is_ok = false;
					this->ok_streams[this->ok_streams.size() - 1]->_ignore_expect_continue = true;
					if(!this->ok_streams[this->ok_streams.size() - 1]->ok(true, false, 0,
											      -1, -1, -1,
											      NULL, false)) {
						this->ok_streams[this->ok_streams.size() - 1]->is_ok = true;
						this->ok_streams[this->ok_streams.size() - 1]->_ignore_expect_continue = false;
						this->ok_streams[this->ok_streams.size() - 1]->complete_data = dataItem;
					}
				}
				if(!completeExpectContinue) {
					if(iter.stream->direction == TcpReassemblyStream::DIRECTION_TO_DEST) {
						if(!iter.nextAckByMaxSeqInReverseDirection() &&
						   !iter.nextAckInDirection()) {
							break;
						}
					} else if(iter.stream->direction == TcpReassemblyStream::DIRECTION_TO_SOURCE) {
						if(!iter.nextAckInReverseDirection()) {
							if(iter.stream->direction == TcpReassemblyStream::DIRECTION_TO_SOURCE &&
							   iter.stream->complete_data.getData() &&
							   iter.stream->complete_data.getDatalen() == 25 &&
							   !memcmp(iter.stream->complete_data.getData(), "HTTP/1.1 100 Continue\r\n\r\n", 25) &&
							   this->ok_streams.size() > 1) {
								TcpReassemblyDataItem dataItem = this->ok_streams[this->ok_streams.size() - 1]->complete_data;
								this->ok_streams[this->ok_streams.size() - 1]->complete_data.clearData();
								this->ok_streams[this->ok_streams.size() - 1]->is_ok = false;
								if(!iter.stream->ok(true, false, 0,
										    -1, -1, -1,
										    NULL, false,
										    iter.stream->ok_packets[0][1]) ||
								   (iter.stream->complete_data.getData() &&
								    memcmp(iter.stream->complete_data.getData(), "HTTP/1.1 200 OK", 15))) {
									this->ok_streams[this->ok_streams.size() - 1]->is_ok = true;
									this->ok_streams[this->ok_streams.size() - 1]->complete_data = dataItem;
								}
							}
							break;
						}
					} else {
						break;
					}
					if(iter.stream && iter.stream->ack) {
						bool okAck = true;
						while(std::find(processedAck.begin(), processedAck.end(), iter.stream->ack) != processedAck.end()) {
							if(!iter.nextAckInDirection()) {
								okAck = false;
								break;
							}
						}
						if(!okAck) {
							break;
						}
					}
				}
			} else if(iter.stream->direction == TcpReassemblyStream::DIRECTION_TO_SOURCE &&
				  (!iter.stream->complete_data.getData() ||
				   iter.stream->complete_data.getDatalen() < 25 ||
				   memcmp(iter.stream->complete_data.getData(), "HTTP/1.1 100 Continue\r\n\r\n", 25))) {
				if(this->ok_streams.size() > 0 &&
				   this->ok_streams[this->ok_streams.size() - 1]->http_expect_continue &&
				   this->ok_streams[this->ok_streams.size() - 1]->complete_data.getData() &&
				   this->ok_streams[this->ok_streams.size() - 1]->complete_data.getDatalen() <
						this->ok_streams[this->ok_streams.size() - 1]->http_content_length + this->ok_streams[this->ok_streams.size() - 1]->http_header_length + 4) {
					TcpReassemblyDataItem dataItem = this->ok_streams[this->ok_streams.size() - 1]->complete_data;
					this->ok_streams[this->ok_streams.size() - 1]->complete_data.clearData();
					this->ok_streams[this->ok_streams.size() - 1]->is_ok = false;
					this->ok_streams[this->ok_streams.size() - 1]->_ignore_expect_continue = true;
					if(!this->ok_streams[this->ok_streams.size() - 1]->ok(true, false, 0,
											      -1, -1, -1, 
											      NULL, false)) {
						this->ok_streams[this->ok_streams.size() - 1]->is_ok = true;
						this->ok_streams[this->ok_streams.size() - 1]->_ignore_expect_continue = false;
						this->ok_streams[this->ok_streams.size() - 1]->complete_data = dataItem;
						this->ok_streams[this->ok_streams.size() - 1]->http_expect_continue = true;
					}
				}
			}
			// prevent by infinite loop
			if(std::find(processedAck.begin(), processedAck.end(), iter.stream->ack) != processedAck.end()) {
				break;
			}
		}
	}
	return(iter.state < STATE_SYN_OK ? -1 : countDataStream);
}

void TcpReassemblyLink::complete(bool final, bool eraseCompletedStreams) {
	if(this->state == STATE_CRAZY) {
		this->complete_crazy(final, eraseCompletedStreams);
	} else {
		if(reassembly->simpleByAck) {
			this->complete_simple_by_ack();
		} else {
			this->complete_normal(final);
		}
	}
}

void TcpReassemblyLink::complete_normal(bool final) {
	if(ENABLE_DEBUG(reassembly->getType(), _debug_data || _debug_save)) {
		(*_debug_stream) << endl;
	}
	while(this->ok_streams.size()) {
		TcpReassemblyData *reassemblyData = NULL;
		size_t countIgnore = 0;
		size_t countData = 0;
		size_t countRequest = 0;
		size_t countResponse = 0;
		while(countIgnore < this->ok_streams.size()) {
			TcpReassemblyStream* stream = this->ok_streams[countIgnore];
			if(reassembly->enableIgnorePairReqResp ?
			    stream->completed_finally :
			    (stream->completed_finally ||
			     stream->direction == TcpReassemblyStream::DIRECTION_TO_SOURCE)) {
				++countIgnore;
			} else {
				break;
			}
		}
		TcpReassemblyStream::eDirection direction = TcpReassemblyStream::DIRECTION_TO_DEST;
		while(countIgnore + countData + countRequest + countResponse < this->ok_streams.size()) {
			TcpReassemblyStream* stream = this->ok_streams[countIgnore + countData + countRequest + countResponse];
			if(reassembly->enableIgnorePairReqResp ||
			   stream->direction == direction ||
			   (stream->direction == TcpReassemblyStream::DIRECTION_TO_SOURCE && countRequest)) {
				if(!reassemblyData) {
					reassemblyData = new FILE_LINE(36006) TcpReassemblyData;
				}
				if(reassembly->enableIgnorePairReqResp) {
					++countData;
				} else {
					direction = stream->direction;
					if(direction == TcpReassemblyStream::DIRECTION_TO_DEST) {
						++countRequest;
					} else {
						++countResponse;
					}
				}
				u_char *data = stream->complete_data.getData();
				u_int32_t datalen = stream->complete_data.getDatalen();
				timeval time = stream->complete_data.getTime();
				if(data) {
					if(reassembly->enableIgnorePairReqResp) {
						reassemblyData->addData(data, datalen, time, stream->ack, stream->complete_data.getSeq(),
									(TcpReassemblyDataItem::eDirection)stream->direction);
					} else {
						if(direction == TcpReassemblyStream::DIRECTION_TO_DEST) {
							reassemblyData->addRequest(data, datalen, time, stream->ack, stream->complete_data.getSeq());
						} else {
							reassemblyData->addResponse(data, datalen, time, stream->ack, stream->complete_data.getSeq());
						}
					}
				}
			} else {
				break;
			}
		}
		if(countData || (countRequest && (countResponse || final))) {
			if(reassembly->dataCallback) {
				reassembly->dataCallback->processData(
					this->ip_src, this->ip_dst,
					this->port_src, this->port_dst,
					reassemblyData,
					this->ethHeader, this->ethHeaderLength,
					this->handle_index, this->dlt, this->sensor_id, this->sensor_ip, this->pid,
					this->uData, this->uData2, this->uData2_last, this,
					ENABLE_DEBUG(reassembly->getType(), _debug_save) ? _debug_stream : NULL);
				reassemblyData = NULL;
			}
			for(size_t i = 0; i < countIgnore + countData + countRequest + countResponse; i++) {
				if(reassembly->enableDestroyStreamsInComplete) {
					TcpReassemblyStream *stream = this->ok_streams[0];
					this->ok_streams.erase(this->ok_streams.begin());
					for(deque<TcpReassemblyStream*>::iterator iter = this->queueStreams.begin(); iter != this->queueStreams.end();) {
						if(*iter == stream) {
							iter = this->queueStreams.erase(iter);
						} else {
							++iter;
						}
					}
					this->queue_by_ack.erase(stream->ack);
					if(stream->direction == TcpReassemblyStream::DIRECTION_TO_DEST) {
						if(stream->first_seq == this->first_seq_to_dest) {
							this->first_seq_to_dest = stream->max_next_seq;
						}
					} else {
						if(stream->first_seq == this->first_seq_to_source) {
							this->first_seq_to_source = stream->max_next_seq;
						}
					}
					delete stream;
				} else {
					this->ok_streams[0]->is_ok = false;
					this->ok_streams[0]->completed_finally = true;
					this->ok_streams.erase(this->ok_streams.begin());
				}
			}
		} else {
			if(reassemblyData) {
				delete reassemblyData;
			}
			break;
		}
	}
}

void TcpReassemblyLink::complete_simple_by_ack() {
	if(!this->ok_streams.size()) {
		return;
	}
	SimpleBuffer data;
	timeval time;
	time.tv_sec = 0;
	time.tv_usec = 0;
	u_int32_t seq = 0;
	u_int32_t ack = 0;
	TcpReassemblyDataItem::eDirection direction = TcpReassemblyDataItem::DIRECTION_NA;
	for(unsigned i = 0; i < this->ok_streams.size(); i++) {
		if(this->ok_streams[i]->complete_data.getData()) {
			data.add(this->ok_streams[i]->complete_data.getData(), this->ok_streams[i]->complete_data.getDatalen());
			if(!time.tv_sec) {
				time = this->ok_streams[i]->complete_data.getTime();
				seq = this->ok_streams[i]->complete_data.getSeq();
				ack = this->ok_streams[i]->ack;
				direction = (TcpReassemblyDataItem::eDirection)this->ok_streams[i]->direction;
			}
		}
	}
	TcpReassemblyData *reassemblyData = new FILE_LINE(36007) TcpReassemblyData;
	reassemblyData->addData(data.data(), data.size(),
				time, ack, seq,
				direction);
	reassembly->dataCallback->processData(
					this->ip_src, this->ip_dst,
					this->port_src, this->port_dst,
					reassemblyData,
					this->ethHeader, this->ethHeaderLength,
					this->handle_index, this->dlt, this->sensor_id, this->sensor_ip, this->pid,
					this->uData, this->uData2, this->uData2_last, this,
					ENABLE_DEBUG(reassembly->getType(), _debug_save) ? _debug_stream : NULL);
	while(this->ok_streams.size()) {
		TcpReassemblyStream *stream = this->ok_streams[0];
		this->ok_streams.erase(this->ok_streams.begin());
		if(reassembly->completeMod == 1) {
			stream->cleanupCompleteData();
			if(!stream->queuePacketVars.size()) {
				for(deque<TcpReassemblyStream*>::iterator iter = this->queueStreams.begin(); iter != this->queueStreams.end();) {
					if(*iter == stream) {
						iter = this->queueStreams.erase(iter);
					} else {
						++iter;
					}
				}
				this->queue_by_ack.erase(stream->ack);
				delete stream;
			}
		} else {
			for(deque<TcpReassemblyStream*>::iterator iter = this->queueStreams.begin(); iter != this->queueStreams.end();) {
				if(*iter == stream) {
					iter = this->queueStreams.erase(iter);
				} else {
					++iter;
				}
			}
			this->queue_by_ack.erase(stream->ack);
			delete stream;
		}
	}
}

void TcpReassemblyLink::complete_crazy(bool final, bool eraseCompletedStreams) {
	size_t lastCountAllWithSkip = 0;
	unsigned counterEqLastCountAllWithSkip = 0;
	while(true) {
		size_t size_ok_streams = this->ok_streams.size();
		TcpReassemblyData *reassemblyData = NULL;
		size_t skip_offset = 0;
		while(skip_offset < size_ok_streams && 
		      this->ok_streams[skip_offset]->direction != TcpReassemblyStream::DIRECTION_TO_DEST) {
			this->ok_streams[skip_offset + completed_offset]->completed_finally = true;
			++skip_offset;
		}
		size_t old_skip_offset;
		do {
			old_skip_offset = skip_offset;
			while(skip_offset < size_ok_streams && this->ok_streams[skip_offset + completed_offset]->completed_finally) {
				++skip_offset;
			}
			while(skip_offset < size_ok_streams && 
			      this->ok_streams[skip_offset]->direction != TcpReassemblyStream::DIRECTION_TO_DEST) {
				this->ok_streams[skip_offset + completed_offset]->completed_finally = true;
				++skip_offset;
			}
			while(skip_offset < size_ok_streams && 
			      !this->ok_streams[skip_offset]->http_type) {
				this->ok_streams[skip_offset + completed_offset]->completed_finally = true;
				++skip_offset;
			}
		} while(skip_offset > old_skip_offset);
		size_t countRequest = 0;
		size_t countRslt = 0;
		bool postExpectContinueInFirstRequest = false;
		bool forceExpectContinue = false;
		while(skip_offset + countRequest < size_ok_streams && 
		      this->ok_streams[skip_offset + countRequest]->direction == TcpReassemblyStream::DIRECTION_TO_DEST) {
			
			/*
			if(this->ok_streams[skip_offset + countRequest]->ack == 3805588303) {
				cout << "-- ***** --" << endl;
			}
			*/
			
			++countRequest;
			if(countRequest == 1) {
				u_char *data = this->ok_streams[skip_offset]->complete_data.getData();
				u_int32_t datalen = this->ok_streams[skip_offset]->complete_data.getDatalen();
				if(!this->ok_streams[skip_offset]->http_ok_data_complete &&
				   data && datalen > 24 && 
				   !memcmp(data, "POST ", 5) &&
				   strcasestr((char*)data, "Expect: 100-continue")) {
					postExpectContinueInFirstRequest = true;
				} else {
					break;
				}
			}
			if(countRequest == 2 && postExpectContinueInFirstRequest) {
				u_char *data = this->ok_streams[skip_offset + 1]->complete_data.getData();
				u_int32_t datalen = this->ok_streams[skip_offset + 1]->complete_data.getDatalen();
				if(data && datalen > 0 && data[0] == '{') {
					forceExpectContinue = true;
					break;
				} else {
					--countRequest;
					break;
				}
			}
		}
		if(!countRequest) {
			break;
		}
		while(skip_offset + countRequest + countRslt < size_ok_streams && 
		      this->ok_streams[skip_offset + countRequest + countRslt]->direction == TcpReassemblyStream::DIRECTION_TO_SOURCE) {
			++countRslt;
		}
		if(postExpectContinueInFirstRequest && !forceExpectContinue) {
			if(final || skip_offset + countRequest + countRslt + 2 <= size_ok_streams) {
				// OK
			} else {
				break;
			}
		} else {
			if(final || countRslt) {
				// OK
			} else {
				break;
			}
		}
		if(reassembly->enableHttpCleanupExt) {
			if(lastCountAllWithSkip == skip_offset + countRequest + countRslt) {
				if((++counterEqLastCountAllWithSkip) > 100) {
					break;
				}
			} else {
				lastCountAllWithSkip = skip_offset + countRequest + countRslt;
				counterEqLastCountAllWithSkip = 0;
			}
		}
		reassemblyData = new FILE_LINE(36008) TcpReassemblyData;
		bool existsSeparateExpectContinueData = false;
		for(size_t i = 0; i < countRequest + countRslt; i++) {
			TcpReassemblyStream *stream = this->ok_streams[skip_offset + i];
			u_char *data = stream->complete_data.getData();
			u_int32_t datalen = stream->complete_data.getDatalen();
			timeval time = stream->complete_data.getTime();
			if(data) {
				
				/*
				if(this->ok_streams[skip_offset + i]->ack == 2857364427) {
					cout << "-- ***** --" << endl;
				}
				*/
				
				if(i == countRequest - 1 &&
				   datalen > 24 && 
				   !memcmp(data, "POST ", 5) &&
				   strcasestr((char*)data, "Expect: 100-continue")) {
					if(skip_offset + countRequest + countRslt + 1 <= size_ok_streams) {
						if(this->ok_streams[skip_offset + countRequest + countRslt]->http_ok_expect_continue_data) {
							existsSeparateExpectContinueData = true;
							reassemblyData->forceAppendExpectContinue = true;
						} else if(this->ok_streams[skip_offset + countRequest + countRslt]->complete_data.getData() &&
							  this->ok_streams[skip_offset + countRequest + countRslt]->complete_data.getData()[0] == '{') {
							existsSeparateExpectContinueData = true;
						} else if(this->ok_streams[skip_offset]->http_header_length &&
							  this->ok_streams[skip_offset]->http_content_length &&
						          this->ok_streams[skip_offset + countRequest + countRslt]->complete_data.getData() &&
						          this->ok_streams[skip_offset]->complete_data.getDatalen() + 
									this->ok_streams[skip_offset + countRequest + countRslt]->complete_data.getDatalen() ==
								this->ok_streams[skip_offset]->http_header_length + this->ok_streams[skip_offset]->http_content_length + 4) {
							existsSeparateExpectContinueData = true;
							reassemblyData->forceAppendExpectContinue = true;
						}
					}
				}
				if(ENABLE_DEBUG(reassembly->getType(), _debug_data)) {
					(*_debug_stream) << endl;
					if(i == 0) {
						(*_debug_stream) << "** REQUEST **";
					} else if (i == countRequest) {
						(*_debug_stream) << "** RSLT **";
					}
					if(i == 0 || i == countRequest) {
						(*_debug_stream) << endl << endl;
					}
					(*_debug_stream)
						<< "  ack: " << this->ok_streams[skip_offset + i]->ack << "  "
						<< this->ip_src.getString() << " / " << this->port_src << " -> "
						<< this->ip_dst.getString() << " / " << this->port_dst << " "
						<< endl << endl;
					(*_debug_stream) << data << endl << endl;
				}
				if(i < countRequest) {
					reassemblyData->addRequest(data, datalen, time);
				} else {
					reassemblyData->addResponse(data, datalen, time);
				}
				this->ok_streams[skip_offset + i]->completed_finally = true;
			}
		}
		if(existsSeparateExpectContinueData &&
		   skip_offset + countRequest + countRslt + 1 <= size_ok_streams && 
		   this->ok_streams[skip_offset + countRequest + countRslt]->direction == TcpReassemblyStream::DIRECTION_TO_DEST) {
			/*
			if(countRequest == 1 && this->ok_streams[skip_offset]->http_ok &&
			   this->ok_streams[skip_offset]->http_expect_continue &&
			   this->ok_streams[skip_offset]->http_content_length &&
			   (dataItem.datalen > this->ok_streams[skip_offset]->http_content_length + 1 ||
			    dataItem.datalen < this->ok_streams[skip_offset]->http_content_length -1)) {
				this->ok_streams[skip_offset + countRequest + countRslt]->is_ok = false;
				this->ok_streams[skip_offset + countRequest + countRslt]->complete_data = NULL;
				if(this->ok_streams[skip_offset + countRequest + countRslt]->ok(true, false, 0,
												-1, -1, this->ok_streams[skip_offset], false)) {
					cout << "-- REPAIR STREAM --" << endl;
					dataItem.destroy();
					dataItem = this->ok_streams[skip_offset + countRequest + countRslt]->getCompleteData(true);
				}
			}
			*/
			u_char *data = this->ok_streams[skip_offset + countRequest + countRslt]->complete_data.getData();
			u_int32_t datalen = this->ok_streams[skip_offset + countRequest + countRslt]->complete_data.getDatalen();
			timeval time = this->ok_streams[skip_offset + countRequest + countRslt]->complete_data.getTime();
			if(data) {
				if(ENABLE_DEBUG(reassembly->getType(), _debug_data)) {
					(*_debug_stream) << endl;
					(*_debug_stream) << "** EXPECT CONTINUE **";
					(*_debug_stream) << endl << endl;
					(*_debug_stream) << "  ack: " << this->ok_streams[skip_offset + countRequest + countRslt]->ack << endl << endl;
					(*_debug_stream) << data << endl << endl;
				}
				reassemblyData->addExpectContinue(data, datalen, time);
				this->ok_streams[skip_offset + countRequest + countRslt]->completed_finally = true;
			}
			if(skip_offset + countRequest + countRslt + 2 <= size_ok_streams && 
			   this->ok_streams[skip_offset + countRequest + countRslt + 1]->direction == TcpReassemblyStream::DIRECTION_TO_SOURCE) {
				data = this->ok_streams[skip_offset + countRequest + countRslt + 1]->complete_data.getData();
				datalen = this->ok_streams[skip_offset + countRequest + countRslt + 1]->complete_data.getDatalen();
				time = this->ok_streams[skip_offset + countRequest + countRslt + 1]->complete_data.getTime();
				if(ENABLE_DEBUG(reassembly->getType(), _debug_data)) {
					(*_debug_stream) << endl;
					(*_debug_stream) << "** EXPECT CONTINUE RSLT **";
					(*_debug_stream) << endl << endl;
					(*_debug_stream) << "  ack: " << this->ok_streams[skip_offset + countRequest + countRslt + 1]->ack << endl << endl;
					(*_debug_stream) << data << endl << endl;
				}
				reassemblyData->addExpectContinueResponse(data, datalen, time);
				this->ok_streams[skip_offset + countRequest + countRslt + 1]->completed_finally = true;
			}
		}
		if(reassemblyData->isFill()) {
			if(reassembly->dataCallback) {
				reassembly->dataCallback->processData(
					this->ip_src, this->ip_dst,
					this->port_src, this->port_dst,
					reassemblyData,
					this->ethHeader, this->ethHeaderLength,
					this->handle_index, this->dlt, this->sensor_id, this->sensor_ip, this->pid,
					this->uData, this->uData2, this->uData2_last, this,
					ENABLE_DEBUG(reassembly->getType(), _debug_save) ? _debug_stream : NULL);
				reassemblyData = NULL;
			}
			if(eraseCompletedStreams) {
				while(this->ok_streams.size() && this->ok_streams[0]->completed_finally) {
					this->ok_streams[0]->is_ok = false;
					this->ok_streams[0]->clearCompleteData();
					this->ok_streams.erase(this->ok_streams.begin());
				}
			}
			skip_offset = 0;
		}
		if(reassemblyData) {
			delete reassemblyData;
		}
	}
}

TcpReassemblyLink::streamIterator TcpReassemblyLink::createIterator() {
	streamIterator iterator(this);
	return(iterator);
}

void TcpReassemblyLink::switchDirection() {
	vmIP tmp_ip = this->ip_src;
	this->ip_src = this->ip_dst;
	this->ip_dst = tmp_ip;
	vmPort tmp_port = this->port_src;
	this->port_src = this->port_dst;
	this->port_dst = tmp_port;
	bool tmp_fin = this->fin_to_source;
	this->fin_to_source = this->fin_to_dest;
	this->fin_to_dest = tmp_fin;
	map<uint32_t, TcpReassemblyStream*>::iterator iter;
	for(iter = this->queue_by_ack.begin(); iter != this->queue_by_ack.end(); iter++) {
		iter->second->direction = iter->second->direction == TcpReassemblyStream::DIRECTION_TO_DEST ?
						TcpReassemblyStream::DIRECTION_TO_SOURCE :
						TcpReassemblyStream::DIRECTION_TO_DEST;
	}
	for(iter = this->queue_flags_by_ack.begin(); iter != this->queue_flags_by_ack.end(); iter++) {
		iter->second->direction = iter->second->direction == TcpReassemblyStream::DIRECTION_TO_DEST ?
						TcpReassemblyStream::DIRECTION_TO_SOURCE :
						TcpReassemblyStream::DIRECTION_TO_DEST;
	}
	for(iter = this->queue_nul_by_ack.begin(); iter != this->queue_nul_by_ack.end(); iter++) {
		iter->second->direction = iter->second->direction == TcpReassemblyStream::DIRECTION_TO_DEST ?
						TcpReassemblyStream::DIRECTION_TO_SOURCE :
						TcpReassemblyStream::DIRECTION_TO_DEST;
	}
}

void TcpReassemblyLink::createEthHeader(u_char *packet, int dlt) {
	u_int16_t header_ip_offset;
	u_int16_t protocol;
	u_int16_t vlan;
	if(parseEtherHeader(dlt, packet,
			    NULL, NULL,
			    header_ip_offset, protocol, vlan)) {
		this->ethHeaderLength = header_ip_offset;
		if(this->ethHeaderLength > 0 && this->ethHeaderLength < 50) {
			this->ethHeader = new FILE_LINE(36009) u_char[this->ethHeaderLength];
			memcpy(this->ethHeader, packet, this->ethHeaderLength);
		}
	}
}

void TcpReassemblyLink::extCleanup(int id, bool all) {
	/*
	syslog(LOG_INFO, "TCPREASSEMBLY EXT CLEANUP %i: link %s:%u -> %s:%u size: %zd",
	       id,
	       this->ip_src.getString().c_str(), this->port_src.getPort(),
	       this->ip_dst.getString().c_str(), this->port_dst.getPort(),
	       this->queueStreams.size());
	*/
	if(reassembly->log) {
		ostringstream outStr;
		outStr << fixed 
		       << "EXT CLEANUP " << id << ": " 
		       << sqlDateTimeString(time(NULL)) << " "
		       << this->ip_src.getString() << ":" << this->port_src
		       << " -> "
		       << this->ip_dst.getString() << ":" << this->port_dst
		       << " size: "
		       << this->queueStreams.size();
		reassembly->addLog(outStr.str().c_str());
		deque<TcpReassemblyStream*>::iterator iterStream;
		for(iterStream = this->queueStreams.begin(); iterStream != this->queueStreams.end(); iterStream++) {
			TcpReassemblyStream *stream = *iterStream;
			reassembly->addLog("ack: " + intToString(stream->ack));
			map<uint32_t, TcpReassemblyStream_packet_var>::iterator iterPacketVar;
			for(iterPacketVar = stream->queuePacketVars.begin(); iterPacketVar != stream->queuePacketVars.end(); iterPacketVar++) {
				reassembly->addLog("seq: " + intToString(iterPacketVar->first));
				TcpReassemblyStream_packet_var *packetVar = &iterPacketVar->second;
				map<uint32_t, TcpReassemblyStream_packet>::iterator iterPacket;
				for(iterPacket = packetVar->queuePackets.begin(); iterPacket != packetVar->queuePackets.end(); iterPacket++) {
					ostringstream outStr;
					outStr << fixed
					       << "next seq: " 
					       << intToString(iterPacket->first)
					       << " time: " 
					       << sqlDateTimeString(iterPacket->second.time.tv_sec) << "." << setw(6) << iterPacket->second.time.tv_usec;
					reassembly->addLog(outStr.str());
					reassembly->addLog(string((char*)iterPacket->second.data, iterPacket->second.datalen));
				}
			}
		}
	}
	deque<TcpReassemblyStream*>::iterator iterStream;
	while(this->queueStreams.size() > (all ? 0 : reassembly->extCleanupStreamsLimitStreams)) {
		iterStream = this->queueStreams.begin();
		TcpReassemblyStream *stream = *iterStream;
		this->queueStreams.erase(iterStream);
		this->queue_by_ack.erase(stream->ack);
		delete stream;
	}
}

void TcpReassemblyLink::addRemainData(TcpReassemblyDataItem::eDirection direction, u_int32_t ack, u_int32_t seq, u_char *data, u_int32_t datalen, u_int32_t time_s) {
	int index = direction == TcpReassemblyDataItem::DIRECTION_TO_DEST ? 0 :
		    direction == TcpReassemblyDataItem::DIRECTION_TO_SOURCE ? 1 : -1;
	cleanupRemainData(direction, time_s);
	if(index >= 0 && data && datalen) {
		sRemainDataItem item;
		item.ack = ack;
		item.seq = seq;
		item.data = new FILE_LINE(0) u_char[datalen];
		item.datalen = datalen;
		item.time_s = time_s;
		memcpy(item.data, data, datalen);
		this->remainData[index].push_back(item);
	}
}

void TcpReassemblyLink::clearRemainData(TcpReassemblyDataItem::eDirection direction) {
	int index = direction == TcpReassemblyDataItem::DIRECTION_TO_DEST ? 0 :
		    direction == TcpReassemblyDataItem::DIRECTION_TO_SOURCE ? 1 : -1;
	for(int i = 0; i < 2; i++) {
		if(index < 0 || index == i) {
			for(unsigned j = 0; j < remainData[i].size(); j++) {
				delete [] remainData[i][j].data;
			}
			remainData[i].clear();
		}
	}
}

void TcpReassemblyLink::cleanupRemainData(TcpReassemblyDataItem::eDirection direction, u_int32_t time_s) {
	int index = direction == TcpReassemblyDataItem::DIRECTION_TO_DEST ? 0 :
		    direction == TcpReassemblyDataItem::DIRECTION_TO_SOURCE ? 1 : -1;
	if(!time_s) {
		time_s = getTimeS_rdtsc();
	}
	for(int i = 0; i < 2; i++) {
		if(index < 0 || index == i) {
			while(remainData[i].size() &&
			      remainData[i].front().time_s < time_s - reassembly->linkTimeout * 2) {
				delete remainData[i].front().data;
				remainData[i].pop_front();
			}
		}
	}
}

u_char *TcpReassemblyLink::completeRemainData(TcpReassemblyDataItem::eDirection direction, u_int32_t *rslt_datalen, u_int32_t ack, u_int32_t seq, u_char *data, u_int32_t datalen, u_int32_t skip_first_items) {
	int index = direction == TcpReassemblyDataItem::DIRECTION_TO_DEST ? 0 :
		    direction == TcpReassemblyDataItem::DIRECTION_TO_SOURCE ? 1 : -1;
	if(index >= 0) {
		*rslt_datalen = getRemainDataLength(direction, skip_first_items) + datalen;
		u_char *rslt_data = new FILE_LINE(0) u_char[*rslt_datalen];
		if(ack && seq && existsAllAckSeq(direction)) {
			map<u_int64_t, list<int> > sort_by_ack_seq;
			for(unsigned i = skip_first_items; i < remainData[index].size(); i++) {
				sort_by_ack_seq[(u_int64_t)remainData[index][i].ack << 32 | remainData[index][i].seq].push_back(i);
			}
			if(data && datalen) {
				sort_by_ack_seq[(u_int64_t)ack << 32 | seq].push_back(-1);
			}
			u_int32_t offset = 0;
			for(map<u_int64_t, list<int> >::iterator iter = sort_by_ack_seq.begin(); iter != sort_by_ack_seq.end(); iter++) {
				if(iter->second.size()) {
					for(list<int>::iterator iter_i = iter->second.begin(); iter_i != iter->second.end(); iter_i++) {
						int i = *iter_i;
						if(i >= 0) {
							memcpy(rslt_data + offset, remainData[index][i].data, remainData[index][i].datalen);
							offset += remainData[index][i].datalen;
						} else {
							memcpy(rslt_data + offset, data, datalen);
							offset += datalen;
						}
					}
				}
			}
		} else {
			u_int32_t offset = 0;
			for(unsigned i = skip_first_items; i < remainData[index].size(); i++) {
				memcpy(rslt_data + offset, remainData[index][i].data, remainData[index][i].datalen);
				offset += remainData[index][i].datalen;
			}
			if(data && datalen) {
				memcpy(rslt_data + offset, data, datalen);
				offset += datalen;
			}
		}
		return(rslt_data);
	}
	*rslt_datalen = 0;
	return(NULL);
}

u_int32_t TcpReassemblyLink::getRemainDataLength(TcpReassemblyDataItem::eDirection direction, u_int32_t skip_first_items) {
	int index = direction == TcpReassemblyDataItem::DIRECTION_TO_DEST ? 0 :
		    direction == TcpReassemblyDataItem::DIRECTION_TO_SOURCE ? 1 : -1;
	if(index >= 0) {
		u_int32_t length = 0;
		for(unsigned i = skip_first_items; i < remainData[index].size(); i++) {
			length += remainData[index][i].datalen;
		}
		return(length);
	}
	return(0);
}

u_int32_t TcpReassemblyLink::getRemainDataItems(TcpReassemblyDataItem::eDirection direction) {
	int index = direction == TcpReassemblyDataItem::DIRECTION_TO_DEST ? 0 :
		    direction == TcpReassemblyDataItem::DIRECTION_TO_SOURCE ? 1 : -1;
	if(index >= 0) {
		return(remainData[index].size());
	}
	return(0);
}

bool TcpReassemblyLink::existsRemainData(TcpReassemblyDataItem::eDirection direction) {
	int index = direction == TcpReassemblyDataItem::DIRECTION_TO_DEST ? 0 :
		    direction == TcpReassemblyDataItem::DIRECTION_TO_SOURCE ? 1 : -1;
	if(index >= 0) {
		return(remainData[index].size() > 0);
	}
	return(false);
}

bool TcpReassemblyLink::existsAllAckSeq(TcpReassemblyDataItem::eDirection direction) {
	int index = direction == TcpReassemblyDataItem::DIRECTION_TO_DEST ? 0 :
		    direction == TcpReassemblyDataItem::DIRECTION_TO_SOURCE ? 1 : -1;
	if(index >= 0) {
		for(unsigned i = 0; i < remainData[index].size(); i++) {
			if(!remainData[index][i].ack || !remainData[index][i].seq) {
				return(false);
			}
		}
		return(true);
	}
	return(false);
}

list<d_u_int32_t> *TcpReassemblyLink::getSipOffsets() {
	return(&sip_offsets);
}

void TcpReassemblyLink::joinSipOffsets() {
	u_int32_t max = 0;
	for(list<d_u_int32_t>::iterator iter = sip_offsets.begin(); iter != sip_offsets.end(); iter++) {
		max += (*iter)[1];
	}
	sip_offsets.clear();
	sip_offsets.push_back(d_u_int32_t(0, max));
}

void TcpReassemblyLink::clearCompleteStreamsData() {
	map<uint32_t, TcpReassemblyStream*>::iterator iter;
	for(iter = this->queue_by_ack.begin(); iter != this->queue_by_ack.end(); iter++) {
		iter->second->clearCompleteData();
	}
}

bool TcpReassemblyLink::checkDuplicitySeq(u_int32_t newSeq) {
	if(this->check_duplicity_seq) {
		for(unsigned i = 0; i < this->check_duplicity_seq_length; i++) {
			if(newSeq == this->check_duplicity_seq[i]) {
				return(true);
			}
		}
		for(unsigned i = this->check_duplicity_seq_length - 1; i >= 1; i--) {
			this->check_duplicity_seq[i] = this->check_duplicity_seq[i - 1];
		}
	} else {
		this->check_duplicity_seq = new FILE_LINE(0) u_int32_t[this->check_duplicity_seq_length];
		for(unsigned i = 1; i < this->check_duplicity_seq_length; i++) {
			this->check_duplicity_seq[i] = 0;
		}
	}
	this->check_duplicity_seq[0] = newSeq;
	return(false);
}


TcpReassembly::TcpReassembly(eType type) {
	this->type = type;
	this->_sync_links = 0;
	this->_sync_push = 0;
	this->_sync_cleanup = 0;
	this->enableHttpForceInit = false;
	this->enableCrazySequence = false;
	this->enableWildLink = false;
	this->ignoreTcpHandshake = false;
	this->enableIgnorePairReqResp = false;
	this->enableDestroyStreamsInComplete = false;
	this->enableAllCompleteAfterZerodataAck = false;
	this->enableValidateDataViaCheckData = false;
	this->unlimitedReassemblyAttempts = false;
	this->maxReassemblyAttempts = 50;
	this->maxStreamLength = 0;
	this->enableValidateLastQueueDataViaCheckData = false;
	this->enableStrictValidateDataViaCheckData = false;
	this->needValidateDataViaCheckData = false;
	this->simpleByAck = false;
	this->ignorePshInCheckOkData = false;
	this->smartMaxSeq = false;
	this->smartMaxSeqByPsh = false;
	this->skipZeroData = false;
	this->ignoreZeroData = false;
	this->enableCleanupThread = false;
	this->enableAutoCleanup = true;
	this->cleanupPeriod = 20;
	this->enableHttpCleanupExt = false;
	this->enablePacketThread = false;
	this->dataCallback = NULL;
	this->enablePushLock = false;
	this->enableLinkLock = false;
	this->enableSmartCompleteData = false;
	this->completeMod = 0;
	this->enableExtStat = false;
	this->extCleanupStreamsLimitStreams = 0;
	this->extCleanupStreamsLimitHeap = 0;
	this->act_time_from_header = 0;
	this->last_time = 0;
	this->last_cleanup_call_time_from_header = 0;
	this->last_erase_links_time = 0;
	this->doPrintContent = false;
	this->packetQueue = NULL;
	this->cleanupThreadHandle = 0;
	this->packetThreadHandle = 0;
	this->cleanupThreadId = 0;
	this->packetThreadId = 0;
	this->terminated = false;
	this->ignoreTerminating = false;
	memset(this->cleanupThreadPstatData, 0, sizeof(this->cleanupThreadPstatData));
	memset(this->packetThreadPstatData, 0, sizeof(this->packetThreadPstatData));
	this->_cleanupCounter = 0;
	this->linkTimeout = 2 * 60;
	this->initCleanupThreadOk = false;
	this->initPacketThreadOk = false;
	this->terminatingCleanupThread = false;
	this->terminatingPacketThread = false;
	char *log = NULL;
	switch(type) {
	case http: log = opt_tcpreassembly_http_log; break;
	case webrtc: log = opt_tcpreassembly_webrtc_log; break;
	case ssl: log = opt_tcpreassembly_ssl_log; break;
	case sip: log = opt_tcpreassembly_sip_log; break;
	case diameter: log = opt_tcpreassembly_diameter_log; break;
	}
	if(log && *log) {
		this->log = fopen(log, "at");
		if(this->log) {
			this->addLog((string(" -- start ") + sqlDateTimeString(getTimeMS()/1000)).c_str());
		}
	} else {
		this->log = NULL;
	}
	this->dumperEnable = false;
	this->dumper = NULL;
	this->dumperSync = 0;
	this->dumperFileCounter = 0;
	this->dumperPacketCounter = 0;
}

TcpReassembly::~TcpReassembly() {
	if(this->initCleanupThreadOk) {
		this->terminatingCleanupThread = true;
		pthread_join(this->cleanupThreadHandle, NULL);
	}
	if(this->initPacketThreadOk) {
		this->terminatingPacketThread = true;
		pthread_join(this->packetThreadHandle, NULL);
	}
	if(this->packetQueue) {
		delete this->packetQueue;
	}
	if(!this->enableCleanupThread || opt_pb_read_from_file[0]) {
		if(this->enableCleanupThread) {
			this->cleanup(true);
		} else {
			this->cleanup_simple(true);
		}
		this->dataCallback->writeToDb(true);
	}
	map<TcpReassemblyLink_id, TcpReassemblyLink*>::iterator iter;
	for(iter = this->links.begin(); iter != this->links.end();) {
		delete iter->second;
		this->links.erase(iter++);
	}
	if(this->log) {
		this->addLog((string(" -- stop ") + sqlDateTimeString(getTimeMS()/1000)).c_str());
		fclose(this->log);
	}
	if(this->dumper) {
		this->dumper->close();
		delete this->dumper;
	}
}

inline void *_TcpReassembly_cleanupThreadFunction(void* arg) {
	return(((TcpReassembly*)arg)->cleanupThreadFunction(arg));
}

inline void *_TcpReassembly_packetThreadFunction(void* arg) {
	return(((TcpReassembly*)arg)->packetThreadFunction(arg));
}

void TcpReassembly::prepareCleanupPstatData(int pstatDataIndex) {
	if(!this->enableCleanupThread) {
		return;
	}
	if(this->cleanupThreadPstatData[pstatDataIndex][0].cpu_total_time) {
		this->cleanupThreadPstatData[pstatDataIndex][1] = this->cleanupThreadPstatData[pstatDataIndex][0];
	}
	pstat_get_data(this->cleanupThreadId, this->cleanupThreadPstatData[pstatDataIndex]);
}

double TcpReassembly::getCleanupCpuUsagePerc(int pstatDataIndex, bool preparePstatData) {
	if(!this->enableCleanupThread) {
		return(-1);
	}
	if(preparePstatData) {
		this->prepareCleanupPstatData(pstatDataIndex);
	}
	double ucpu_usage, scpu_usage;
	if(this->cleanupThreadPstatData[pstatDataIndex][0].cpu_total_time && this->cleanupThreadPstatData[pstatDataIndex][1].cpu_total_time) {
		pstat_calc_cpu_usage_pct(
			&this->cleanupThreadPstatData[pstatDataIndex][0], &this->cleanupThreadPstatData[pstatDataIndex][1],
			&ucpu_usage, &scpu_usage);
		return(ucpu_usage + scpu_usage);
	}
	return(-1);
}

void TcpReassembly::preparePacketPstatData(int pstatDataIndex) {
	if(!this->enablePacketThread) {
		return;
	}
	if(this->packetThreadPstatData[pstatDataIndex][0].cpu_total_time) {
		this->packetThreadPstatData[pstatDataIndex][1] = this->packetThreadPstatData[pstatDataIndex][0];
	}
	pstat_get_data(this->packetThreadId, this->packetThreadPstatData[pstatDataIndex]);
}

double TcpReassembly::getPacketCpuUsagePerc(int pstatDataIndex, bool preparePstatData) {
	if(!this->enablePacketThread) {
		return(-1);
	}
	if(preparePstatData) {
		this->preparePacketPstatData(pstatDataIndex);
	}
	double ucpu_usage, scpu_usage;
	if(this->packetThreadPstatData[pstatDataIndex][0].cpu_total_time && this->packetThreadPstatData[pstatDataIndex][1].cpu_total_time) {
		pstat_calc_cpu_usage_pct(
			&this->packetThreadPstatData[pstatDataIndex][0], &this->packetThreadPstatData[pstatDataIndex][1],
			&ucpu_usage, &scpu_usage);
		return(ucpu_usage + scpu_usage);
	}
	return(-1);
}

string TcpReassembly::getCpuUsagePerc(int pstatDataIndex) {
	ostringstream outStr;
	double tPacketCpu = -1;
	double tCleanupCpu = -1;
	outStr << fixed;
	bool existsPerc = false;
	if(this->enablePacketThread) {
		tPacketCpu = this->getPacketCpuUsagePerc(pstatDataIndex, true);
		if(tPacketCpu >= 0) {
			outStr << setprecision(1) << tPacketCpu;
			existsPerc = true;
		}
	}
	if(this->enableCleanupThread) {
		tCleanupCpu = this->getCleanupCpuUsagePerc(pstatDataIndex, true);
		if(tCleanupCpu >= 0) {
			if(tPacketCpu >= 0) {
				outStr << '|';
			}
			outStr << setprecision(1) << tCleanupCpu;
			existsPerc = true;
		}
	}
	if(existsPerc) {
		outStr << '%';
	}
	size_t links_size = links.size();
	if(links_size) {
		if(existsPerc) {
			outStr << '|';
		}
		outStr << links.size() << 'l';
		extern int opt_sip_tcp_reassembly_ext_quick_mod;
		if(!(opt_sip_tcp_reassembly_ext_quick_mod & 2) && this->enableExtStat) {
			if(this->enablePushLock) {
				this->lock_push();
			}
			unsigned sumStreams = 0;
			unsigned maxStreams = 0;
			unsigned sumPackets = 0;
			unsigned maxPackets = 0;
			map<TcpReassemblyLink_id, TcpReassemblyLink*>::iterator iter_link;
			for(iter_link = this->links.begin(); iter_link != this->links.end(); iter_link++) {
				TcpReassemblyLink *link = iter_link->second;
				unsigned streamsCount = link->queue_by_ack.size();
				sumStreams += streamsCount;
				if(streamsCount > maxStreams) {
					maxStreams = streamsCount;
				}
				map<uint32_t, TcpReassemblyStream*>::iterator iter_stream;
				for(iter_stream = link->queue_by_ack.begin(); iter_stream != link->queue_by_ack.end(); iter_stream++) {
					TcpReassemblyStream *stream = iter_stream->second;
					unsigned packetsCount = stream->queuePacketVars.size();
					sumPackets += packetsCount;
					if(packetsCount > maxPackets) {
						maxPackets = packetsCount;
					}
				}
			}
			outStr << '|' << sumStreams << '/' << maxStreams << 's'
			       << '|' << sumPackets << '/' << maxPackets << 'p';
			if(this->enablePushLock) {
				this->unlock_push();
			}
		}
	}
	return(outStr.str());
}

void TcpReassembly::createCleanupThread() {
	if(!this->cleanupThreadHandle) {
		vm_pthread_create("tcp reassembly cleanup",
				  &this->cleanupThreadHandle, NULL, _TcpReassembly_cleanupThreadFunction, this, __FILE__, __LINE__);
	}
}

void TcpReassembly::createPacketThread() {
	if(!this->packetQueue) {
		this->packetQueue = new FILE_LINE(0) SafeAsyncQueue<sPacket>(50);
	}
	if(!this->packetThreadHandle) {
		vm_pthread_create("tcp reassembly packets queue",
				  &this->packetThreadHandle, NULL, _TcpReassembly_packetThreadFunction, this, __FILE__, __LINE__);
	}
}

void* TcpReassembly::cleanupThreadFunction(void*) {
	this->initCleanupThreadOk = true;
	if(verbosity) {
		ostringstream outStr;
		this->cleanupThreadId = get_unix_tid();
		outStr << "start cleanup thread t" << getTypeString()
		       << " /" << this->cleanupThreadId << endl;
		syslog(LOG_NOTICE, "%s", outStr.str().c_str());
	}
	unsigned counter = 0;
	while((!is_terminating() || this->ignoreTerminating) &&
	      !this->terminatingCleanupThread) {
		++counter;
		if(!(counter % 100)) {
			this->cleanup();
			this->dataCallback->writeToDb();
		}
		USLEEP(100000);
	}
	return(NULL);
}

void* TcpReassembly::packetThreadFunction(void*) {
	this->initPacketThreadOk = true;
	if(verbosity) {
		ostringstream outStr;
		this->packetThreadId = get_unix_tid();
		outStr << "start packet thread t" << getTypeString()
		       << " /" << this->packetThreadId << endl;
		syslog(LOG_NOTICE, "%s", outStr.str().c_str());
	}
	sPacket packet;
	#if DEBUG_DTLS_QUEUE_DEFERRED_SDP
	unsigned _c = 0;
	#endif
	while((!is_terminating() || this->ignoreTerminating) &&
	      !this->terminatingPacketThread) {
		if(packetQueue->pop(&packet)) {
			#if DEBUG_DTLS_QUEUE_DEFERRED_SDP
			++_c;
			if(_c == 2) {
				sleep(10);
			}
			#endif
			this->_push(packet.header, packet.header_ip, packet.packet,
				    packet.block_store, packet.block_store_index,
				    packet.handle_index, packet.dlt, packet.sensor_id, packet.sensor_ip, packet.pid,
				    packet.uData, packet.uData2, packet.isSip);
			if(packet.alloc_packet) {
				delete packet.header;
				delete [] packet.packet;
			}
			if(packet.block_store && packet.block_store_locked) {
				packet.block_store->unlock_packet(packet.block_store_index);
			}
		} else {
			USLEEP(1000);
		}
	}
	return(NULL);
}

void TcpReassembly::setIgnoreTerminating(bool ignoreTerminating) {
	this->ignoreTerminating = ignoreTerminating;
}

void TcpReassembly::addLog(const char *logString) {
	if(!this->log) {
		return;
	}
	fputs(logString, this->log);
	fputc('\n', this->log);
	fflush(this->log);
}

void TcpReassembly::push_tcp(pcap_pkthdr *header, iphdr2 *header_ip, u_char *packet, bool alloc_packet,
			     pcap_block_store *block_store, int block_store_index, bool block_store_locked,
			     u_int16_t handle_index, int dlt, int sensor_id, vmIP sensor_ip, sPacketInfoData pid,
			     void *uData, void *uData2, bool isSip) {
 
	if(dumperEnable) {
		bool port_ok = false;
		if(dumperPorts.size()) {
			tcphdr2 *header_tcp = (tcphdr2*)((u_char*)header_ip + header_ip->get_hdr_size());
			for(int i = 0; i < 2 && !port_ok; i++) {
				u_int16_t port = i == 0 ? header_tcp->get_source() : header_tcp->get_dest();
				list<u_int16_t>::iterator port_iter;
				if((port_iter = std::lower_bound(dumperPorts.begin(), dumperPorts.end(), port)) != dumperPorts.end() &&
				   *port_iter == port) {
					port_ok = true;
					break;
				}
			}
		} else {
			port_ok = true;
		}
		if(port_ok) {
			__SYNC_LOCK(dumperSync);
			if(!dumper || (dumperPacketCounter && !(dumperPacketCounter % 1000000))) {
				if(dumper) {
					dumper->close();
					delete dumper;
				}
				dumper = new FILE_LINE(0) PcapDumper(PcapDumper::na, NULL);
				dumper->setEnableAsyncWrite(false);
				dumper->setTypeCompress(FileZipHandler::compress_na);
				string dumpFileName = dumperFileName + "-" + intToString(++dumperFileCounter) + ".pcap";
				if(dumper->open(tsf_na, dumpFileName.c_str(), dlt)) {
					dumper->dump(header, packet, dlt, true);
					++dumperPacketCounter;
				} else {
					dumper->close();
					delete dumper;
					dumper = NULL;
					dumperEnable = false;
				}
			} else {
				dumper->dump(header, packet, dlt, true);
				++dumperPacketCounter;
				if(!(dumperPacketCounter % 1000)) {
					dumper->flush();
				}
			}
			__SYNC_UNLOCK(dumperSync);
		}
	}
 
	if(_ENABLE_DEBUG(type, true) && !_debug_stream) {
		if(sverb.tcpreassembly_debug_file) {
			_debug_stream = new std::ofstream((sverb.tcpreassembly_debug_file + (" " + sqlDateTimeString(time(NULL)))).c_str(), 
							  ios_base::out | ios_base::app);
		} else {
			_debug_stream = &cout;
		}
	}
 
	if((debug_limit_counter && debug_counter > debug_limit_counter) ||
	   !(type == ssl || 
	     type == sip ||
	     type == diameter ||
	     this->check_ip(header_ip->get_saddr(), header_ip->get_daddr()))) {
		if(block_store && block_store_locked) {
			block_store->unlock_packet(block_store_index);
		}
		return;
	}
	if(this->enablePacketThread) {
		if(!alloc_packet &&
		   block_store && !block_store_locked) {
			block_store->lock_packet(block_store_index, 2 /*pb lock flag*/);
			block_store_locked = true;
		}
		sPacket _packet;
		_packet.header = header;
		_packet.header_ip = header_ip;
		_packet.packet = packet;
		_packet.alloc_packet = alloc_packet;
		_packet.block_store = block_store;
		_packet.block_store_index = block_store_index;
		_packet.block_store_locked = block_store_locked;
		_packet.handle_index = handle_index;
		_packet.dlt = dlt;
		_packet.sensor_id = sensor_id;
		_packet.sensor_ip = sensor_ip;
		_packet.pid = pid;
		_packet.uData = uData;
		_packet.uData2 = uData2;
		_packet.isSip = isSip;
		this->packetQueue->push(_packet);
	} else {
		if(this->enablePushLock) {
			this->lock_push();
		}
		this->_push(header, header_ip, packet,
			    block_store, block_store_index,
			    handle_index, dlt, sensor_id, sensor_ip, pid,
			    uData, uData2, isSip);
		if(this->enablePushLock) {
			this->unlock_push();
		}
		if(alloc_packet) {
			delete header;
			delete [] packet;
		}
		if(block_store && block_store_locked) {
			block_store->unlock_packet(block_store_index);
		}
	}
}

bool TcpReassembly::checkOkData(u_char * data, u_int32_t datalen, int8_t strict_mode, list<d_u_int32_t> *sip_offsets, u_int32_t *datalen_used) {
	switch(type) {
	case http:
		return(true);
	case webrtc:
		if(checkOkWebrtcHttpData(data, datalen) || 
		   checkOkWebrtcData(data, datalen)) {
			if(datalen_used) {
				*datalen_used = datalen;
			}
			return(true);
		}
		break;
	case ssl:
		if(checkOkSslData(data, datalen)) {
			if(datalen_used) {
				*datalen_used = datalen;
			}
			return(true);
		}
		break;
	case sip:
		sip_offsets->clear();
		if(check_websocket(data, datalen)) {
			sip_offsets->push_back(d_u_int32_t(0, datalen));
			if(datalen_used) {
				*datalen_used = datalen;
			}
			return(true);
		} else if(checkOkSipData(data, datalen, strict_mode, sip_offsets, datalen_used)) {
			return(true);
		}
		break;
	case diameter:
		if(checkOkDiameter(data, datalen)) {
			if(datalen_used) {
				*datalen_used = datalen;
			}
			return(true);
		}
		break;
	}
	return(false);
}

void TcpReassembly::enableDumper(const char *fileName, const char *ports) {
	dumperEnable = true;
	dumperFileName = fileName;
	if(ports && *ports) {
		vector<string> ports_vect_str = split(ports, 'x');
		if(ports_vect_str.size()) {
			for(unsigned i = 0; i < ports_vect_str.size(); i++) {
				dumperPorts.push_back(atoi(ports_vect_str[i].c_str()));
				dumperPorts.sort();
			}
		}
	}
}
 
void TcpReassembly::_push(pcap_pkthdr *header, iphdr2 *header_ip, u_char *packet,
			  pcap_block_store *block_store, int block_store_index,
			  u_int16_t handle_index, int dlt, int sensor_id, vmIP sensor_ip, sPacketInfoData pid,
			  void *uData, void *uData2, bool isSip) {

	tcphdr2 *header_tcp_pointer;
	tcphdr2 header_tcp;
	u_char *data;
	u_int32_t datalen;
	u_int32_t datacaplen;
	
	header_tcp_pointer = (tcphdr2*)((u_char*)header_ip + header_ip->get_hdr_size());
	data = (u_char*)header_tcp_pointer + (header_tcp_pointer->doff << 2);
	
	if((data - packet) > header->caplen) {
		return;
	}
	
	datacaplen = header->caplen - ((u_char*)data - packet);
	datalen = datacaplen;
	if(header->len > header->caplen) {
		datalen += (header->len - header->caplen);
	}
	u_int8_t proto;
	u_int32_t tcp_data_length = header_ip->get_tot_len() - 
				    header_ip->get_hdr_size(&proto) - header_ip->get_footer_size(proto) - 
				    header_tcp_pointer->doff * 4;
	if(datalen > tcp_data_length) {
		datalen = tcp_data_length;
	}
	if(datacaplen > tcp_data_length) {
		datacaplen = tcp_data_length;
	}
	
	header_tcp = *header_tcp_pointer;
	header_tcp.seq = htonl(header_tcp.seq);
	header_tcp.ack_seq = htonl(header_tcp.ack_seq);
	u_int32_t next_seq = header_tcp.seq + datalen;
	
	if(sverb.tcp_debug_port) {
		if((int)header_tcp.get_source() != sverb.tcp_debug_port && (int)header_tcp.get_dest() != sverb.tcp_debug_port) {
			return;
		}
	}
	vmIP *tcp_debug_ip = (vmIP*)sverb.tcp_debug_ip;
	if(tcp_debug_ip->isSet()) {
		if(header_ip->get_saddr() != *tcp_debug_ip && header_ip->get_daddr() != *tcp_debug_ip) {
			return;
		}
	}
	
	if(debug_seq && header_tcp.seq == debug_seq) {
		cout << " -- XXX DEBUG SEQ XXX" << endl;
	}

	if(ENABLE_DEBUG(type, _debug_packet)) {
		string _data;
		if(datalen) {
			char *__data = new FILE_LINE(36014) char[datalen + 1];
			memcpy_heapsafe(__data, __data,
					data, NULL,
					datalen, 
					__FILE__, __LINE__);
			__data[datalen] = 0;
			_data = __data;
			delete [] __data;
			_data = _data.substr(0, 5000);
			for(size_t i = 0; i < _data.length(); i++) {
				if(_data[i] == 13 || _data[i] == 10) {
					_data[i] = '\\';
				}
				if(_data[i] < 32) {
					_data.resize(i);
				}
			}
		}
		(*_debug_stream)
			<< fixed
			<< sqlDateTimeString(header->ts.tv_sec) << "." << setw(6) << header->ts.tv_usec
			<< " : "
			<< setw(15) << header_ip->get_saddr().getString() << "/" << setw(6) << header_tcp.get_source()
			<< " -> " 
			<< setw(15) << header_ip->get_daddr().getString() << "/" << setw(6) << header_tcp.get_dest()
			<< "   "
			<< (header_tcp.flags_bit.fin ? 'F' : '-')
			<< (header_tcp.flags_bit.syn ? 'S' : '-') 
			<< (header_tcp.flags_bit.rst ? 'R' : '-')
			<< (header_tcp.flags_bit.psh ? 'P' : '-')
			<< (header_tcp.flags_bit.ack ? 'A' : '-')
			<< (header_tcp.flags_bit.urg ? 'U' : '-')
			<< "  "
			<< " len: " << setw(5) << datalen
			<< " seq: " << setw(12) << header_tcp.seq
			<< " next seq: " << setw(12) << next_seq
			<< " ack: " << setw(12) << header_tcp.ack_seq
			<< " data: " << _data
			<< endl;
		++debug_counter;
		
		/*
		if(strstr((char*)data, "CHANNEL_CREATE")) {
			cout << "-- ***** --" << endl;
		}
		*/
		
	}
	
	this->last_time = getTimeMS();
	this->act_time_from_header = getTimeMS(header);
	
	TcpReassemblyLink *link = NULL;
	map<TcpReassemblyLink_id, TcpReassemblyLink*>::iterator iter;
	TcpReassemblyStream::eDirection direction = TcpReassemblyStream::DIRECTION_TO_DEST;
	TcpReassemblyLink_id id(header_ip->get_saddr(), header_ip->get_daddr(), header_tcp.get_source(), header_tcp.get_dest());
	TcpReassemblyLink_id idr(header_ip->get_daddr(), header_ip->get_saddr(), header_tcp.get_dest(), header_tcp.get_source());
	if(this->enableCleanupThread || this->enableLinkLock) {
		this->lock_links();
	}
	if(this->last_time > this->last_erase_links_time + 5000) {
		for(iter = this->links.begin(); iter != this->links.end();) {
			if(iter->second->_erase) {
				delete iter->second;
				this->links.erase(iter++);
			} else {
				iter++;
			}
		}
		this->last_erase_links_time = this->last_time;
	}
	iter = this->links.find(id);
	if(iter != this->links.end()) {
		link = iter->second;
	} else {
		iter = this->links.find(idr);
		if(iter != this->links.end()) {
			link = iter->second;
			direction = TcpReassemblyStream::DIRECTION_TO_SOURCE;
		}
	}
	bool queue_locked = false;
	bool create_new_link = false;
	if(link) {
		if(this->enableCleanupThread/* || this->enableLinkLock*/) { // not for this->enableLinkLock -> lock colision
			link->lock_queue();
			queue_locked = true;
		}
		if(link->_erase) {
			delete link;
			this->links.erase(iter);
			link = NULL;
		}
	}
	if(link) {
		if(!this->enableCrazySequence &&
		   link->state == TcpReassemblyLink::STATE_SYN_SENT &&
		   this->enableHttpForceInit &&
		   direction == TcpReassemblyStream::DIRECTION_TO_DEST &&
		   ((datalen > 5 && !memcmp(data, "POST ", 5)) ||
		    (datalen > 4 && !memcmp(data, "GET ", 4)))) {
			link->state = TcpReassemblyLink::STATE_SYN_FORCE_OK;
		}
		link->uData2_last = uData2;
	} else {
		if(!this->enableCrazySequence &&
		   header_tcp.flags_bit.syn && !header_tcp.flags_bit.ack) {
			if(this->check_dest_ip_port(header_ip->get_saddr(), header_tcp.get_source(), header_ip->get_daddr(), header_tcp.get_dest())) {
				if(ENABLE_DEBUG(type, _debug_packet)) {
					(*_debug_stream) <<
						fixed
						<< endl
						<< " ** NEW LINK " 
						<< getTypeString(true)
						<< " NORMAL: " 
						<< setw(15) << header_ip->get_saddr().getString() << "/" << setw(6) << header_tcp.get_source()
						<< " -> " 
						<< setw(15) << header_ip->get_daddr().getString() << "/" << setw(6) << header_tcp.get_dest()
						<< endl;
				}
				link = new FILE_LINE(36011) TcpReassemblyLink(this, header_ip->get_saddr(), header_ip->get_daddr(), header_tcp.get_source(), header_tcp.get_dest(),
									      packet, header_ip,
									      handle_index, dlt, sensor_id, sensor_ip, pid,
									      uData, uData2);
				this->links[id] = link;
				create_new_link = true;
			}
		} else if(!this->enableCrazySequence && this->enableWildLink) {
			if(type != ssl ||
			   (opt_ssl_reassembly_ipport_reverse_enable ||
			    this->check_dest_ip_port(header_ip->get_saddr(), header_tcp.get_source(), header_ip->get_daddr(), header_tcp.get_dest()))) {
				if(ENABLE_DEBUG(type, _debug_packet)) {
					(*_debug_stream)
						<< fixed
						<< " ** NEW LINK "
						<< getTypeString(true)
						<< " FORCE: " 
						<< setw(15) << header_ip->get_saddr().getString() << "/" << setw(6) << header_tcp.get_source()
						<< " -> " 
						<< setw(15) << header_ip->get_daddr().getString() << "/" << setw(6) << header_tcp.get_dest()
						<< endl;
				}
				link = new FILE_LINE(36012) TcpReassemblyLink(this, header_ip->get_saddr(), header_ip->get_daddr(), header_tcp.get_source(), header_tcp.get_dest(),
									      packet, header_ip,
									      handle_index, dlt, sensor_id, sensor_ip, pid,
									      uData, uData2);
				this->links[id] = link;
				create_new_link = true;
				link->state = TcpReassemblyLink::STATE_SYN_FORCE_OK;
				link->forceOk = true;
			}
		} else if(this->enableCrazySequence ||
			  (this->enableHttpForceInit &&
			   ((datalen > 5 && !memcmp(data, "POST ", 5)) ||
			    (datalen > 4 && !memcmp(data, "GET ", 4))))) {
			if(ENABLE_DEBUG(type, _debug_packet)) {
				(*_debug_stream)
					<< fixed
					<< " ** NEW LINK "
					<< getTypeString(true)
					<< " CRAZY: " 
					<< setw(15) << header_ip->get_saddr().getString() << "/" << setw(6) << header_tcp.get_source()
					<< " -> " 
					<< setw(15) << header_ip->get_daddr().getString() << "/" << setw(6) << header_tcp.get_dest()
					<< endl;
			}
			link = new FILE_LINE(36013) TcpReassemblyLink(this, header_ip->get_saddr(), header_ip->get_daddr(), header_tcp.get_source(), header_tcp.get_dest(),
								      packet, header_ip,
								      handle_index, dlt, sensor_id, sensor_ip, pid,
								      uData, uData2);
			this->links[id] = link;
			create_new_link = true;
			if(this->enableCrazySequence) {
				link->state = TcpReassemblyLink::STATE_CRAZY;
			} else {
				link->state = TcpReassemblyLink::STATE_SYN_FORCE_OK;
				link->first_seq_to_dest = header_tcp.seq;
			}
		}
	}
	if(link) {
		if(this->enableCleanupThread || this->enableLinkLock) {
			if(!queue_locked) {
				link->lock_queue();
			}
			this->unlock_links();
		}
		link->push(direction, header->ts, header_tcp, 
			   data, datalen, datacaplen,
			   block_store, block_store_index,
			   isSip);
		if(this->enableCleanupThread || this->enableLinkLock) {
			link->unlock_queue();
		} else {
			if(this->getType() == TcpReassembly::ssl &&
			   (link->state == TcpReassemblyLink::STATE_RESET || 
			    link->state == TcpReassemblyLink::STATE_CLOSE || 
			    link->state == TcpReassemblyLink::STATE_CLOSED)) {
				extern bool opt_ssl_destroy_tcp_link_on_rst;
				if(opt_ssl_destroy_tcp_link_on_rst) {
					delete link;
					if(create_new_link) {
						this->links.erase(id);
					} else {
						this->links.erase(iter);
					}
				}
			}
		}
	} else if(this->enableCleanupThread || this->enableLinkLock) {
		this->unlock_links();
	}

	if(!this->enableCleanupThread && this->enableAutoCleanup && !this->_sync_cleanup) {
		this->cleanup_simple(false, this->enableLinkLock);
	}
}

void TcpReassembly::cleanup(bool all) {
	if(all && ENABLE_DEBUG(type, _debug_cleanup)) {
		(*_debug_stream) << "cleanup all " << getTypeString() << endl;
	}
	list<TcpReassemblyLink*> links;
	map<TcpReassemblyLink_id, TcpReassemblyLink*>::iterator iter;
	this->lock_links();
	if(all && opt_pb_read_from_file[0] && ENABLE_DEBUG(type, _debug_cleanup)) {
		(*_debug_stream)
			<< "COUNT REST LINKS " 
			<< getTypeString(true) << ": "
			<< this->links.size() << endl;
	}
	for(iter = this->links.begin(); iter != this->links.end(); iter++) {
		if(!iter->second->_erase) {
			links.push_back(iter->second);
		}
	}
	this->unlock_links();
	
	list<TcpReassemblyLink*>::iterator iter_links;
	for(iter_links = links.begin(); iter_links != links.end(); iter_links++) {
		TcpReassemblyLink *link = *iter_links;
		u_int64_t act_time = this->act_time_from_header + getTimeMS() - this->last_time;
		link->lock_queue();
		
		if(type == http  &&
		   !enableHttpCleanupExt &&
		   link && link->queue_by_ack.size() > 500) {
			if(this->isActiveLog() || ENABLE_DEBUG(type, _debug_cleanup)) {
				ostringstream outStr;
				outStr << fixed 
				       << "cleanup " 
				       << getTypeString()
				       << " - remove link "
				       << "(too much ack - " << link->queue_by_ack.size() << ") "
				       << setw(15) << link->ip_src.getString() << "/" << setw(6) << link->port_src
				       << " -> "
				       << setw(15) << link->ip_dst.getString() << "/" << setw(6) << link->port_dst;
				if(ENABLE_DEBUG(type, _debug_cleanup)) {
					(*_debug_stream) << outStr.str() << endl;
				}
				this->addLog(outStr.str().c_str());
			}
			link->_erase = true;
			link->unlock_queue();
			continue;
		}
		
		if(act_time > link->last_packet_at_from_header + (linkTimeout/20) * 1000) {
			link->cleanup(act_time);
		}
		bool final = link->last_packet_at_from_header &&
			     act_time > link->last_packet_at_from_header + linkTimeout * 1000;
		if((all || final ||
		    (link->last_packet_at_from_header &&
		     act_time > link->last_packet_at_from_header + (linkTimeout/20) * 1000 &&
		     link->last_packet_at_from_header > link->last_packet_process_cleanup_at)) &&
		   (link->link_is_ok < 2 || this->enableCleanupThread)) {
		 
			/*
			if(link->port_src == 53442 || link->port_dst == 53442) {
				cout << " -- ***** -- ";
			}
			*/
		 
			link->last_packet_process_cleanup_at = link->last_packet_at_from_header;
			bool _debug_output = false;
			if(!link->exists_data) {
				if(ENABLE_DEBUG(type, _debug_rslt)) {
					(*_debug_stream) << "RSLT: EMPTY";
					_debug_output = true;
				}
			} else {
				int countDataStream = link->okQueue(all || final ? 2 : 1, 0, 0, 0, false, ENABLE_DEBUG(type, _debug_check_ok));
				if(countDataStream > 0) {
					link->complete(all || final, true);
					link->link_is_ok = 2;
				}
				if(ENABLE_DEBUG(type, _debug_rslt)) {
					if(countDataStream < 0) {
						(*_debug_stream) << (countDataStream == -1 ? "RSLT: MISSING REQUEST" :
								    (countDataStream == -2 ? "RSLT: DIRECTION NOT CONFIRMED" :
											     "RSLT: EMPTY OR OTHER ERROR"));
					}
					else if(countDataStream > 1) {
						(*_debug_stream) << "RSLT: OK (" << countDataStream << ")";
					} else if(countDataStream > 0) {
						(*_debug_stream) << "RSLT: ONLY REQUEST (" << countDataStream << ")";
					} else {
						if(countDataStream == 0) {
							if(!link->queueStreams.size()) {
								(*_debug_stream) << "EMPTY";
							} else {
								(*_debug_stream) << "ERROR";
							}
						} else {
							(*_debug_stream) << "ERROR " << countDataStream;
						}
					}
					_debug_output = true;
				}
			}
			if(_debug_output) {
				if(ENABLE_DEBUG(type, _debug_packet)) {
					(*_debug_stream)
						<< " clean "
						<< link->ip_src.getString() << " / " << link->port_src
						<< " -> "
						<< link->ip_dst.getString() << " / " << link->port_dst;
				}
				(*_debug_stream) << endl;
			}
		}
		if(all || final ||
		   (link->queue_by_ack.size() && !link->existsFinallyUncompletedDataStream())) {
			link->_erase = 1;
		}
		link->unlock_queue();
	}
	
	if(this->doPrintContent) {
		if(ENABLE_DEBUG(type, _debug_print_content)) {
			this->printContent();
		}
		this->doPrintContent = false;
	}
	if(ENABLE_DEBUG(type, _debug_print_content_summary)) {
		this->printContentSummary();
	}
}

void TcpReassembly::cleanup_simple(bool all, bool lock) {
 
	if(lock) lock_cleanup();
		
	if(!all) {
		if(this->act_time_from_header < this->last_cleanup_call_time_from_header + cleanupPeriod * 1000) {
			if(lock) unlock_cleanup();
			return;
		}
		this->last_cleanup_call_time_from_header = this->act_time_from_header;
	}
	
	u_int64_t start_at = 0;
	if(ENABLE_CLEANUP_LOG(getType())) {
		start_at = getTimeMS_rdtsc();
	}
 
	if(all && ENABLE_DEBUG(type, _debug_cleanup)) {
		(*_debug_stream) << "cleanup simple all " << getTypeString() << endl;
	}
	if(all && opt_pb_read_from_file[0] && ENABLE_DEBUG(type, _debug_cleanup)) {
		(*_debug_stream)
			<< "COUNT REST LINKS " 
			<< getTypeString(true) << ": "
			<< this->links.size() << endl;
	}
	if(simpleByAck) {
		u_int64_t act_time = this->act_time_from_header;
		map<TcpReassemblyLink_id, TcpReassemblyLink*>::iterator iterLink;
		if(lock) lock_links();
		for(iterLink = this->links.begin(); iterLink != this->links.end(); ) {
			TcpReassemblyLink *link = iterLink->second;
			if(all || act_time > link->last_packet_at_from_header + linkTimeout * 1000) {
				if(lock) link->lock_queue();
				delete link;
				this->links.erase(iterLink++);
			} else {
				if(lock) link->lock_queue();
				extern cBuffersControl buffersControl;
				if(this->extCleanupStreamsLimitStreams &&
				   link->queueStreams.size() > this->extCleanupStreamsLimitStreams &&
				   (!this->extCleanupStreamsLimitHeap || buffersControl.getPerc_pb_used() > this->extCleanupStreamsLimitHeap)) {
					link->extCleanup(1, true);
				} else {
					deque<TcpReassemblyStream*>::iterator iterStream;
					for(iterStream = link->queueStreams.begin(); iterStream != link->queueStreams.end();) {
						TcpReassemblyStream *stream = *iterStream;
						if(act_time > stream->last_packet_at_from_header + linkTimeout * 1000) {
							iterStream = link->queueStreams.erase(iterStream);
							link->queue_by_ack.erase(stream->ack);
							delete stream;
						} else if(maxStreamLength > 0 && stream->queuePacketVars.size() > (unsigned)maxStreamLength) {
							map<uint32_t, TcpReassemblyStream_packet_var>::iterator iterPacketVars;
							for(iterPacketVars = stream->queuePacketVars.begin(); iterPacketVars != stream->queuePacketVars.end(); ) {
								if(act_time > iterPacketVars->second.last_packet_at_from_header + linkTimeout * 1000) {
									stream->queuePacketVars.erase(iterPacketVars++);
								} else {
									++iterPacketVars;
								}
							}
							if(!stream->queuePacketVars.size()) {
								iterStream = link->queueStreams.erase(iterStream);
								link->queue_by_ack.erase(stream->ack);
								delete stream;
							} else {
								++iterStream;
							}
						} else {
							++iterStream;
						}
					}
				}
				iterLink++;
				if(lock) link->unlock_queue();
			}
		}
		if(lock) unlock_links();
	} else {
		size_t counter = 0;
		u_int64_t time_correction = 0;
		map<TcpReassemblyLink_id, TcpReassemblyLink*>::iterator iter;
		for(iter = this->links.begin(); iter != this->links.end(); ) {
			++counter;
			if(!(counter % 1000)) {
				time_correction = getTimeMS() - this->last_time;
			}
			u_int64_t act_time = this->act_time_from_header + time_correction;
			TcpReassemblyLink *link = iter->second;
			bool final = link->last_packet_at_from_header &&
				     act_time > link->last_packet_at_from_header + linkTimeout * 1000;
			if(link->queueStreams.size() &&
			   (all || final ||
			    (link->last_packet_at_from_header &&
			     act_time > link->last_packet_at_from_header + 5 * 1000 &&
			     link->last_packet_at_from_header > link->last_packet_process_cleanup_at))) {
				int countDataStream = link->okQueue(all || final ? 2 : 1, 0, 0, 0, false, ENABLE_DEBUG(this->type, _debug_check_ok));
				if(ENABLE_DEBUG(this->getType(), _debug_check_ok)) {
					(*_debug_stream) << endl;
				}
				if(ENABLE_DEBUG(this->getType(), _debug_rslt)) {
					(*_debug_stream) << " -- RSLT: ";
					if(countDataStream == 0) {
						if(!link->queueStreams.size()) {
							(*_debug_stream) << "EMPTY";
						} else {
							(*_debug_stream) << "ERROR ";
						}
					} else if(countDataStream < 0) {
						(*_debug_stream) << "empty";
					} else {
						(*_debug_stream) << "OK (" << countDataStream << ")";
					}
					(*_debug_stream) << " " << link->port_src << " / " << link->port_dst;
					(*_debug_stream) << endl;
				}
				if(countDataStream > 0) {
					link->complete(all || final, true);
				}
			}
			if(all || final) {
				delete link;
				link = NULL;
				this->links.erase(iter++);
			} else {
				iter++;
			}
		}
	}
	
	if(this->doPrintContent) {
		if(ENABLE_DEBUG(type, _debug_print_content)) {
			this->printContent();
		}
		this->doPrintContent = false;
	}
	if(ENABLE_DEBUG(type, _debug_print_content_summary)) {
		this->printContentSummary();
	}
	
	if(lock) unlock_cleanup();
	
	if(ENABLE_CLEANUP_LOG(getType())) {
		syslog(LOG_NOTICE, "TcpReassembly::cleanup_simple (%i ms)", (int)(getTimeMS_rdtsc() - start_at));
	}
	
}

/*
bool TcpReassembly::enableStop() {
	return(getTimeMS() - this->last_time > 20 * 1000);
}
*/

void TcpReassembly::printContent() {
	std::ostream *__debug_stream = _debug_stream ? _debug_stream : &cout;
	map<TcpReassemblyLink_id, TcpReassemblyLink*>::iterator iter;
	int counter = 0;
	for(iter = this->links.begin(); iter != this->links.end(); iter++) {
		(*__debug_stream)
			<< fixed << setw(3) << (++counter) << "   "
			<< endl;
		iter->second->printContent(1);
	}
}

void TcpReassembly::printContentSummary() {
	std::ostream *__debug_stream = _debug_stream ? _debug_stream : &cout;
	(*__debug_stream) << "LINKS " << getTypeString(true) << ": " << this->links.size() << endl;
	if(this->dataCallback) {
		this->dataCallback->printContentSummary();
	}
}
