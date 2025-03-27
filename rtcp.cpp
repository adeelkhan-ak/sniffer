#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <syslog.h>
#include <errno.h>
#include <arpa/inet.h>

#include "calltable.h"
#include "rtp.h"

#define NTP_TIMEDIFF1970TO2036SEC 2085978496ul


//#include "rtcp.h"

/*
 * Static part of RTCP header
 *
 * 0                   1                   2                   3
 * 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |V=2|P|    RC   |       PT      |             length            | header
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *
 */

typedef struct rtcp_header {
#if __BYTE_ORDER == __BIG_ENDIAN
	u_int8_t version:2,
		 padding:1,
		 rc_sc:5;
#else
	u_int8_t rc_sc:5,
		 padding:1,
		 version:2;
#endif
	u_int8_t	packet_type;
	u_int16_t length;
} rtcp_header_t;


/*
 * RTCP SR packet type sender info portion
 *
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |                         SSRC of sender                        |
 * +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+
 * |              NTP timestamp, most significant word             | sender
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+ info
 * |             NTP timestamp, least significant word             |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |                         RTP timestamp                         |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |                     sender's packet count                     |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |                      sender's octet count                     |
 * +---------------------------------------------------------------+
 */

typedef struct rtcp_sr_senderinfo
{
	u_int32_t sender_ssrc;
	u_int32_t timestamp_MSW;
	u_int32_t timestamp_LSW;
	u_int32_t timestamp_RTP;
	u_int32_t sender_pkt_cnt;
	u_int32_t sender_octet_cnt;
} rtcp_sr_senderinfo_t;

/*
 * RTCP SR report block
 *
 * +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+
 * |                 SSRC_1 (SSRC of first source)                 | 
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+ 
 * | fraction lost |       cumulative number of packets lost       |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |           extended highest sequence number received           |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |                      interarrival jitter                      |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |                         last SR (LSR)                         |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |                   delay since last SR (DLSR)                  |
 * +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+
 */

typedef struct rtcp_sr_reportblock
{
	u_int32_t ssrc;
	u_int8_t	frac_lost;
	u_int8_t	packets_lost[3];
	u_int32_t ext_seqno_recvd;
	u_int32_t jitter;
	u_int32_t lsr;
	u_int32_t delay_since_lsr;
} rtcp_sr_reportblock_t;


/* 
 * RTCP packet type definitions 
 */

#define RTCP_PACKETTYPE_SR	200
#define RTCP_PACKETTYPE_RR	201
#define RTCP_PACKETTYPE_SDES	202
#define RTCP_PACKETTYPE_BYE	203
#define RTCP_PACKETTYPE_APP	204
#define RTCP_PACKETTYPE_RTPFB	205
#define RTCP_PACKETTYPE_PSFB	206
#define RTCP_PACKETTYPE_XR	207

/*
 * RTCP payload type map
 */
#if 0
typedef struct strmap {
	u_int32_t number;
	char * string;
} strmap_t;

strmap_t rtcp_packettype_map[] =
{
	{ RTCP_PACKETTYPE_SR,				"sender report" },
	{ RTCP_PACKETTYPE_RR,				"receiver report" },
	{ RTCP_PACKETTYPE_SDES,			"source description" },
	{ RTCP_PACKETTYPE_BYE,			 "bye" },
	{ RTCP_PACKETTYPE_APP,			 "application" },
	{ 0, ""}
};
#endif


/*
 * RTCP XR report block type
 */
typedef enum rtcp_xr_report_type_t_ {
    RTCP_XR_LOSS_RLE = 1,  /* Loss RLE report */
    RTCP_XR_DUP_RLE,       /* Duplicate RLE report */
    RTCP_XR_RTCP_TIMES,    /* Packet receipt times report */
    RTCP_XR_RCVR_RTT,      /* Receiver reference time report */
    RTCP_XR_DLRR,          /* DLRR report */
    RTCP_XR_STAT_SUMMARY,  /* Statistics summary report */
    RTCP_XR_VOIP_METRICS,  /* VoIP metrics report */
    RTCP_XR_BT_XNQ,        /* BT's eXtended Network Quality report */
    RTCP_XR_TI_XVQ,        /* TI eXtended VoIP Quality report */
    RTCP_XR_POST_RPR_LOSS_RLE,  /* Post ER Loss RLE report */
    RTCP_XR_MA = 200,           /* Media Acquisition report (avoid */
    RTCP_XR_DC,                 /* Diagnostic Counters report (TBD) */
    NOT_AN_XR_REPORT       /* this MUST always be LAST */
} rtcp_xr_report_type_t;


typedef struct rtcp_xr_header {
	rtcp_header_t ch;
	uint32_t ssrc;
} rtcp_xr_header_t;

/*
 * generic XR report definition
 */
typedef struct rtcp_xr_gen_t_ {
    uint8_t  bt;                /* Report Block Type */
    uint8_t  type_specific;     /* Report Type Specific */
    uint16_t length;            /* Report Length */
} rtcp_xr_gen_t;

