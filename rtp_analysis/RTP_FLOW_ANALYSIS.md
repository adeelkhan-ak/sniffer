# VoIPmonitor RTP Flow Analysis & Correlation

## Overview

This document provides an in-depth analysis of RTP (Real-time Transport Protocol) flow processing in VoIPmonitor, including packet processing, quality analysis, correlation with SIP calls, and statistical calculations.

## RTP Processing Architecture

### Core RTP Components

#### 1. RTP Class Structure
```cpp
class RTP {
    // Core identifiers
    u_int32_t ssrc;           // Synchronization source identifier
    vmIP saddr, daddr;        // Source and destination IP addresses
    vmPort sport, dport;      // Source and destination ports
    
    // Packet processing
    u_int16_t seq;            // Current sequence number
    u_int32_t last_ts;        // Last timestamp
    int codec;                // Audio codec type
    
    // Quality metrics
    struct stats_t stats;     // Packet loss, jitter, delay statistics
    struct rtcp_t rtcp;       // RTCP feedback data
    
    // Audio processing
    struct ast_channel *channel_record;  // Jitter buffer channel
    struct dsp *DSP;          // Digital signal processing
    
    // Quality scores
    uint8_t mosf1_min, mosf2_min, mosAD_min;  // MOS scores
    float mosf1_avg, mosf2_avg, mosAD_avg;    // Average MOS
};
```

#### 2. RTP Header Processing
```cpp
struct RTPFixedHeader {
    unsigned char version:2;    // RTP version (always 2)
    unsigned char padding:1;    // Padding flag
    unsigned char extension:1;  // Extension flag
    unsigned char cc:4;         // CSRC count
    unsigned char marker:1;     // Marker bit
    unsigned char payload:7;    // Payload type
    u_int16_t sequence;         // Sequence number
    u_int32_t timestamp;        // RTP timestamp
    u_int32_t sources[1];       // SSRC identifier
};
```

## RTP Flow Processing Pipeline

### 1. Packet Reception and Validation

#### Initial Processing
1. **Packet Arrival**: RTP packets arrive via network interface
2. **Protocol Detection**: Identified as RTP based on port and payload analysis
3. **SSRC Validation**: Check synchronization source identifier
4. **Sequence Validation**: Validate sequence number progression
5. **Timestamp Analysis**: Analyze RTP timestamp for timing

#### Correlation with SIP Call
```cpp
bool Call::read_rtp(CallBranch *c_branch, packet_s_process_0 *packetS, 
                   int iscaller, bool find_by_dest, bool stream_in_multiple_calls,
                   s_sdp_flags_base sdp_flags, char enable_save_packet) {
    
    // Find or create RTP stream based on IP:Port
    RTP *rtp = findRtpStream(packetS->saddr, packetS->daddr, 
                            packetS->sport, packetS->dport);
    
    // Process RTP packet
    return rtp->read(c_branch, packetS->data, packetS->header_ip, 
                    &packetS->datalen, packetS->header_pt, 
                    packetS->saddr, packetS->daddr, 
                    packetS->sport, packetS->dport,
                    packetS->sensor_id, packetS->sensor_ip);
}
```

### 2. RTP Stream Management

#### Stream Identification
- **SSRC Mapping**: Each RTP stream identified by unique SSRC
- **IP:Port Correlation**: Associated with specific network endpoints
- **Call Association**: Linked to parent SIP call via CallBranch
- **Direction Detection**: Caller vs Called stream identification

#### Stream Lifecycle
1. **Stream Creation**: New RTP object created on first packet
2. **Codec Negotiation**: Payload type determines audio codec
3. **Quality Monitoring**: Continuous analysis during active period
4. **Stream Termination**: Cleanup when call ends or stream stops

### 3. Packet Processing Flow

#### Core Processing Steps
```cpp
bool RTP::read(CallBranch *c_branch, unsigned char* data, 
               iphdr2 *header_ip, unsigned *len, struct pcap_pkthdr *header,
               vmIP saddr, vmIP daddr, vmPort sport, vmPort dport,
               int sensor_id, vmIP sensor_ip, char *ifname) {
    
    // 1. Basic validation
    if (stopReadProcessing) return false;
    
    // 2. Extract RTP header
    RTPFixedHeader *rtp_header = (RTPFixedHeader*)data;
    
    // 3. Sequence number analysis
    processSequenceNumber(rtp_header->sequence);
    
    // 4. Timestamp processing
    processTimestamp(rtp_header->timestamp);
    
    // 5. Payload processing
    processPayload(data + sizeof(RTPFixedHeader), 
                  *len - sizeof(RTPFixedHeader));
    
    // 6. Quality analysis
    calculateQualityMetrics();
    
    // 7. Jitter buffer processing
    jitterbuffer(channel_record, save_audio, energylevels, mos_lqo);
    
    // 8. Statistics update
    updateStatistics();
    
    return true;
}
```

