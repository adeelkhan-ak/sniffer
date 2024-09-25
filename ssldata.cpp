#include <iomanip>

#include "ssldata.h"
#include "sniff_proc_class.h"
#include "sql_db.h"
#include "ssl_dssl.h"
#include "websocket.h"
#include "config_param.h"

#ifdef FREEBSD
#include <sys/socket.h>
#endif


using namespace std;

extern int opt_enable_ssl;
extern bool opt_ssl_enable_redirection_unencrypted_sip_content;

extern int check_sip20(char *data, unsigned long len, ParsePacket::ppContentsX *parseContents, bool isTcp);

#if defined(HAVE_LIBGNUTLS) and defined(HAVE_SSL_WS)
extern void decrypt_ssl(vector<string> *rslt_decrypt, char *data, unsigned int datalen, unsigned int saddr, unsigned int daddr, int sport, int dport);
#endif

extern map<vmIPport, string> ssl_ipport;
extern map<vmIPmask_port, string> ssl_netport;
extern bool opt_ssl_ipport_reverse_enable;
extern PreProcessPacket *preProcessPacket[PreProcessPacket::ppt_end_base];

extern char *ssl_portmatrix;

static volatile int ssl_ipport_sync = 0;


SslData::SslData() {
	this->counterProcessData = 0;
	this->counterDecryptData = 0;
}

SslData::~SslData() {
}