typedef struct rtcp_xr_voip_metrics_report_block {
	uint32_t ssrc;
	uint8_t loss_rate;
	uint8_t discard_rate;
	uint8_t burst_density;
	uint8_t gap_density;
	uint16_t burst_duration;
	uint16_t gap_duration;
	uint16_t round_trip_delay;
	uint16_t end_system_delay;
	int8_t signal_level;
	int8_t noise_level;
	uint8_t rerl;
	uint8_t gmin;
	uint8_t r_factor;
	uint8_t ext_r_factor;
	uint8_t mos_lq;
	uint8_t mos_cq;
	uint8_t rx_config;
	uint8_t reserved2;
	uint16_t jb_nominal;
	uint16_t jb_maximum;
	uint16_t jb_abs_max;
} rtcp_xr_voip_metrics_report_block_t;

extern struct arg_t * my_args;
extern unsigned int opt_ignoreRTCPjitter;

/*----------------------------------------------------------------------------
**
** dump_rtcp_sr()
**
** Parse RTCP sender report fields
**
**----------------------------------------------------------------------------
*/


struct sPrepareRtcpDataParams {
	struct sRtpStream {
		vmIP saddr;
		vmIP daddr;
		u_int32_t ssrc;
		int index;
	};
	vector<int> select_streams;
	vector<sRtpStream> rtp_streams;
	double jitter_limit;
	sPrepareRtcpDataParams() {
		jitter_limit = 0;
	}
};

sPrepareRtcpDataParams *prepare_rtcp_data_params;


static inline RTP *find_rtp(Call *call, u_int32_t ssrc, u_int32_t ssrc_sender, vmIP ip_src, vmIP ip_dst, int *rtp_find_type) {
	RTP *rtp = NULL;
	*rtp_find_type = 0;
	if(ssrc) {
		for(int i = 0; i < call->rtp_size(); i++) { 
			RTP *rtp_i = call->rtp_stream_by_index(i);
			if(rtp_i->ssrc == ssrc &&
			   rtp_i->saddr == ip_dst && rtp_i->daddr == ip_src) {
				rtp = rtp_i;
				*rtp_find_type = 1;
			}
		}
	}
	if(!rtp && ssrc_sender) {
		for(int i = 0; i < call->rtp_size() && !rtp; i++) { 
			RTP *rtp_i = call->rtp_stream_by_index(i);
			if(rtp_i->ssrc == ssrc_sender &&
			   rtp_i->saddr == ip_src && rtp_i->daddr == ip_dst) {
				RTP *_rtp = NULL;
				int c = 0;
				for(int j = 0; j < call->rtp_size(); j++) { 
					if(j != i) {
						RTP *rtp_j = call->rtp_stream_by_index(j);
						if(rtp_j->saddr == ip_dst && rtp_j->daddr == ip_src) {
							_rtp = rtp_j;
							++c;
						}
					}
				}
				if(c == 1) {
					rtp = _rtp;
					*rtp_find_type = 2;
				}
				break;
			}
		}
	}
	if(!rtp) {
		for(int i = 0; i < call->rtp_size(); i++) { 
			RTP *rtp_i = call->rtp_stream_by_index(i);
			if(rtp_i->ssrc == ssrc) {
				rtp = rtp_i;
				*rtp_find_type = 3;
			}
		}
	}
	return(rtp);
}

static inline int find_rtp_index(u_int32_t ssrc, u_int32_t ssrc_sender, vmIP ip_src, vmIP ip_dst) {
	int stream_index = -1;
	if(ssrc) {
		for(unsigned i = 0; i < prepare_rtcp_data_params->rtp_streams.size(); i++) { 
			if(prepare_rtcp_data_params->rtp_streams[i].ssrc == ssrc &&
			   prepare_rtcp_data_params->rtp_streams[i].saddr == ip_dst && prepare_rtcp_data_params->rtp_streams[i].daddr == ip_src) {
				stream_index = prepare_rtcp_data_params->rtp_streams[i].index;
			}
		}
	}
	if(stream_index < 0 && ssrc_sender) {
		for(unsigned i = 0; i < prepare_rtcp_data_params->rtp_streams.size() && stream_index < 0; i++) { 
			if(prepare_rtcp_data_params->rtp_streams[i].ssrc == ssrc_sender &&
			   prepare_rtcp_data_params->rtp_streams[i].saddr == ip_src && prepare_rtcp_data_params->rtp_streams[i].daddr == ip_dst) {
				int _stream_index = -1;
				int c = 0;
				for(unsigned j = 0; j < prepare_rtcp_data_params->rtp_streams.size(); j++) { 
					if(j != i) {
						if(prepare_rtcp_data_params->rtp_streams[j].saddr == ip_dst && prepare_rtcp_data_params->rtp_streams[j].daddr == ip_src) {
							_stream_index = prepare_rtcp_data_params->rtp_streams[j].index;
							++c;
						}
					}
				}
				if(c == 1) {
					stream_index = _stream_index;
				}
				break;
			}
		}
	}
	if(stream_index < 0) {
		for(unsigned i = 0; i < prepare_rtcp_data_params->rtp_streams.size(); i++) { 
			if(prepare_rtcp_data_params->rtp_streams[i].ssrc == ssrc) {
				stream_index = prepare_rtcp_data_params->rtp_streams[i].index;
			}
		}
	}
	if(stream_index >= 0) {
		if(prepare_rtcp_data_params->select_streams.size() == 0) {
			return(stream_index);
		} else {
			for(unsigned i = 0; i < prepare_rtcp_data_params->select_streams.size(); i++) { 
				if(stream_index == prepare_rtcp_data_params->select_streams[i]) {
					return(stream_index);
				}
			}
		}
	}
	return(-1);
}
 