## Quality Analysis Framework

### 1. Packet Loss Detection

#### Sequence Gap Analysis
```cpp
void RTP::processSequenceNumber(u_int16_t new_seq) {
    if (last_seq != -1) {
        int seq_diff = (new_seq - last_seq) & 0xFFFF;
        
        if (seq_diff > 1) {
            // Packet loss detected
            u_int32_t lost_packets = seq_diff - 1;
            stats.lost += lost_packets;
            
            // Update loss statistics by range
            if (lost_packets <= 10) {
                stats.slost[lost_packets]++;
            } else {
                stats.slost[10]++;  // 10+ losses
            }
        }
    }
    
    last_seq = new_seq;
    stats.received++;
}
```

#### Loss Percentage Calculation
```cpp
double RTP::calculateLossPercentage() {
    if (stats.received == 0) return 0.0;
    
    u_int32_t total_expected = stats.received + stats.lost;
    return (double)stats.lost / total_expected * 100.0;
}
```

### 2. Jitter Analysis

#### RFC 3550 Jitter Calculation
```cpp
void RTP::calculateJitter(u_int32_t timestamp, struct timeval arrival_time) {
    if (s->lastTimeStamp != 0) {
        // Calculate interarrival jitter per RFC 3550
        int32_t timestamp_diff = timestamp - s->lastTimeStamp;
        int64_t arrival_diff = getTimeUS(arrival_time) - getTimeUS(s->lastTimeRec);
        
        int32_t transit_diff = abs(arrival_diff - timestamp_diff * 125); // 125us per timestamp unit for 8kHz
        
        // Exponential smoothing: J(i) = J(i-1) + (|D(i-1,i)| - J(i-1))/16
        s->jitter += (transit_diff - s->jitter) / 16.0;
        
        // Update statistics
        if (s->jitter > stats.maxjitter) {
            stats.maxjitter = s->jitter;
        }
        stats.avgjitter = (stats.avgjitter * (stats.received - 1) + s->jitter) / stats.received;
    }
    
    s->lastTimeStamp = timestamp;
    s->lastTimeRec = arrival_time;
}
```

### 3. Delay Analysis

#### End-to-End Delay Calculation
```cpp
void RTP::calculateDelay() {
    // Delay buckets for histogram analysis
    if (s->delay < 50) stats.d50++;
    else if (s->delay < 70) stats.d70++;
    else if (s->delay < 90) stats.d90++;
    else if (s->delay < 120) stats.d120++;
    else if (s->delay < 150) stats.d150++;
    else if (s->delay < 200) stats.d200++;
    else if (s->delay < 300) stats.d300++;
}
```

### 4. MOS (Mean Opinion Score) Calculation

#### ITU-T G.107 E-Model Implementation
```cpp
double calculate_mos(double packet_loss_percent, double burst_ratio, 
                    int codec, unsigned int received, bool call_connected) {
    
    // Base quality for codec
    double base_mos = 4.5; // G.711 baseline
    
    // Packet loss impact
    double loss_factor = packet_loss_percent * 2.5;
    
    // Burst loss penalty
    double burst_factor = burst_ratio * 1.5;
    
    // Calculate R-factor
    double r_factor = 93.2 - loss_factor - burst_factor;
    
    // Convert R-factor to MOS
    double mos;
    if (r_factor < 0) mos = 1.0;
    else if (r_factor > 100) mos = 4.5;
    else if (r_factor < 60) mos = 1.0 + 0.035 * r_factor + r_factor * (r_factor - 60) * (100 - r_factor) * 7e-6;
    else mos = 1.0 + 0.035 * r_factor + r_factor * (r_factor - 60) * (100 - r_factor) * 7e-6;
    
    return mos;
}
```

#### Multiple MOS Calculations
```cpp
void RTP::updateMOSScores() {
    // MOS F1 - Fixed jitter buffer (200ms)
    if (channel_fix1) {
        double burst_ratio, loss_ratio;
        burstr_calculate(channel_fix1, stats.received, &burst_ratio, &loss_ratio, 1);
        last_interval_mosf1 = calculate_mos_fromrtp(this, 1, 1);
    }
    
    // MOS F2 - Fixed jitter buffer (500ms)  
    if (channel_fix2) {
        double burst_ratio, loss_ratio;
        burstr_calculate(channel_fix2, stats.received, &burst_ratio, &loss_ratio, 1);
        last_interval_mosf2 = calculate_mos_fromrtp(this, 2, 1);
    }
    
    // MOS Adaptive - Adaptive jitter buffer
    if (channel_adapt) {
        double burst_ratio, loss_ratio;
        burstr_calculate(channel_adapt, stats.received, &burst_ratio, &loss_ratio, 1);
        last_interval_mosAD = calculate_mos_fromrtp(this, 3, 1);
    }
}
```