void SslData::processData(vmIP ip_src, vmIP ip_dst,
			  vmPort port_src, vmPort port_dst,
			  TcpReassemblyData *data,
			  u_char *ethHeader, u_int32_t ethHeaderLength,
			  u_int16_t handle_index, int dlt, int sensor_id, vmIP sensor_ip, sPacketInfoData pid,
			  void */*uData*/, void */*uData2*/, void */*uData2_last*/, TcpReassemblyLink *reassemblyLink,
			  std::ostream *debugStream) {
	++this->counterProcessData;
	if(debugStream) {
		(*debugStream) << "### SslData::processData " << this->counterProcessData << endl;
	}
	for(size_t i_data = 0; i_data < data->data.size(); i_data++) {
		TcpReassemblyDataItem *dataItem = &data->data[i_data];
		if(!dataItem->getData()) {
			continue;
		}
		vmIP _ip_src = dataItem->getDirection() == TcpReassemblyDataItem::DIRECTION_TO_DEST ? ip_src : ip_dst;
		vmIP _ip_dst = dataItem->getDirection() == TcpReassemblyDataItem::DIRECTION_TO_DEST ? ip_dst : ip_src;
		vmPort _port_src = dataItem->getDirection() == TcpReassemblyDataItem::DIRECTION_TO_DEST ? port_src : port_dst;
		vmPort _port_dst = dataItem->getDirection() == TcpReassemblyDataItem::DIRECTION_TO_DEST ? port_dst : port_src;
		if(opt_ssl_enable_redirection_unencrypted_sip_content) {
			u_char *_data = dataItem->getData();
			unsigned int _datalen = dataItem->getDatalen();
			while(_datalen >= 1 && (_data[0] == '\r' || _data[0] == '\n')) {
				_data += 1;
				_datalen -= 1;
			}
			if(_datalen > 0 &&
			   TcpReassemblySip::checkSip(_data, _datalen, TcpReassemblySip::_chssm_na)) {
				pcap_pkthdr *tcpHeader;
				u_char *tcpPacket;
				createSimpleTcpDataPacket(ethHeaderLength, &tcpHeader,  &tcpPacket,
							  ethHeader, _data, _datalen, 0,
							  _ip_src, _ip_dst, _port_src, _port_dst,
							  dataItem->getSeq(), dataItem->getAck(), 0,
							  dataItem->getTime().tv_sec, dataItem->getTime().tv_usec, dlt);
				unsigned iphdrSize = ((iphdr2*)(tcpPacket + ethHeaderLength))->get_hdr_size();
				unsigned dataOffset = ethHeaderLength + 
						      iphdrSize +
						      ((tcphdr2*)(tcpPacket + ethHeaderLength + iphdrSize))->doff * 4;
				packet_flags pflags;
				pflags.init();
				pflags.tcp = 2;
				if(opt_t2_boost_direct_rtp) {
					sHeaderPacketPQout hp(tcpHeader, tcpPacket,
							      dlt, sensor_id, sensor_ip);
					preProcessPacket[PreProcessPacket::ppt_detach_x]->push_packet(
							ethHeaderLength, 0xFFFF,
							dataOffset, _datalen,
							_port_src, _port_dst,
							pflags,
							&hp,
							handle_index);
				} else {
					preProcessPacket[PreProcessPacket::ppt_detach]->push_packet(
							#if USE_PACKET_NUMBER
							0, 
							#endif
							_ip_src, _port_src, _ip_dst, _port_dst, 
							_datalen, dataOffset,
							handle_index, tcpHeader, tcpPacket, _t_packet_alloc_header_std, 
							pflags, (iphdr2*)(tcpPacket + ethHeaderLength), NULL,
							NULL, 0, dlt, sensor_id, sensor_ip, pid,
							false);
				}
				continue;
			}
		}
		if(reassemblyLink->checkDuplicitySeq(dataItem->getSeq())) {
			if(debugStream) {
				(*debugStream) << "SKIP SEQ " << dataItem->getSeq() << endl;
			}
			continue;
		}
		if(debugStream) {
			(*debugStream)
				<< "###"
				<< fixed
				<< setw(15) << ip_src.getString()
				<< " / "
				<< setw(5) << port_src
				<< (dataItem->getDirection() == TcpReassemblyDataItem::DIRECTION_TO_DEST ? " --> " : " <-- ")
				<< setw(15) << ip_dst.getString()
				<< " / "
				<< setw(5) << port_dst
				<< "  len: " << setw(4) << dataItem->getDatalen();
			u_int32_t ack = dataItem->getAck();
			u_int32_t seq = dataItem->getSeq();
			if(ack) {
				(*debugStream) << "  ack: " << setw(5) << ack;
			}
			if(seq) {
				(*debugStream) << "  seq: " << setw(5) << seq;
			}
			(*debugStream) << endl;
		}
		vector<string> rslt_decrypt;
		bool ok_first_ssl_header = false;
		u_char *ssl_data = NULL;
		u_int32_t ssl_datalen;
		bool alloc_ssl_data = false;
		bool exists_remain_data = reassemblyLink->existsRemainData(dataItem->getDirection());
		bool ignore_remain_data = false;
		if(exists_remain_data) {
			reassemblyLink->cleanupRemainData(dataItem->getDirection(), dataItem->getTime().tv_sec);
			exists_remain_data = reassemblyLink->existsRemainData(dataItem->getDirection());
			SslHeader header(dataItem->getData(), dataItem->getDatalen());
			if(header.isOk() && header.length && (u_int32_t)header.length + header.getDataOffsetLength() <= dataItem->getDatalen()) {
				ok_first_ssl_header = true;
				ignore_remain_data = true;
				ssl_data = dataItem->getData();
				ssl_datalen = dataItem->getDatalen();
				if(debugStream) {
					(*debugStream) << "SKIP REMAIN DATA" << endl;
				}
			}
		}
		if(exists_remain_data && !ignore_remain_data) {
			u_int32_t remain_data_items = reassemblyLink->getRemainDataItems(dataItem->getDirection());
			for(u_int32_t skip_first_remain_data_items = 0; skip_first_remain_data_items < remain_data_items; skip_first_remain_data_items++) {
				if(alloc_ssl_data) {
					delete [] ssl_data;
					alloc_ssl_data = false;
				}
				u_int32_t remain_data_length = reassemblyLink->getRemainDataLength(dataItem->getDirection(), skip_first_remain_data_items);
				ssl_datalen = remain_data_length + dataItem->getDatalen();
				ssl_data = reassemblyLink->completeRemainData(dataItem->getDirection(), &ssl_datalen, dataItem->getAck(), dataItem->getSeq(), dataItem->getData(), dataItem->getDatalen(), skip_first_remain_data_items);
				alloc_ssl_data = true;
				SslHeader header(ssl_data, ssl_datalen);
				if(header.isOk() && header.length && (u_int32_t)header.length + header.getDataOffsetLength() <= ssl_datalen) {
					ok_first_ssl_header = true;
					if(debugStream) {
						(*debugStream) << "APPLY PREVIOUS REMAIN DATA: " << remain_data_length << endl;
					}
					break;
				}
			}
		}
		if(!ok_first_ssl_header) {
			if(alloc_ssl_data) {
				delete [] ssl_data;
				alloc_ssl_data = false;
			}
			ssl_data = dataItem->getData();
			ssl_datalen = dataItem->getDatalen();
			SslHeader header(ssl_data, ssl_datalen);
			if(header.isOk() && header.length && (u_int32_t)header.length + header.getDataOffsetLength() <= ssl_datalen) {
				ok_first_ssl_header = true;
				if(exists_remain_data) {
					ignore_remain_data = true;
				}
			}
		}
		if(ok_first_ssl_header) {
			u_int32_t ssl_data_offset = 0;
			while(ssl_data_offset < ssl_datalen &&
			      ssl_datalen - ssl_data_offset >= 5) {
				SslHeader header(ssl_data + ssl_data_offset, ssl_datalen - ssl_data_offset);
				if(header.isOk() && header.length && (u_int32_t)header.length + header.getDataOffsetLength() <= ssl_datalen - ssl_data_offset) {
					if(debugStream) {
						(*debugStream)
							<< "SSL HEADER "
							<< "content type: " << (int)header.content_type << " / "
							<< "version: " << hex << header.version << dec << " / "
							<< "length: " << header.length
							<< endl;
					}
					vector<string> rslt_decrypt_part;
					if(opt_enable_ssl == 10) {
						#if defined(HAVE_LIBGNUTLS) and defined(HAVE_SSL_WS)
						decrypt_ssl(&rslt_decrypt_part, (char*)(ssl_data + ssl_data_offset), header.length + header.getDataOffsetLength(), htonl(_ip_src), htonl(_ip_dst), _port_src, _port_dst);
						#endif
					} else {
						decrypt_ssl_dssl(&rslt_decrypt_part, (char*)(ssl_data + ssl_data_offset), header.length + header.getDataOffsetLength(), _ip_src, _ip_dst, _port_src, _port_dst, dataItem->getTime(), ignore_remain_data);
					}
					if(rslt_decrypt_part.size()) {
						for(size_t i = 0; i < rslt_decrypt_part.size(); i++) {
							rslt_decrypt.push_back(rslt_decrypt_part[i]);
						}
					}
					ssl_data_offset += header.length + header.getDataOffsetLength();
				} else {
					break;
				}
			}
			if(exists_remain_data) {
				reassemblyLink->clearRemainData(dataItem->getDirection());
				if(debugStream) {
					(*debugStream) << "CLEAR REMAIN DATA" << endl;
				}
			}
			if(ssl_data_offset < ssl_datalen) {
				reassemblyLink->addRemainData(dataItem->getDirection(), dataItem->getAck(), dataItem->getSeq(), ssl_data + ssl_data_offset, ssl_datalen - ssl_data_offset, dataItem->getTime().tv_sec);
				if(debugStream) {
					(*debugStream) << "SET REMAIN DATA: " << (ssl_datalen - ssl_data_offset) << endl;
				}
			}
		} else {
			reassemblyLink->addRemainData(dataItem->getDirection(), dataItem->getAck(), dataItem->getSeq(), ssl_data, ssl_datalen, dataItem->getTime().tv_sec);
			if(debugStream) {
				(*debugStream) << (exists_remain_data ? "ADD" : "SET") << " REMAIN DATA: " << ssl_datalen << endl;
			}
		}
		if(alloc_ssl_data) {
			delete [] ssl_data;
		}
		/* old version
		for(int pass = 0; pass < 2; pass++) {
			u_char *ssl_data;
			u_int32_t ssl_datalen;
			bool alloc_ssl_data = false;
			if(reassemblyLink->existsRemainData(dataItem->getDirection())) {
				ssl_datalen = reassemblyLink->getRemainDataLength(dataItem->getDirection()) + dataItem->getDatalen();
				ssl_data = reassemblyLink->completeRemainData(dataItem->getDirection(), &ssl_datalen, dataItem->getAck(), dataItem->getSeq(), dataItem->getData(), dataItem->getDatalen());
				alloc_ssl_data = true;
			} else {
				ssl_data = dataItem->getData();
				ssl_datalen = dataItem->getDatalen();
			}
			u_int32_t ssl_data_offset = 0;
			while(ssl_data_offset < ssl_datalen &&
			      ssl_datalen - ssl_data_offset >= 5) {
				SslHeader header(ssl_data + ssl_data_offset, ssl_datalen - ssl_data_offset);
				if(header.isOk() && header.length && (u_int32_t)header.length + header.getDataOffsetLength() <= ssl_datalen - ssl_data_offset) {
					if(debugStream) {
						(*debugStream)
							<< "SSL HEADER "
							<< "content type: " << (int)header.content_type << " / "
							<< "version: " << hex << header.version << dec << " / "
							<< "length: " << header.length
							<< endl;
					}
					vector<string> rslt_decrypt_part;
					if(opt_enable_ssl == 10) {
						#if defined(HAVE_LIBGNUTLS) and defined(HAVE_SSL_WS)
						decrypt_ssl(&rslt_decrypt_part, (char*)(ssl_data + ssl_data_offset), header.length + header.getDataOffsetLength(), htonl(_ip_src), htonl(_ip_dst), _port_src, _port_dst);
						#endif
					} else {
						decrypt_ssl_dssl(&rslt_decrypt_part, (char*)(ssl_data + ssl_data_offset), header.length + header.getDataOffsetLength(), _ip_src, _ip_dst, _port_src, _port_dst, dataItem->getTime(),
								 pass == 1);
					}
					if(rslt_decrypt_part.size()) {
						for(size_t i = 0; i < rslt_decrypt_part.size(); i++) {
							rslt_decrypt.push_back(rslt_decrypt_part[i]);
						}
					}
					ssl_data_offset += header.length + header.getDataOffsetLength();
				} else {
					break;
				}
			}
			if(pass == 0) {
				bool ok = false;
				if(reassemblyLink->existsRemainData(dataItem->getDirection()) &&
				   !ssl_data_offset &&
				   (!checkOkSslHeader(dataItem->getData(), dataItem->getDatalen()) || 
				    _checkOkSslData(dataItem->getData(), dataItem->getDatalen()))) {
					// next pass with ignore remainData
					reassemblyLink->clearRemainData(dataItem->getDirection());
					if(debugStream) {
						(*debugStream) << "SKIP REMAIN DATA" << endl;
					}
				} else {
					if(ssl_data_offset < ssl_datalen) {
						reassemblyLink->clearRemainData(dataItem->getDirection());
						reassemblyLink->addRemainData(dataItem->getDirection(), dataItem->getAck(), dataItem->getSeq(), ssl_data + ssl_data_offset, ssl_datalen - ssl_data_offset, dataItem->getTime().tv_sec);
						if(debugStream) {
							(*debugStream) << "REMAIN DATA LENGTH: " << ssl_datalen - ssl_data_offset << endl;
						}
					} else {
						reassemblyLink->clearRemainData(dataItem->getDirection());
					}
					ok = true;
				}
				if(alloc_ssl_data) {
					delete [] ssl_data;
				}
				if(ok) {
					break;
				}
			}
		}
		*/
		if(rslt_decrypt.size() == 1 && rslt_decrypt[0].length() == 1) {
			sStreamId sid(_ip_src, _port_src, _ip_dst, _port_dst);
			if(!reassemblyBuffer.existsStream(&sid)) {
				incomplete_prev_rslt_decrypt[sid] = rslt_decrypt[0];
				continue;
			}
		}
		for(size_t i = 0; i < rslt_decrypt.size(); i++) {
			if(debugStream) {
				string out(rslt_decrypt[i], 0,100);
				std::replace(out.begin(), out.end(), '\n', ' ');
				std::replace(out.begin(), out.end(), '\r', ' ');
				if(out.length()) {
					(*debugStream) << "TS: " << dataItem->getTime().tv_sec << "." << dataItem->getTime().tv_usec << " " << _ip_src.getString() << " -> " << _ip_dst.getString() << " SIP " << rslt_decrypt[i].length() << " " << out << endl;
				}
				++this->counterDecryptData;
				(*debugStream) << "DECRYPT DATA " << this->counterDecryptData << " : " << rslt_decrypt[i] << endl;
			}
			if(!ethHeader || !ethHeaderLength) {
				continue;
			}
			string dataComb;
			int dataCombUse = 0;
			if(i < rslt_decrypt.size() - 1 && rslt_decrypt[i].length() == 1) {
				dataComb = rslt_decrypt[i] + rslt_decrypt[i + 1];
				if(check_sip20((char*)dataComb.c_str(), dataComb.length(), NULL, true) ||
				   check_websocket((char*)dataComb.c_str(), dataComb.length(), cWebSocketHeader::_chdst_na)) {
					dataCombUse = true;
				}
			} else if(i == 0 && incomplete_prev_rslt_decrypt.size()) {
				sStreamId sid(_ip_src, _port_src, _ip_dst, _port_dst);
				map<sStreamId, string>::iterator iter = incomplete_prev_rslt_decrypt.find(sid);
				if(iter != incomplete_prev_rslt_decrypt.end()) {
					dataComb = iter->second + rslt_decrypt[0];
					if(check_sip20((char*)dataComb.c_str(), dataComb.length(), NULL, true) ||
					   check_websocket((char*)dataComb.c_str(), dataComb.length(), cWebSocketHeader::_chdst_na)) {
						dataCombUse = 2;
					}
					incomplete_prev_rslt_decrypt.erase(iter);
				}
			}
			u_char *data = NULL;
			unsigned dataLength = 0;
			ReassemblyBuffer::eType dataType = ReassemblyBuffer::_na;
			if(dataCombUse) {
				data = (u_char*)dataComb.c_str();
				dataLength = dataComb.size();
				if(dataCombUse != 2) {
					++i;
				}
			} else {
				data = (u_char*)rslt_decrypt[i].c_str();
				dataLength = rslt_decrypt[i].size();
			}
			/* diagnosis of bad length websocket data
			if(check_websocket(data, dataLength, cWebSocketHeader::_chdst_na) &&
			   !check_websocket(data, dataLength, cWebSocketHeader::_chdst_strict)) {
				print_websocket_check((char*)data, dataLength);
			}
			*/
			if(check_websocket(data, dataLength)) {
				dataType = ReassemblyBuffer::_websocket;
			} else if(check_websocket(data, dataLength, cWebSocketHeader::_chdst_na) || 
				  (dataLength < websocket_header_length((char*)data, dataLength) && check_websocket_first_byte(data, dataLength))) {
				dataType = ReassemblyBuffer::_websocket_incomplete;
			} else if(check_sip20((char*)data, dataLength, NULL, false)) {
				if(TcpReassemblySip::_checkSip(data, dataLength, TcpReassemblySip::_chssm_na)) {
					dataType = ReassemblyBuffer::_sip;
				} else {
					dataType = ReassemblyBuffer::_sip_incomplete;
				}
			}
			list<ReassemblyBuffer::sDataRslt> dataRslt;
			reassemblyBuffer.cleanup(dataItem->getTime(), &dataRslt);
			bool doProcessPacket = false;
			bool createStream = false;
			if(reassemblyBuffer.existsStream(_ip_src, _port_src, _ip_dst, _port_dst)) {
				doProcessPacket = true;
				createStream = false;
			} else {
				if(dataType == ReassemblyBuffer::_websocket_incomplete ||
				   dataType == ReassemblyBuffer::_sip_incomplete) {
					doProcessPacket = true;
					createStream = true;
				}
			}
			if(doProcessPacket) {
				reassemblyBuffer.processPacket(ethHeader, ethHeaderLength,
							       _ip_src, _port_src, _ip_dst, _port_dst,
							       dataType, data, dataLength, createStream, 
							       dataItem->getTime(), dataItem->getAck(), dataItem->getSeq(),
							       handle_index, dlt, sensor_id, sensor_ip, pid,
							       &dataRslt);
			}
			if(dataRslt.size()) {
				for(list<ReassemblyBuffer::sDataRslt>::iterator iter = dataRslt.begin(); iter != dataRslt.end(); iter++) {
					processPacket(&(*iter));
				}
			}
			if(!doProcessPacket) {
				processPacket(ethHeader, ethHeaderLength, false,
					      data, dataLength, dataType, false,
					      _ip_src, _ip_dst, _port_src, _port_dst,
					      dataItem->getTime(), dataItem->getAck(), dataItem->getSeq(),
					      handle_index, dlt, sensor_id, sensor_ip, pid);
			}
		}
	}
	delete data;
}
 