char *dump_rtcp_sr(char *data, unsigned int datalen, int count, CallBranch *c_branch, struct timeval *ts,
		   vmIP ip_src, vmPort port_src, vmIP ip_dst, vmPort port_dst, bool srtcp)
{
	char *pkt = data;
	rtcp_sr_senderinfo_t senderinfo;
	rtcp_sr_reportblock_t reportblock;
	int reports_seen;

	/* Get the sender info */
	if((pkt + sizeof(rtcp_sr_senderinfo_t)) < (data + datalen)){
		memcpy(&senderinfo, pkt, sizeof(rtcp_sr_senderinfo_t));
		pkt += sizeof(rtcp_sr_senderinfo_t);
	} else {
		return pkt;
	}

	/* Conversions */
	senderinfo.sender_ssrc = ntohl(senderinfo.sender_ssrc);
	senderinfo.timestamp_MSW = ntohl(senderinfo.timestamp_MSW);
	senderinfo.timestamp_LSW = ntohl(senderinfo.timestamp_LSW);
	senderinfo.timestamp_RTP = ntohl(senderinfo.timestamp_RTP);
	senderinfo.sender_pkt_cnt = ntohl(senderinfo.sender_pkt_cnt);
	senderinfo.sender_octet_cnt = ntohl(senderinfo.sender_octet_cnt);

	#if not EXPERIMENTAL_LITE_RTP_MOD
	u_int32_t cur_lsr = ((senderinfo.timestamp_MSW & 0xffff) << 16) | ((senderinfo.timestamp_LSW & 0xffff0000) >> 16);
	u_int32_t last_lsr = 0;
	u_int32_t last_lsr_delay = 0;
	RTP *rtp_sender = NULL;
	for(int find_pass = 0; find_pass < 2 && !rtp_sender; find_pass++) {
		for(int i = 0; i < c_branch->call->rtp_size(); i++) { 
			RTP *rtp_i = c_branch->call->rtp_stream_by_index(i);
			if(rtp_i->ssrc == senderinfo.sender_ssrc &&
			   ((find_pass == 0 && 
			     ((rtp_i->saddr == ip_src && rtp_i->daddr == ip_dst) ||
			      (rtp_i->saddr == ip_dst && rtp_i->daddr == ip_src))) ||
			    find_pass == 1)) {
				rtp_sender = rtp_i;
				rtp_sender->rtcp.lsr4compare = cur_lsr;
				last_lsr = rtp_sender->rtcp.last_lsr;
				last_lsr_delay = rtp_sender->rtcp.last_lsr_delay;
				rtp_sender->rtcp.sniff_ts.tv_sec = ts->tv_sec;
				rtp_sender->rtcp.sniff_ts.tv_usec = ts->tv_usec;
				break;
			}
		}
	}
	#endif

	if(sverb.debug_rtcp) {
		cout << " * dump_rtcp_sr SSRC: " << hex << senderinfo.sender_ssrc << dec
		     << " Timestamp MSW: " << senderinfo.timestamp_MSW
		     << " Timestamp LSW: " << senderinfo.timestamp_LSW
		     << " RTP timestamp: " << senderinfo.timestamp_RTP
		     << " Sender packet count: " << senderinfo.sender_pkt_cnt
		     << " Sender octet count: " << senderinfo.sender_octet_cnt
		     << endl;
	}
	
	/* Loop over report blocks */
	reports_seen = 0;
	while(reports_seen < count) {
		/* Get the report block */
		if((pkt + sizeof(rtcp_sr_reportblock_t)) <= (data + datalen)){
			memcpy(&reportblock, pkt, sizeof(rtcp_sr_reportblock_t));
			pkt += sizeof(rtcp_sr_reportblock_t);
		} else {
			break;
		}
			
		/* Conversions */
		reportblock.ssrc = ntohl(reportblock.ssrc);
		reportblock.ext_seqno_recvd = ntohl(reportblock.ext_seqno_recvd);
		reportblock.jitter = ntohl(reportblock.jitter);
		reportblock.lsr = ntohl(reportblock.lsr);
		reportblock.delay_since_lsr = ntohl(reportblock.delay_since_lsr);

		if(!prepare_rtcp_data_params) {
			int rtp_find_type;
			RTP *rtp = find_rtp(c_branch->call, reportblock.ssrc, senderinfo.sender_ssrc, ip_src, ip_dst, &rtp_find_type);
			int32_t loss = ntoh24(reportblock.packets_lost);
			loss = loss & 0x800000 ? 0xff000000 | loss : loss;
			if(rtp) {
				#if not EXPERIMENTAL_LITE_RTP_MOD
				rtp->rtcp.counter++;
				rtp->rtcp.loss = loss;
				if (reportblock.frac_lost)
					rtp->rtcp.fraclost_pkt_counter++;
				rtp->rtcp.maxfr = (rtp->rtcp.maxfr < reportblock.frac_lost) ? reportblock.frac_lost : rtp->rtcp.maxfr;
				rtp->rtcp.avgfr = (rtp->rtcp.avgfr * (rtp->rtcp.counter - 1) + reportblock.frac_lost) / rtp->rtcp.counter;
				if (opt_ignoreRTCPjitter == 0 or reportblock.jitter < opt_ignoreRTCPjitter) {
					rtp->rtcp.jitt_counter++;
					rtp->rtcp.maxjitter = (rtp->rtcp.maxjitter < reportblock.jitter) ? reportblock.jitter : rtp->rtcp.maxjitter;
					rtp->rtcp.avgjitter = (rtp->rtcp.avgjitter * (rtp->rtcp.jitt_counter - 1) + reportblock.jitter) / rtp->rtcp.jitt_counter;
				}
				// calculate rtcp round trip delay
				if (reportblock.lsr && reportblock.delay_since_lsr && rtp->rtcp.lsr4compare == reportblock.lsr) {
					if (last_lsr && last_lsr_delay && rtp_sender) {
						int tmpdiff = cur_lsr - last_lsr - last_lsr_delay - reportblock.delay_since_lsr;
						if (tmpdiff > 0) {
							rtp_sender->rtcp.rtd_sum += tmpdiff;
							rtp_sender->rtcp.rtd_count++;
							if (rtp_sender->rtcp.rtd_max < (uint)tmpdiff) {
								rtp_sender->rtcp.rtd_max = tmpdiff;
							}
						}
					}
					if (timerisset(&rtp->rtcp.sniff_ts)) {
						struct timeval tmpts;
						timersub(ts, &rtp->rtcp.sniff_ts, &tmpts);
						unsigned int ms = tmpts.tv_sec * 1000 + tmpts.tv_usec / 1000 - reportblock.delay_since_lsr *1000 / 65536;
						if (ms > 0) {
							rtp->rtcp.rtd_w_count++;
							rtp->rtcp.rtd_w_sum += ms;
							if (rtp->rtcp.rtd_w_max < ms) {
								rtp->rtcp.rtd_w_max = ms;
							}
						}
					}
				}
				rtp->rtcp.last_lsr = reportblock.lsr;
				rtp->rtcp.last_lsr_delay = reportblock.delay_since_lsr;
				#endif
				if(sverb.debug_rtcp) {
					cout << "sSSRC: " << hex << reportblock.ssrc << dec
					     << " " << ip_src.getString() << "->" << ip_dst.getString() << " (" <<  rtp_find_type << ")"
					     << " RTP->ssrc_index: " << rtp->ssrc_index
					     << " Fraction lost: " << (int)reportblock.frac_lost
					     << " Packets lost: " << loss
					     << " Highest seqno received: " << reportblock.ext_seqno_recvd
					     << " Jitter: " << reportblock.jitter
					     << " Last SR: " << reportblock.lsr
					     << " Delay since last SR: " <<reportblock.delay_since_lsr
					     << endl;
				}
			} else {
				c_branch->call->rtcpData.add_rtcp_sr_rr(c_branch->branch_id, vmIPport(ip_dst, port_dst), vmIPport(ip_src, port_src), reportblock.ssrc,
									*ts,
									true, loss,
									opt_ignoreRTCPjitter == 0 || reportblock.jitter < opt_ignoreRTCPjitter, reportblock.jitter,
									true /*reportblock.frac_lost != 0*/, reportblock.frac_lost);
				if(sverb.debug_rtcp) {
					cout << "sSSRC: " << hex << reportblock.ssrc << dec 
					     << " skipped (no rtp stream with this ssrc)" 
					     << endl;
				}
			}
		} else {
			int stream_index = find_rtp_index(reportblock.ssrc, senderinfo.sender_ssrc, ip_src, ip_dst);
			if(stream_index >= 0) {
				cout << "rtcp,"
				     << stream_index << ","
				     << TIME_US_TO_SF(c_branch->call->getRelTime(ts)) << ","
				     <<	(opt_ignoreRTCPjitter == 0 || reportblock.jitter < opt_ignoreRTCPjitter ? intToString(reportblock.jitter) : "") << ","
				     << (reportblock.frac_lost ? intToString(reportblock.frac_lost) : "") << ","
				     << (srtcp ? "1" : "") << ","
					 << ip_src.getString() << ","
					 << ip_dst.getString() << ","
					 << "Sender Report"
				     << endl;
			}
		}

		reports_seen++;
	}
	return pkt;
}