## Jitter Buffer Processing

### 1. Asterisk Jitter Buffer Integration

#### Buffer Management
```cpp
void RTP::jitterbuffer(struct ast_channel *channel, bool save_audio, 
                      bool energylevels, bool mos_lqo) {
    
    // Create frame for jitter buffer
    struct ast_frame frame;
    frame.frametype = AST_FRAME_VOICE;
    frame.subclass.codec = codec;
    frame.data.ptr = payload_data;
    frame.datalen = payload_len;
    frame.samples = getSamplesFromCodec(codec, payload_len);
    frame.seq = seq;
    frame.ts = getTimeMS(header_ts);
    
    // Put frame into jitter buffer
    int jb_result = ast_jb_put(channel, &frame);
    
    if (jb_result == JB_IMPL_OK) {
        // Successfully buffered
        struct ast_frame *jb_frame;
        while ((jb_frame = ast_jb_read(channel)) != NULL) {
            // Process buffered frame
            processBufferedFrame(jb_frame);
            ast_frfree(jb_frame);
        }
    }
}
```

#### Buffer Configuration
```cpp
struct ast_jb_conf {
    long max_size;           // Maximum buffer size (ms)
    long resync_threshold;   // Resync threshold (ms)  
    long impl_type;          // Implementation type
    long target_extra;       // Target extra delay (ms)
};

// Different buffer configurations for MOS calculation
ast_jb_conf jb_conf_f1 = {200, 1000, JB_IMPL_FIXED, 40};      // Fixed 200ms
ast_jb_conf jb_conf_f2 = {500, 1000, JB_IMPL_FIXED, 40};      // Fixed 500ms  
ast_jb_conf jb_conf_adapt = {500, 1000, JB_IMPL_ADAPTIVE, 40}; // Adaptive
```

### 2. Packet Loss Concealment

#### Missing Packet Handling
```cpp
void RTP::handleMissingPackets(u_int16_t expected_seq, u_int16_t received_seq) {
    u_int16_t missing_count = received_seq - expected_seq;
    
    for (u_int16_t i = 0; i < missing_count; i++) {
        // Generate silence frame for missing packet
        struct ast_frame silence_frame;
        silence_frame.frametype = AST_FRAME_VOICE;
        silence_frame.subclass.codec = codec;
        silence_frame.data.ptr = generateSilence(codec);
        silence_frame.datalen = getFrameSize(codec);
        silence_frame.samples = getSamplesFromCodec(codec, silence_frame.datalen);
        silence_frame.seq = expected_seq + i;
        
        // Insert into jitter buffer
        ast_jb_put(channel_record, &silence_frame);
    }
}
```

## RTCP Integration

### 1. RTCP Feedback Processing

#### Receiver Reports
```cpp
void RTP::processRTCPReceiverReport(RTCPReceiverReport *rr) {
    rtcp.loss = rr->fraction_lost;
    rtcp.maxfr = max(rtcp.maxfr, rr->fraction_lost);
    rtcp.avgfr = (rtcp.avgfr * rtcp.counter + rr->fraction_lost) / (rtcp.counter + 1);
    
    rtcp.maxjitter = max(rtcp.maxjitter, rr->interarrival_jitter);
    rtcp.avgjitter = (rtcp.avgjitter * rtcp.jitt_counter + rr->interarrival_jitter) / (rtcp.jitt_counter + 1);
    
    rtcp.counter++;
    rtcp.jitt_counter++;
}
```

#### Sender Reports
```cpp
void RTP::processRTCPSenderReport(RTCPSenderReport *sr) {
    // Calculate round-trip delay
    if (rtcp.last_lsr == sr->lsr) {
        u_int32_t delay = getCurrentNTPTime() - sr->lsr - rtcp.last_lsr_delay;
        rtcp.rtd_sum += delay;
        rtcp.rtd_count++;
        rtcp.rtd_max = max(rtcp.rtd_max, delay);
    }
    
    rtcp.last_lsr = sr->lsr;
    rtcp.lsr4compare = sr->lsr;
}
```

## Codec Processing

### 1. Codec Detection and Handling