void SslData::printContentSummary() {
}

void SslData::processPacket(u_char *ethHeader, unsigned ethHeaderLength, bool ethHeaderAlloc,
			    u_char *data, unsigned dataLength, ReassemblyBuffer::eType dataType, bool dataAlloc,
			    vmIP ip_src, vmIP ip_dst, vmPort port_src, vmPort port_dst,
			    timeval time, u_int32_t ack, u_int32_t seq,
			    u_int16_t handle_index, int dlt, int sensor_id, vmIP sensor_ip, sPacketInfoData pid) {
	if(sverb.ssldecode) {
		hexdump(data, dataLength);
		cout << "--- begin ---" << endl;
		cout << ip_src.getString() << ":" << port_src.getString() << " -> "
		     << ip_dst.getString() << ":" << port_dst.getString() << endl;
		if(dataType == ReassemblyBuffer::_websocket) {
			cWebSocketHeader ws(data, dataLength);
			bool allocWsData;
			u_char *ws_data = ws.decodeData(&allocWsData);
			if(ws_data) {
				if(strncasestr((char*)ws_data, "Call-ID", ws.getDataLength())) {
					cout << string((char*)ws_data, ws.getDataLength()) << endl;
				}
				if(allocWsData) {
					delete [] ws_data;
				}
			}
		} else {
			if(strncasestr((char*)data, "Call-ID", dataLength)) {
				cout << string((char*)data, dataLength) << endl;
			}
		}
		cout << "--- end ---" << endl;
	}
	if(dataType == ReassemblyBuffer::_websocket) {
		pcap_pkthdr *tcpHeader;
		u_char *tcpPacket;
		createSimpleTcpDataPacket(ethHeaderLength, &tcpHeader,  &tcpPacket,
					  ethHeader, data, dataLength, 0,
					  ip_src, ip_dst, port_src, port_dst,
					  seq, ack, 0,
					  time.tv_sec, time.tv_usec, dlt);
		unsigned iphdrSize = ((iphdr2*)(tcpPacket + ethHeaderLength))->get_hdr_size();
		unsigned dataOffset = ethHeaderLength + 
				      iphdrSize +
				      ((tcphdr2*)(tcpPacket + ethHeaderLength + iphdrSize))->doff * 4;
		packet_flags pflags;
		pflags.init();
		pflags.tcp = 2;
		pflags.ssl = true;
		preProcessPacket[opt_t2_boost_direct_rtp ?
				  PreProcessPacket::ppt_sip :
				  PreProcessPacket::ppt_detach]->push_packet(
			#if USE_PACKET_NUMBER
			0, 
			#endif
			ip_src, port_src, ip_dst, port_dst, 
			dataLength, dataOffset,
			handle_index, tcpHeader, tcpPacket, _t_packet_alloc_header_std, 
			pflags, (iphdr2*)(tcpPacket + ethHeaderLength), (iphdr2*)(tcpPacket + ethHeaderLength),
			NULL, 0, dlt, sensor_id, sensor_ip, pid,
			false);
	} else {
		pcap_pkthdr *udpHeader;
		u_char *udpPacket;
		createSimpleUdpDataPacket(ethHeaderLength, &udpHeader,  &udpPacket,
					  ethHeader, data, dataLength, 0,
					  ip_src, ip_dst, port_src, port_dst,
					  time.tv_sec, time.tv_usec);
		unsigned iphdrSize = ((iphdr2*)(udpPacket + ethHeaderLength))->get_hdr_size();
		unsigned dataOffset = ethHeaderLength + 
				      iphdrSize + 
				      sizeof(udphdr2);
		packet_flags pflags;
		pflags.init();
		pflags.ssl = true;
		preProcessPacket[opt_t2_boost_direct_rtp ?
				  PreProcessPacket::ppt_sip :
				  PreProcessPacket::ppt_detach]->push_packet(
			#if USE_PACKET_NUMBER
			0,
			#endif
			ip_src, port_src, ip_dst, port_dst, 
			dataLength, dataOffset,
			handle_index, udpHeader, udpPacket, _t_packet_alloc_header_std, 
			pflags, (iphdr2*)(udpPacket + ethHeaderLength), (iphdr2*)(udpPacket + ethHeaderLength),
			NULL, 0, dlt, sensor_id, sensor_ip, pid,
			false);
	}
	if(sverb.ssl_stats) {
		ssl_stats_add_delay_processPacket(getTimeUS(time));
	}
	if(ethHeaderAlloc) {
		delete [] ethHeader;
	}
	if(dataAlloc) {
		delete [] data;
	}
}