/*----------------------------------------------------------------------------
**
** dump_rtcp_rr()
**
** Parse RTCP receiver report fields
**
**----------------------------------------------------------------------------
*/

char *dump_rtcp_rr(char *data, int datalen, int count, CallBranch *c_branch, struct timeval *ts,
		   vmIP ip_src, vmPort port_src, vmIP ip_dst, vmPort port_dst, bool srtcp)
{
	char *pkt = data;
	rtcp_sr_reportblock_t reportblock;
	int	reports_seen;
	u_int32_t ssrc;

	/* Get the SSRC */
	if((pkt + sizeof(u_int32_t)) < (data + datalen)){
		ssrc = *pkt;
		pkt += sizeof(u_int32_t);
	} else {
		return pkt;
	}

	/* Conversions */
	ssrc = ntohl(ssrc);

	if(sverb.debug_rtcp) {
		cout << " * dump_rtcp_rr SSRC: " << hex << ssrc << dec
		     << endl;
	}

	/* Loop over report blocks */
	reports_seen = 0;
	while(reports_seen < count) {
		/* Get the report block */
		if((pkt + sizeof(rtcp_sr_reportblock_t)) <= (data + datalen)){
			memcpy(&reportblock, pkt, sizeof(rtcp_sr_reportblock_t));
			pkt += sizeof(rtcp_sr_reportblock_t);
		} else {
			break;
		}

		/* Conversions */
		reportblock.ssrc = ntohl(reportblock.ssrc);
		reportblock.ext_seqno_recvd = ntohl(reportblock.ext_seqno_recvd);
		reportblock.jitter = ntohl(reportblock.jitter);
		reportblock.lsr = ntohl(reportblock.lsr);
		reportblock.delay_since_lsr = ntohl(reportblock.delay_since_lsr);
		
		if(!prepare_rtcp_data_params) {

			int rtp_find_type;
			RTP *rtp = find_rtp(c_branch->call, reportblock.ssrc, 0, ip_src, ip_dst, &rtp_find_type);
			int32_t loss = ntoh24(reportblock.packets_lost);
			loss = loss & 0x800000 ? 0xff000000 | loss : loss;
			if(rtp) {
				#if not EXPERIMENTAL_LITE_RTP_MOD
				rtp->rtcp.counter++;
				rtp->rtcp.loss = loss;
				if (reportblock.frac_lost)
					rtp->rtcp.fraclost_pkt_counter++;
				rtp->rtcp.maxfr = (rtp->rtcp.maxfr < reportblock.frac_lost) ? reportblock.frac_lost : rtp->rtcp.maxfr;
				rtp->rtcp.avgfr = (rtp->rtcp.avgfr * (rtp->rtcp.counter - 1) + reportblock.frac_lost) / rtp->rtcp.counter;
				if (opt_ignoreRTCPjitter == 0 or reportblock.jitter < opt_ignoreRTCPjitter) {
					rtp->rtcp.jitt_counter++;
					rtp->rtcp.maxjitter = (rtp->rtcp.maxjitter < reportblock.jitter) ? reportblock.jitter : rtp->rtcp.maxjitter;
					rtp->rtcp.avgjitter = (rtp->rtcp.avgjitter * (rtp->rtcp.jitt_counter - 1) + reportblock.jitter) / rtp->rtcp.jitt_counter;
				}
				// calculate rtcp round trip delay
				if (reportblock.lsr && reportblock.delay_since_lsr && rtp->rtcp.lsr4compare == reportblock.lsr) {
					if (timerisset(&rtp->rtcp.sniff_ts)) {
						struct timeval tmpts;
						timersub(ts, &rtp->rtcp.sniff_ts, &tmpts);
						unsigned int ms = tmpts.tv_sec * 1000 + tmpts.tv_usec / 1000 - reportblock.delay_since_lsr * 1000 / 65536;
						if (ms > 0) {
							rtp->rtcp.rtd_w_count++;
							rtp->rtcp.rtd_w_sum += ms;
							if (rtp->rtcp.rtd_w_max < ms) {
								rtp->rtcp.rtd_w_max = ms;
							}
						}
					}
				}
				#endif
				if(sverb.debug_rtcp) {
					cout << "rSSRC: " << hex << reportblock.ssrc << dec
					     << " " << ip_src.getString() << "->" << ip_dst.getString() << " (" <<  rtp_find_type << ")"
					     << " RTP->ssrc_index: " << rtp->ssrc_index
					     << " Fraction lost: " << (int)reportblock.frac_lost
					     << " Packets lost: " << loss
					     << " Highest seqno received: " << reportblock.ext_seqno_recvd
					     << " Jitter: " << reportblock.jitter
					     << " Last SR: " << reportblock.lsr
					     << " Delay since last SR: " <<reportblock.delay_since_lsr
					     << endl;
				}
			} else {
				c_branch->call->rtcpData.add_rtcp_sr_rr(c_branch->branch_id, vmIPport(ip_dst, port_dst), vmIPport(ip_src, port_src), reportblock.ssrc,
									*ts,
									true, loss,
									opt_ignoreRTCPjitter == 0 || reportblock.jitter < opt_ignoreRTCPjitter, reportblock.jitter,
									true /*reportblock.frac_lost != 0*/, reportblock.frac_lost);
				if(sverb.debug_rtcp) {
					cout << "sSSRC: " << hex << reportblock.ssrc << dec 
					     << " skipped (no rtp stream with this ssrc)" 
					     << endl;
				}
			}
		} else {
			int stream_index = find_rtp_index(reportblock.ssrc, 0, ip_src, ip_dst);
			if(stream_index >= 0) {
				cout << "rtcp,"
				     << stream_index << ","
				     << TIME_US_TO_SF(c_branch->call->getRelTime(ts)) << ","
				     <<	(opt_ignoreRTCPjitter == 0 || reportblock.jitter < opt_ignoreRTCPjitter ? intToString(reportblock.jitter) : "") << ","
				     << (reportblock.frac_lost ? intToString(reportblock.frac_lost) : "") << ","
				     << (srtcp ? "1" : "") << ","
					 << ip_src.getString() << ","
					 << ip_dst.getString() << ","
					 << "Receiver Report"
				     << endl;
			}
		}

		reports_seen++;
	}
	return pkt;
}