#### Payload Type Mapping
```cpp
int RTP::getCodecFromPayloadType(int payload_type) {
    switch (payload_type) {
        case 0: return PAYLOAD_PCMU;     // G.711 μ-law
        case 8: return PAYLOAD_PCMA;     // G.711 A-law  
        case 18: return PAYLOAD_G729;    // G.729
        case 9: return PAYLOAD_G722;     // G.722
        case 4: return PAYLOAD_G723;     // G.723
        case 96: return PAYLOAD_OPUS;    // Opus (dynamic)
        default: return -1;              // Unknown
    }
}
```

#### Dynamic Payload Handling
```cpp
void RTP::processDynamicPayload(int payload_type, const char *codec_name) {
    for (int i = 0; i < MAX_RTPMAP; i++) {
        if (rtpmap[i].payload == payload_type) {
            if (strcasecmp(codec_name, "OPUS") == 0) {
                rtpmap[i].codec = PAYLOAD_OPUS;
                rtpmap[i].frame_size = 20; // 20ms default
                rtpmap[i].bit_rate = 32000; // 32kbps default
            }
            // Add other dynamic codecs...
            break;
        }
    }
}
```

### 2. Audio Processing

#### Sample Rate Determination
```cpp
unsigned int RTP::getSampleRate() {
    switch (codec) {
        case PAYLOAD_G722:
        case PAYLOAD_G7221:
        case PAYLOAD_G722116:
            return 16000;
        case PAYLOAD_G722132:
        case PAYLOAD_ISAC32:
            return 32000;
        case PAYLOAD_OPUS48:
        case PAYLOAD_G722148:
            return 48000;
        default:
            return 8000;  // Most codecs use 8kHz
    }
}
```

#### Frame Size Calculation
```cpp
int RTP::getFrameSize(int codec_type) {
    switch (codec_type) {
        case PAYLOAD_PCMU:
        case PAYLOAD_PCMA:
            return packetization * 8;  // 8 bytes per ms for G.711
        case PAYLOAD_G729:
            return packetization / 10; // 10 bytes per 10ms for G.729
        case PAYLOAD_G723:
            return (packetization == 30) ? 24 : 20; // G.723 frame sizes
        default:
            return packetization * 8;
    }
}
```

## Statistical Analysis

### 1. Real-time Statistics

#### Sliding Window Analysis
```cpp
class RTPStatistics {
    struct TimeWindow {
        u_int64_t start_time;
        u_int64_t end_time;
        u_int32_t packets_received;
        u_int32_t packets_lost;
        double avg_jitter;
        double max_jitter;
        uint8_t min_mos;
        double avg_mos;
    };
    
    std::deque<TimeWindow> windows;
    static const int WINDOW_SIZE_MS = 5000; // 5 second windows
    
public:
    void updateWindow(RTP *rtp) {
        u_int64_t current_time = getTimeMS();
        
        // Create new window if needed
        if (windows.empty() || 
            current_time - windows.back().start_time > WINDOW_SIZE_MS) {
            
            TimeWindow new_window;
            new_window.start_time = current_time;
            new_window.end_time = current_time + WINDOW_SIZE_MS;
            windows.push_back(new_window);
            
            // Remove old windows (keep last 12 = 1 minute)
            while (windows.size() > 12) {
                windows.pop_front();
            }
        }
        
        // Update current window
        TimeWindow &current = windows.back();
        current.packets_received = rtp->stats.received;
        current.packets_lost = rtp->stats.lost;
        current.avg_jitter = rtp->stats.avgjitter;
        current.max_jitter = rtp->stats.maxjitter;
    }
};
```

### 2. Database Storage

#### RTP Statistics Table Updates
```cpp
void RTP::saveStatistics() {
    SqlDb_row rtp_stat_row;
    
    rtp_stat_row.add(saddr, "saddr");
    rtp_stat_row.add(daddr, "daddr"); 
    rtp_stat_row.add(sport, "sport");
    rtp_stat_row.add(dport, "dport");
    rtp_stat_row.add(ssrc, "ssrc");
    
    // Packet statistics
    rtp_stat_row.add(stats.received, "received");
    rtp_stat_row.add(stats.lost, "lost");
    rtp_stat_row.add((int)(stats.lost * 1000.0 / (stats.received + stats.lost)), "packet_loss_perc_mult1000");
    
    // Jitter statistics  
    rtp_stat_row.add((int)(stats.avgjitter * 10), "avgjitter_mult10");
    rtp_stat_row.add((int)(stats.maxjitter * 10), "maxjitter_mult10");
    
    // Delay histogram
    rtp_stat_row.add(stats.d50, "d50");
    rtp_stat_row.add(stats.d70, "d70");
    rtp_stat_row.add(stats.d90, "d90");
    rtp_stat_row.add(stats.d120, "d120");
    rtp_stat_row.add(stats.d150, "d150");
    rtp_stat_row.add(stats.d200, "d200");
    rtp_stat_row.add(stats.d300, "d300");
    
    // Loss histogram
    for (int i = 1; i <= 10; i++) {
        rtp_stat_row.add(stats.slost[i], string("sl") + intToString(i));
    }
    
    // MOS scores
    rtp_stat_row.add(last_interval_mosf1, "mos_f1_mult10");
    rtp_stat_row.add(last_interval_mosf2, "mos_f2_mult10"); 
    rtp_stat_row.add(last_interval_mosAD, "mos_adapt_mult10");
    
    // Save to database
    sqlStore->query_lock("INSERT INTO rtp_stat SET " + rtp_stat_row.implodeFields());
}
```