bool checkOkSslData(u_char *data, u_int32_t datalen) {
	if(!data) {
		return(false);
	}
	u_int32_t offset = 0;
	u_int32_t len;
	while(offset < datalen &&
	      datalen - offset >= 5 &&
	      (len = _checkOkSslData(data + offset, datalen - offset)) > 0) {
		offset += len;
		if(offset == datalen) {
			return(true);
		}
	}
	return(false);
}

u_int32_t _checkOkSslData(u_char *data, u_int32_t datalen) {
	if(!data) {
		return(false);
	}
	SslData::SslHeader header(data, datalen);
	return(header.length && (u_int32_t)header.length + header.getDataOffsetLength() <= datalen ? header.length + header.getDataOffsetLength() : 0);
}

bool checkOkSslHeader(u_char *data, u_int32_t datalen) {
	if(!data || datalen < 5) {
		return(false);
	}
	SslData::SslHeader header(data, datalen);
	return(header.isOk());
}


int isSslIpPort(vmIP sip, vmPort sport, vmIP dip, vmPort dport) {
	if(!ssl_portmatrix[sport] && !ssl_portmatrix[dport]) {
		return(0);
	}
	ssl_ipport_lock();
	if(ssl_ipport.size()) {
		if(ssl_ipport.find(vmIPport(dip, dport)) != ssl_ipport.end()) {
			ssl_ipport_unlock();
			return(1);
		}
		if(ssl_ipport.find(vmIPport(sip, sport)) != ssl_ipport.end()) {
			ssl_ipport_unlock();
			return(2);
		}
		if(opt_ssl_ipport_reverse_enable) {
			if(ssl_ipport.find(vmIPport(sip, dport)) != ssl_ipport.end()) {
				ssl_ipport_unlock();
				return(1);
			}
			if(ssl_ipport.find(vmIPport(dip, sport)) != ssl_ipport.end()) {
				ssl_ipport_unlock();
				return(2);
			}
		}
	}
	if(ssl_netport.size()) {
		for(map<vmIPmask_port, string>::iterator iter = ssl_netport.begin(); iter != ssl_netport.end(); iter++) {
			if(dport == iter->first.port) {
				if(check_ip(dip, iter->first.ip_mask) ||
				   (opt_ssl_ipport_reverse_enable && check_ip(sip, iter->first.ip_mask))) {
					ssl_ipport_unlock();
					return(1);
				}
			} else if(sport == iter->first.port) {
				if(check_ip(sip, iter->first.ip_mask) ||
				   (opt_ssl_ipport_reverse_enable && check_ip(dip, iter->first.ip_mask))) {
					ssl_ipport_unlock();
					return(2);
				}
			}
		}
	}
	ssl_ipport_unlock();
	return(0);
}