/*----------------------------------------------------------------------------
**
** dump_rtcp_sdes()
**
** Parse RTCP source description fields
**
**----------------------------------------------------------------------------
*/

char *dump_rtcp_sdes(char *data, unsigned int datalen, int count)
{
	char *pkt = data;
	u_int32_t	ssrc;
	u_int8_t	type;
	u_int8_t	length = 0;
	u_int8_t * string;
	int				chunks_read;
	int				pad_len;

	chunks_read = 0;
	while(chunks_read < count) {
		/* Get the ssrc, type and length */
		if((pkt + sizeof(u_int32_t)) < (data + datalen)){
			ssrc = *pkt;
			pkt += sizeof(u_int32_t);
		} else {
			break;
		}
		ssrc = ntohl(ssrc);
		if(sverb.debug_rtcp) {
			cout << " * dump_rtcp_sdes SSRC/CSRC: " << hex << ssrc << dec
			     << endl;
		}
		/* Loop through items */
		while (1) {
			if((pkt + sizeof(u_int8_t)) < (data + datalen)){
				type = (u_int8_t)*pkt;
				pkt += sizeof(u_int8_t);
			} else {
				break;
			}
			if((pkt + sizeof(u_int8_t)) < (data + datalen)){
				length = (u_int8_t)*pkt;
				pkt += sizeof(u_int8_t);
			} else {
				break;
			}
			
			/* Allocate memory for the string then get it */
			string = new FILE_LINE(23001) u_int8_t[length + 1];
			if((pkt + length) < (data + datalen)){
				memcpy(string, pkt, length);
				pkt += length;
			} else {
				delete [] string;
				break;
			}
			string[length] = '\0';
			
			if(sverb.debug_rtcp) {
				cout << "Type: " << (int)type
				     << " Length: " << (int)length
				     << " SDES: " << string
				     << endl;
			}

			/* Free string memory */
			delete [] string;
			
			/* Look for a null terminator */
//			if (look_packet_bytes((u_int8_t *) &byte, pkt, 1) == 0)
//				break;
			if((pkt + 1) < (data + datalen)) {
				pkt++;
				if (*pkt == 0) {
					break;
				}
			} else {
				break;
			}
		}

		/* Figure out the pad and skip by it */
		pad_len = 4 - (length + 2) % 4;
		pkt += pad_len;
			
		chunks_read ++;
	}
	return pkt;
}