## Correlation Architecture

### 1. Call-RTP Association

#### Stream Mapping
```cpp
class Call {
    // RTP stream arrays indexed by direction
    RTP *rtp[2][MAX_SSRC_PER_CALL_FIX];  // [caller/called][ssrc_index]
    int rtp_size[2];                      // Number of streams per direction
    
    // IP:Port to RTP mapping
    map<ip_port_call_info*, RTP*> ip_port_rtp_map;
    
public:
    RTP* findRTPByIPPort(vmIP saddr, vmIP daddr, vmPort sport, vmPort dport) {
        ip_port_call_info key(saddr, daddr, sport, dport);
        auto it = ip_port_rtp_map.find(&key);
        return (it != ip_port_rtp_map.end()) ? it->second : nullptr;
    }
    
    void addRTPStream(RTP *rtp_stream, int iscaller) {
        if (rtp_size[iscaller] < MAX_SSRC_PER_CALL_FIX) {
            rtp[iscaller][rtp_size[iscaller]] = rtp_stream;
            rtp_size[iscaller]++;
            
            // Add to IP:Port mapping
            ip_port_call_info *ip_port = new ip_port_call_info(
                rtp_stream->saddr, rtp_stream->daddr,
                rtp_stream->sport, rtp_stream->dport);
            ip_port_rtp_map[ip_port] = rtp_stream;
        }
    }
};
```

### 2. SDP-RTP Correlation

#### Media Session Setup
```cpp
void Call::processSDP(const char *sdp_content, bool iscaller) {
    // Parse SDP for media information
    SDPParser parser(sdp_content);
    
    vector<SDPMedia> media_lines = parser.getMediaLines();
    
    for (auto &media : media_lines) {
        if (media.type == "audio") {
            // Create IP:Port entry for expected RTP stream
            ip_port_call_info *ip_port = new ip_port_call_info();
            ip_port->addr = media.connection_ip;
            ip_port->port = media.port;
            ip_port->iscaller = iscaller;
            ip_port->rtpmap = media.rtpmap;
            
            // Add to expected streams
            ip_ports.push_back(ip_port);
            
            // Pre-create RTP object if needed
            if (opt_pre_create_rtp_streams) {
                RTP *rtp = new RTP(sensor_id, sensor_ip);
                rtp->saddr = iscaller ? sipcallerip : sipcalledip;
                rtp->daddr = media.connection_ip;
                rtp->sport = 0; // Will be set when first packet arrives
                rtp->dport = media.port;
                rtp->iscaller = iscaller;
                
                addRTPStream(rtp, iscaller);
            }
        }
    }
}
```

### 3. Multi-Stream Handling

#### Stream Multiplexing
```cpp
void Call::handleMultipleStreams() {
    // Handle multiple RTP streams per direction
    for (int direction = 0; direction < 2; direction++) {
        for (int i = 0; i < rtp_size[direction]; i++) {
            RTP *stream = rtp[direction][i];
            
            if (stream && stream->stats.received > 0) {
                // Determine primary stream (highest packet count)
                if (!primary_rtp[direction] || 
                    stream->stats.received > primary_rtp[direction]->stats.received) {
                    primary_rtp[direction] = stream;
                }
                
                // Handle codec changes
                if (stream->codecchanged) {
                    handleCodecChange(stream, direction);
                }
                
                // Handle SSRC changes  
                if (stream->ssrc != expected_ssrc[direction]) {
                    handleSSRCChange(stream, direction);
                }
            }
        }
    }
}
```

This comprehensive analysis covers the complete RTP flow processing in VoIPmonitor, from packet reception through quality analysis and database storage. The system provides sophisticated real-time analysis capabilities for VoIP network monitoring and troubleshooting.