int isSslIpPort_server_side(vmIP sip, vmPort /*sport*/, vmIP dip, vmPort dport) {
	if(!ssl_portmatrix[dport]) {
		return(0);
	}
	ssl_ipport_lock();
	if(ssl_ipport.size()) {
		if(ssl_ipport.find(vmIPport(dip, dport)) != ssl_ipport.end()) {
			ssl_ipport_unlock();
			return(1);
		}
		if(opt_ssl_ipport_reverse_enable) {
			if(ssl_ipport.find(vmIPport(sip, dport)) != ssl_ipport.end()) {
				ssl_ipport_unlock();
				return(1);
			}
		}
	}
	if(ssl_netport.size()) {
		for(map<vmIPmask_port, string>::iterator iter = ssl_netport.begin(); iter != ssl_netport.end(); iter++) {
			if(dport == iter->first.port) {
				if(check_ip(dip, iter->first.ip_mask) ||
				   (opt_ssl_ipport_reverse_enable && check_ip(sip, iter->first.ip_mask))) {
					ssl_ipport_unlock();
					return(1);
				}
			}
		}
	}
	ssl_ipport_unlock();
	return(0);
}

string sslIpPort_get_keyfile(vmIP sip, vmPort sport, vmIP dip, vmPort dport) {
	if(!ssl_portmatrix[sport] && !ssl_portmatrix[dport]) {
		return("");
	}
	ssl_ipport_lock();
	if(ssl_ipport.size()) {
		map<vmIPport, string>::iterator iter = ssl_ipport.find(vmIPport(dip, dport));
		if(iter != ssl_ipport.end()) {
			ssl_ipport_unlock();
			return(iter->second);
		}
		iter = ssl_ipport.find(vmIPport(sip, sport));
		if(iter != ssl_ipport.end()) {
			ssl_ipport_unlock();
			return(iter->second);
		}
		if(opt_ssl_ipport_reverse_enable) {
			iter = ssl_ipport.find(vmIPport(sip, dport));
			if(iter != ssl_ipport.end()) {
				ssl_ipport_unlock();
				return(iter->second);
			}
			iter = ssl_ipport.find(vmIPport(dip, sport));
			if(iter != ssl_ipport.end()) {
				ssl_ipport_unlock();
				return(iter->second);
			}
		}
	}
	if(ssl_netport.size()) {
		for(map<vmIPmask_port, string>::iterator iter = ssl_netport.begin(); iter != ssl_netport.end(); iter++) {
			if(dport == iter->first.port) {
				if(check_ip(dip, iter->first.ip_mask) ||
				   (opt_ssl_ipport_reverse_enable && check_ip(sip, iter->first.ip_mask))) {
					ssl_ipport_unlock();
					return(iter->second);
				}
			} else if(sport == iter->first.port) {
				if(check_ip(sip, iter->first.ip_mask) ||
				   (opt_ssl_ipport_reverse_enable && check_ip(dip, iter->first.ip_mask))) {
					ssl_ipport_unlock();
					return(iter->second);
				}
			}
		}
	}
	ssl_ipport_unlock();
	return("");
}