/*----------------------------------------------------------------------------
**
** dump_rtcp_xr()
**
** Parse RTCP extended report fields
**
**----------------------------------------------------------------------------
*/

void dump_rtcp_xr(char *data, unsigned int datalen, int all_block_size, CallBranch *c_branch, struct timeval *ts,
		  vmIP ip_src, vmPort port_src, vmIP ip_dst, vmPort port_dst)
{
	char *pkt = data;
	int reports_seen;

	rtcp_xr_header_t *header = (rtcp_xr_header_t*)pkt;

	if(sverb.debug_rtcp) {
		cout << " * dump_rtcp_xr sender SSRC: " << hex << ntohl(header->ssrc) << dec
		     << endl;
	}

	pkt += sizeof(rtcp_xr_header_t);
	all_block_size -= sizeof(rtcp_xr_header_t);
	
	/* Loop over report blocks */
	reports_seen = 0;
	while(all_block_size > (int)sizeof(rtcp_xr_gen_t)) {

		if(pkt + sizeof(rtcp_xr_gen_t) > (data + datalen)) {
			break;
		}

		rtcp_xr_gen_t *block = (rtcp_xr_gen_t*)pkt;
		unsigned block_size = sizeof(rtcp_xr_gen_t) + ntohs(block->length) * 4;
		all_block_size -= block_size;

		if((rtcp_xr_report_type_t_)block->bt != RTCP_XR_VOIP_METRICS) {
			pkt += block_size;
			continue;
		}

		pkt += sizeof(rtcp_xr_gen_t);
		rtcp_xr_voip_metrics_report_block_t *xr = (rtcp_xr_voip_metrics_report_block_t*)pkt;
	
		unsigned count_use_rtp = 0;
		#if not EXPERIMENTAL_LITE_RTP_MOD
		for(int i = 0; i < c_branch->call->rtp_size(); i++) { RTP *rtp_i = c_branch->call->rtp_stream_by_index(i);
			if(rtp_i->ssrc == ntohl(xr->ssrc)) {
				RTP *rtp = rtp_i;
				rtp->rtcp_xr.counter_fr++;
				rtp->rtcp_xr.maxfr = (rtp->rtcp_xr.maxfr < xr->loss_rate) ? xr->loss_rate : rtp->rtcp_xr.maxfr;
				rtp->rtcp_xr.avgfr = (rtp->rtcp_xr.avgfr * (rtp->rtcp_xr.counter_fr - 1) + xr->loss_rate) / rtp->rtcp_xr.counter_fr;
				if(xr->mos_lq != 0x7F) {
					rtp->rtcp_xr.counter_mos++;
					rtp->rtcp_xr.minmos = (rtp->rtcp_xr.minmos > xr->mos_lq) ? xr->mos_lq : rtp->rtcp_xr.minmos;
					rtp->rtcp_xr.avgmos = (rtp->rtcp_xr.avgmos * (rtp->rtcp_xr.counter_mos - 1) + xr->mos_lq) / rtp->rtcp_xr.counter_mos;
				}
				if(sverb.debug_rtcp) {
					cout << "identifier:" << hex << ntohl(xr->ssrc) << dec
					     << " Fraction lost: " << (int)xr->loss_rate
					     << " Fraction discarded: " << (int)xr->discard_rate
					     << " Burst density: " << (int)xr->burst_density
					     << " Gap density: " << (int)xr->gap_density
					     << " Burst duration: " << ntohs(xr->burst_duration)
					     << " Gap duration: " << ntohs(xr->gap_duration)
					     << " Round trip delay: " << ntohs(xr->round_trip_delay)
					     << " End system delay: " << ntohs(xr->end_system_delay)
					     << " Signal Level: " << (int)xr->signal_level
					     << " Noise level: " << (int)xr->noise_level
					     << " Residual echo return loss: " << (int)xr->rerl
					     << " Gmin: " << (int)xr->gmin
					     << " R Factor: " << (int)xr->r_factor
					     << " External R Factor: " << (int)xr->ext_r_factor
					     << " MOS Listening Quality: " << (int)xr->mos_lq
					     << " MOS Conversational Quality: " << (int)xr->mos_cq
					     << " rx_config: " << (int)xr->rx_config
					     << " Nominal jitter buffer size: " << ntohs(xr->jb_nominal)
					     << " Maximum jitter buffer size: " << ntohs(xr->jb_maximum)
					     << " Absolute maximum jitter buffer size: " << ntohs(xr->jb_abs_max)
					     << endl;
				}
				++count_use_rtp;
			}
		}
		#endif
		if(!count_use_rtp) {
			c_branch->call->rtcpData.add_rtcp_xr(c_branch->branch_id, vmIPport(ip_dst, port_dst), vmIPport(ip_src, port_src), ntohl(xr->ssrc), 
							     *ts,
							     xr->mos_lq != 0x7F, xr->mos_lq,
							     true, xr->loss_rate);
			if(sverb.debug_rtcp) {
				cout << "identifier: " << hex << ntohl(xr->ssrc) << dec 
				     << " skipped (no rtp stream with this ssrc)" 
				     << endl;
			}
		}

		pkt += ntohs(block->length) * 4;
		reports_seen++;
	}
	return;
}

/*----------------------------------------------------------------------------
**
** dump_rtcp()
**
** Parse RTCP packet and dump fields
**
**----------------------------------------------------------------------------
*/

void parse_rtcp(char *data, int datalen, timeval *ts, CallBranch* c_branch, 
		vmIP ip_src, vmPort port_src, vmIP ip_dst, vmPort port_dst, bool srtcp)
{
	char *pkt = data;
	rtcp_header_t *rtcp;
	
	if(sverb.debug_rtcp) {
		cout << "RTCP PACKET " << c_branch->call->fbasename
		     << " ts: " << ts->tv_sec << "." 
				<< setfill('0') << setw(6) << ts->tv_usec
		     << " " << ip_src.getString() << "->" << ip_dst.getString()
		     << endl;
	}

	while(1){
		/* Get the fixed RTCP header */
		if((pkt + sizeof(rtcp_header_t)) < (data + datalen)){
			rtcp = (rtcp_header_t*)pkt;
		} else {
			break;
		}

		int rtcp_size = ntohs(rtcp->length) * 4 + sizeof(rtcp_header_t);

		if(rtcp->version != 2) {
			if(sverb.debug_rtcp) {
				cout << "Malformed RTCP header (version != 2)" << endl;
			}
			pkt += rtcp_size;
			break;
		}
	
		if((pkt + rtcp_size) > (data + datalen)){
			if(sverb.debug_rtcp) {
				cout << "Malformed RTCP header (overflow rtcp length)" << endl;
			}
			//rtcp too big 
			break;
		}

		char *rtcp_data = pkt + sizeof(rtcp_header_t);
	
		if(sverb.debug_rtcp) {
			cout << "RTCP Header"
			     << " Version: " << (int)rtcp->version
			     << " Padding: " << (int)rtcp->padding
			     << " Report/source count: " << (int)rtcp->rc_sc
			     << " Packet type: " << (int)rtcp->packet_type
			     << " Length: " << ntohs(rtcp->length)
			     << endl;
		}
			
		switch(rtcp->packet_type) {
		case RTCP_PACKETTYPE_SR:
			dump_rtcp_sr(rtcp_data, data + datalen - rtcp_data, rtcp->rc_sc, c_branch, ts, 
				     ip_src, port_src, ip_dst, port_dst, srtcp);
			break;
		case RTCP_PACKETTYPE_RR:
			dump_rtcp_rr(rtcp_data, data + datalen - rtcp_data, rtcp->rc_sc, c_branch, ts,
				     ip_src, port_src, ip_dst, port_dst, srtcp);
			break;
		case RTCP_PACKETTYPE_SDES:
			// we do not need to parse it
			//dump_rtcp_sdes(rtcp_data, data + datalen - rtcp_data, rtcp->rc_sc);
			if(sverb.debug_rtcp) {
				cout << " * packet type: sdes - skip" << endl;
			}
			break;
		case RTCP_PACKETTYPE_XR:
			dump_rtcp_xr(pkt, data + datalen - rtcp_data, rtcp_size, c_branch, ts,
				     ip_src, port_src, ip_dst, port_dst);
			break;
		default:
			if(sverb.debug_rtcp) {
				cout << " * packet type: other - skip" << endl;
			}
			break;
		}

		pkt += rtcp_size;
	}
	
	if(sverb.debug_rtcp) {
		cout << endl;
	}
}