void ssl_ipport_lock() {
	__SYNC_LOCK_USLEEP(ssl_ipport_sync, 10);
}

void ssl_ipport_unlock() {
	__SYNC_UNLOCK(ssl_ipport_sync);
}

string ssl_ipport_list() {
	string rslt;
	ssl_ipport_lock();
	if(ssl_ipport.size()) {
		for(map<vmIPport, string>::iterator iter = ssl_ipport.begin(); iter != ssl_ipport.end(); iter++) {
			rslt += iter->first.getString(true);
			if(!iter->second.empty()) {
				rslt += " " + iter->second;
			}
			rslt += "\n";
		}
	}
	if(ssl_netport.size()) {
		for(map<vmIPmask_port, string>::iterator iter = ssl_netport.begin(); iter != ssl_netport.end(); iter++) {
			rslt += iter->first.getString(true);
			if(!iter->second.empty()) {
				rslt += " " + iter->second;
			}
			rslt += "\n";
		}
	}
	ssl_ipport_unlock();
	return rslt;
}

bool ssl_ipport_set(const char *set) {
	ssl_ipport_lock();
	ssl_ipport.clear();
	ssl_netport.clear();
	ssl_ipport_unlock();
	return(ssl_ipport_add(set));
}

bool ssl_ipport_add(const char *add) {
	unsigned counter_ok = 0;
	ssl_ipport_lock();
	vector<string> add_list = split(add, split(",|;|\n", "|"), true);
	for(unsigned i = 0; i < add_list.size(); i++) {
		vmIP ip;
		u_int16_t mask = 0;
		unsigned port = 0;
		string str;
		if(cConfigItem_net_port_str_map::parse(add_list[i].c_str(), ip, mask, port, str)) {
			if(ip.isSet() && port > 0) {
				if(!mask) {
					ssl_ipport[vmIPport(ip, port)] = str;
					++counter_ok;
				} else {
					ssl_netport[vmIPmask_port(vmIPmask(ip, mask), port)] = str;
					++counter_ok;
				}
			}
		}
	}
	ssl_ipport_unlock();
	if(counter_ok > 0) {
		fill_ssl_portmatrix();
	}
	return(counter_ok > 0);
}