void parseRtcpParams(string &rtcp_params_string, sPrepareRtcpDataParams *rtcp_params) {
	JsonItem jsonData;
	jsonData.parse(rtcp_params_string);
	JsonItem *item_streams = jsonData.getItem("select_streams");
	if(item_streams) {
		for(size_t i = 0; i < item_streams->getLocalCount(); i++) {
			rtcp_params->select_streams.push_back(atoi(item_streams->getLocalItem(i)->getLocalValue().c_str()));
		}
	}
	JsonItem *item_rtp_streams = jsonData.getItem("rtp_streams");
	if(item_rtp_streams) {
		for(size_t i = 0; i < item_rtp_streams->getLocalCount(); i++) {
			JsonItem *item_rtp_stream = item_rtp_streams->getLocalItem(i);
			sPrepareRtcpDataParams::sRtpStream rtp_stream;
			rtp_stream.saddr = str_2_vmIP(item_rtp_stream->getValue("saddr").c_str());
			rtp_stream.daddr = str_2_vmIP(item_rtp_stream->getValue("daddr").c_str());
			rtp_stream.ssrc = atoll(item_rtp_stream->getValue("ssrc").c_str());
			rtp_stream.index = atoll(item_rtp_stream->getValue("index").c_str());
			rtcp_params->rtp_streams.push_back(rtp_stream);
		}
	}
	JsonItem *item_jitter_limit = jsonData.getItem("jitter_limit");
	if(item_jitter_limit) {
		rtcp_params->jitter_limit = atof(item_jitter_limit->getLocalValue().c_str());
	}
}

void parseRtcpParams(string &rtcp_params_string) {
	prepare_rtcp_data_params = new FILE_LINE(0) sPrepareRtcpDataParams;
	parseRtcpParams(rtcp_params_string, prepare_rtcp_data_params);
}

bool createRtcpPayloadFromJson(const char *json, SimpleBuffer *buffer) {
	if(!isJsonObject(json)) {
		return(false);
	}
	JsonItem jsonData;
	jsonData.parse(json);
	
	rtcp_header header;
	header.version = 2;
	header.padding = 0;
	header.rc_sc = atoi(jsonData.getValue("report_count").c_str());
	header.packet_type = atoi(jsonData.getValue("type").c_str());
	header.length = 0;
	
	JsonItem *jsonData_sender_info;
	jsonData_sender_info = jsonData.getItem("sender_information");
	if(!jsonData_sender_info) {
		return(false);
	}
	
	rtcp_sr_senderinfo sender_info;
	sender_info.sender_ssrc = htonl(atoll(jsonData.getValue("ssrc").c_str()));
	u_int32_t ntp_sec = atoll(jsonData_sender_info->getValue("ntp_timestamp_sec").c_str());
	u_int32_t ntp_usec = atoll(jsonData_sender_info->getValue("ntp_timestamp_usec").c_str());
	u_int32_t ntp_fract = (u_int32_t)((double)ntp_usec / 1e6 * ((1ul<<32)-1));
	sender_info.timestamp_MSW = htonl(ntp_sec - NTP_TIMEDIFF1970TO2036SEC);
	sender_info.timestamp_LSW = htonl(ntp_fract);
	sender_info.timestamp_RTP = htonl(atoll(jsonData_sender_info->getValue("rtp_timestamp").c_str()));
	sender_info.sender_pkt_cnt = htonl(atoll(jsonData_sender_info->getValue("packets").c_str()));
	sender_info.sender_octet_cnt = htonl(atoll(jsonData_sender_info->getValue("octets").c_str()));
	
	JsonItem *jsonData_report_blocks;
	jsonData_report_blocks = jsonData.getItem("report_blocks");
	if(!jsonData_report_blocks) {
		return(false);
	}
	unsigned report_blocks_count = jsonData_report_blocks->getLocalCount();
	if(report_blocks_count != header.rc_sc) {
		return(false);
	}
	
	header.length = htons((sizeof(sender_info) + report_blocks_count * sizeof(rtcp_sr_reportblock)) / 4);
	buffer->add(&header, sizeof(header));
	buffer->add(&sender_info, sizeof(sender_info));
	
	for(unsigned int i = 0; i < report_blocks_count; i++) {
		JsonItem *jsonData_report_block = jsonData_report_blocks->getLocalItem(i);
		rtcp_sr_reportblock report_block;
		report_block.ssrc = htonl(atoll(jsonData_report_block->getValue("source_ssrc").c_str()));
		report_block.frac_lost = atoi(jsonData_report_block->getValue("fraction_lost").c_str());
		u_int32_t packets_lost = atoll(jsonData_report_block->getValue("packets_lost").c_str());
		report_block.packets_lost[0] = (packets_lost >> 16) & 0xFF;
		report_block.packets_lost[1] = (packets_lost >> 8) & 0xFF;
		report_block.packets_lost[2] = packets_lost & 0xFF;
		report_block.ext_seqno_recvd = htonl(atoll(jsonData_report_block->getValue("highest_seq_no").c_str()));
		report_block.jitter = htonl(atoll(jsonData_report_block->getValue("ia_jitter").c_str()));
		report_block.lsr = htonl(atoll(jsonData_report_block->getValue("lsr").c_str()));
		report_block.delay_since_lsr = htonl(atoll(jsonData_report_block->getValue("dlsr").c_str()));
		buffer->add(&report_block, sizeof(report_block));
	}
	
	return(true);
}