bool ssl_ipport_del(const char *del) {
	unsigned counter_ok = 0;
	ssl_ipport_lock();
	vector<string> add_list = split(del, split(",|;|\n", "|"), true);
	for(unsigned i = 0; i < add_list.size(); i++) {
		vmIP ip;
		u_int16_t mask = 0;
		unsigned port = 0;
		string str;
		if(cConfigItem_net_port_str_map::parse(add_list[i].c_str(), ip, mask, port, str)) {
			if(ip.isSet() && port > 0) {
				if(!mask) {
					vmIPport index(ip, port);
					if(ssl_ipport.find(index) != ssl_ipport.end()) {
						ssl_ipport.erase(index);
						++counter_ok;
					}
				} else {
					vmIPmask_port index(vmIPmask(ip, mask), port);
					if(ssl_netport.find(index) != ssl_netport.end()) {
						ssl_netport.erase(index);
						++counter_ok;
					}
				}
			}
		}
	}
	ssl_ipport_unlock();
	if(counter_ok > 0) {
		fill_ssl_portmatrix();
	}
	return(counter_ok > 0);
}

void fill_ssl_portmatrix() {
	map<unsigned, bool> ssl_ports;
	ssl_ipport_lock();
	if(ssl_ipport.size()) {
		for(map<vmIPport, string>::iterator iter = ssl_ipport.begin(); iter != ssl_ipport.end(); iter++) {
			ssl_ports[iter->first.port] = true;
		}
	}
	if(ssl_netport.size()) {
		for(map<vmIPmask_port, string>::iterator iter = ssl_netport.begin(); iter != ssl_netport.end(); iter++) {
			ssl_ports[iter->first.port] = true;
		}
	}
	for(unsigned port = 0; port < 65536; port++) {
		ssl_portmatrix[port] = ssl_ports.find(port) != ssl_ports.end() && ssl_ports[port];
	}
	ssl_ipport_unlock();
}
