# VoIPmonitor RTP Flow Diagrams Documentation

## Overview

This document provides detailed explanations for the comprehensive set of RTP (Real-time Transport Protocol) flow diagrams that illustrate the complex processing pipeline in VoIPmonitor. These diagrams show how RTP packets are captured, processed, analyzed for quality, and correlated with SIP calls.

## Diagram Index

1. **[RTP Packet Processing Flow](#1-rtp-packet-processing-flow)** - Core packet processing pipeline
2. **[RTP-SIP Correlation](#2-rtp-sip-correlation)** - How RTP streams associate with SIP calls
3. **[RTP Quality Analysis](#3-rtp-quality-analysis)** - Quality metrics calculation and MOS scoring
4. **[Jitter Buffer Processing](#4-jitter-buffer-processing)** - Jitter buffer management and audio processing
5. **[RTP-RTCP Correlation](#5-rtp-rtcp-correlation)** - RTCP feedback integration with RTP analysis
6. **[RTP Codec Processing](#6-rtp-codec-processing)** - Codec detection and audio processing

---

## 1. RTP Packet Processing Flow

**File**: `01_rtp_packet_processing_flow.mmd`

### Purpose
This diagram illustrates the complete pipeline from network packet arrival to RTP stream processing, including validation, stream management, and quality analysis initiation.

### Key Components

#### **Packet Reception (Blue - Input)**
- **Network Packet Arrival**: Entry point for all network traffic
- **Protocol Detection**: Determines if packet contains RTP data
- **Header Extraction**: Parses RTP header fields

#### **Validation (Green - Validation)**
- **Version Check**: Ensures RTP version 2 compliance
- **Stream Existence**: Checks if RTP stream already exists for this SSRC
- **Sequence Validation**: Validates packet sequence number progression

#### **Stream Management (Purple - Processing)**
- **SSRC Lookup**: Finds existing stream or creates new RTP object
- **Stream Initialization**: Sets up new RTP stream with default parameters
- **Call Association**: Links RTP stream to parent SIP call

#### **Quality Processing (Orange - Quality)**
- **Jitter Calculation**: RFC 3550 compliant jitter computation
- **Statistics Update**: Updates packet counts, loss statistics
- **MOS Calculation**: Initiates quality score computation

#### **Security Processing (Red - Security)**
- **SRTP Detection**: Identifies encrypted RTP streams
- **Key Derivation**: Derives decryption keys from SRTP context
- **Payload Decryption**: Decrypts SRTP payload for analysis

#### **Error Handling (Pink - Error)**
- **Packet Loss Detection**: Identifies sequence gaps
- **Loss Statistics**: Updates various loss counters and histograms
- **Invalid Packet Handling**: Processes malformed or invalid packets

### Data Flow
1. Packet arrives from network interface
2. Protocol detection identifies RTP traffic
3. Header parsing extracts key fields (SSRC, sequence, timestamp)
4. Stream lookup finds or creates RTP object
5. Security processing handles SRTP decryption if needed
6. Quality analysis begins with jitter and loss calculations
7. RTCP correlation enhances quality metrics
8. Results stored in database and forwarded to monitoring systems

---

## 2. RTP-SIP Correlation

**File**: `02_rtp_sip_correlation.mmd`

### Purpose
Shows how RTP media streams are associated with SIP signaling sessions, including SDP parsing, stream direction detection, and multi-stream handling.

### Key Components

#### **SIP Processing (Blue - SIP)**
- **SIP INVITE**: Initial call setup with SDP offer
- **SDP Parsing**: Extracts media session parameters
- **Re-INVITE Handling**: Manages mid-call parameter changes

#### **SDP Analysis (Purple - SDP)**
- **Media Line Extraction**: Parses audio/video media descriptions
- **Connection Information**: Extracts IP addresses and port numbers
- **Codec Negotiation**: Processes codec parameters and RTP maps

#### **RTP Association (Green - RTP)**
- **Packet Matching**: Correlates arriving RTP with SDP expectations
- **Direction Detection**: Determines caller vs called stream direction
- **Stream Initialization**: Sets up RTP processing for matched streams

#### **Correlation Logic (Orange - Correlation)**
- **IP:Port Matching**: Matches RTP endpoints with SDP media lines
- **Late Binding**: Handles RTP streams that arrive before SDP processing
- **NAT Handling**: Manages Network Address Translation scenarios

#### **Quality Monitoring (Red - Quality)**
- **Stream Multiplexing**: Handles multiple RTP streams per direction
- **Primary Stream Selection**: Identifies main media stream
- **Codec Synchronization**: Manages codec changes across streams

### Data Flow
1. SIP INVITE received with SDP offer
2. SDP parsed to extract media session parameters
3. Expected RTP streams registered with IP:Port combinations
4. RTP packets arrive and matched against expected streams
5. Direction determined (caller/called) and stream associated with call
6. Multiple streams handled with primary stream selection
7. Quality monitoring initiated for all associated streams
8. Call termination triggers RTP stream cleanup

---

## 3. RTP Quality Analysis

**File**: `03_rtp_quality_analysis.mmd`

### Purpose
Detailed breakdown of quality metrics calculation including packet loss analysis, jitter computation, delay measurement, and MOS (Mean Opinion Score) calculation using multiple jitter buffer configurations.

### Key Components

#### **Sequence Analysis (Purple - Sequence)**
- **Gap Detection**: Identifies missing sequence numbers
- **Loss Histogram**: Categorizes losses by burst size (sl1-sl10+)
- **Loss Percentage**: Calculates overall packet loss rate

#### **Timing Analysis (Green - Timing)**
- **Inter-arrival Time**: Measures packet arrival intervals
- **Transit Time**: Calculates network transit variations
- **RFC 3550 Jitter**: Standard jitter calculation with exponential smoothing

#### **Delay Analysis (Orange - Quality)**
- **End-to-End Delay**: Measures total packet delay
- **Delay Histogram**: Categorizes delays into buckets (d50, d70, d90, etc.)
- **Delay Statistics**: Maintains min/max/average delay metrics

#### **MOS Calculation (Red - MOS)**
- **Multiple Jitter Buffers**: Three parallel analysis paths
  - **F1**: Fixed 200ms buffer for conservative analysis
  - **F2**: Fixed 500ms buffer for liberal analysis  
  - **AD**: Adaptive buffer for optimal analysis
- **ITU-T G.107 E-Model**: Standard quality calculation
- **R-Factor Computation**: Converts loss/burst data to quality rating
- **MOS Conversion**: Transforms R-factor to 1-5 MOS scale

#### **RTCP Integration (Pink - RTCP)**
- **Receiver Reports**: Incorporates RTCP feedback data
- **Round-Trip Delay**: Uses RTCP for network delay measurement
- **Quality Correlation**: Validates RTP analysis with RTCP reports

#### **Codec Processing (Teal - Codec)**
- **Codec Detection**: Identifies audio codec type
- **Quality Models**: Applies codec-specific quality factors
- **Base Quality**: Sets codec-dependent baseline MOS scores

### Quality Metrics Generated
- **Packet Loss**: Total and percentage loss rates
- **Jitter**: Current, maximum, and average jitter values
- **Delay**: End-to-end delay distribution
- **MOS Scores**: Three different MOS calculations (F1, F2, AD)
- **Quality Alerts**: Real-time notifications for quality degradation

---

## 4. Jitter Buffer Processing

**File**: `04_jitter_buffer_processing.mmd`

### Purpose
Shows the sophisticated jitter buffer implementation using Asterisk's jitter buffer framework, including multiple buffer types, packet loss concealment, and quality metric calculation.

### Key Components

#### **Frame Creation (Purple - Frame)**
- **AST Frame Structure**: Creates Asterisk-compatible audio frames
- **Frame Properties**: Sets codec, samples, sequence, timestamp
- **Multiple Instances**: Parallel processing for different buffer types

#### **Buffer Management (Green - Buffer)**
- **Three Buffer Types**:
  - **Fixed 200ms (F1)**: Conservative fixed-size buffer
  - **Fixed 500ms (F2)**: Liberal fixed-size buffer
  - **Adaptive (AD)**: Dynamic size adjustment based on network conditions
- **Buffer States**: Handles OK, DROP, and INTERPOLATION conditions

#### **Audio Processing (Orange - Processing)**
- **Frame Buffering**: Manages packet ordering and timing
- **Audio Extraction**: Retrieves properly timed audio frames
- **Quality Metrics**: Calculates per-buffer quality statistics

#### **Loss Concealment (Pink - Concealment)**
- **Missing Packet Detection**: Identifies sequence gaps
- **Silence Generation**: Creates appropriate silence frames
- **Codec-Specific Handling**: Generates codec-appropriate concealment

#### **Buffer Optimization (Teal - Management)**
- **Size Management**: Monitors and adjusts buffer sizes
- **Overflow Handling**: Drops oldest frames when buffer full
- **Adaptive Resizing**: Dynamically adjusts buffer size (AD only)

#### **Quality Analysis (Light Pink - Quality)**
- **Burst Loss Calculation**: Analyzes loss patterns per buffer
- **Loss/Burst Ratios**: Calculates quality degradation factors
- **MOS Computation**: Generates quality scores for each buffer type

### Buffer Configurations
- **F1 (200ms Fixed)**: Simulates conservative receiver behavior
- **F2 (500ms Fixed)**: Simulates liberal receiver with high delay tolerance
- **AD (Adaptive)**: Simulates intelligent receiver with dynamic adjustment

### Quality Benefits
- **Multiple Perspectives**: Three different quality viewpoints
- **Network Adaptation**: Adaptive buffer responds to conditions
- **Comparative Analysis**: Identifies optimal buffer strategies
- **Loss Concealment**: Maintains audio continuity during packet loss

---

## 5. RTP-RTCP Correlation

**File**: `05_rtp_rtcp_correlation.mmd`

### Purpose
Demonstrates how RTCP (RTP Control Protocol) feedback is integrated with RTP stream analysis to provide enhanced quality metrics and validation of RTP-derived statistics.

### Key Components

#### **RTCP Processing (Purple - RTCP)**
- **Packet Type Detection**: Handles SR, RR, SDES, BYE, APP, XR packets
- **Sender Reports (SR)**: Extracts transmission statistics
- **Receiver Reports (RR)**: Processes reception quality feedback
- **Extended Reports (XR)**: Handles VoIP-specific quality metrics

#### **Statistical Correlation (Orange - Correlation)**
- **Round-Trip Time**: Calculates network RTT from SR/RR pairs
- **Loss Correlation**: Compares RTP vs RTCP loss measurements
- **Jitter Validation**: Cross-validates jitter calculations
- **Quality Consistency**: Identifies discrepancies between measurements

#### **Quality Enhancement (Red - Quality)**
- **RTCP Statistics**: Maintains separate RTCP-derived quality metrics
- **Confidence Scoring**: Validates RTP analysis with RTCP feedback
- **Alert Generation**: Flags inconsistencies for investigation
- **Enhanced Reporting**: Provides dual-source quality validation

#### **Database Integration (Pink - Database)**
- **RTCP Fields**: Stores RTCP-specific quality metrics in CDR
- **Correlation Data**: Maintains relationships between RTP and RTCP stats
- **Historical Analysis**: Enables trend analysis across both protocols

#### **Security Processing (Light Red - Security)**
- **SRTCP Handling**: Processes encrypted RTCP packets
- **Key Management**: Uses same keys as SRTP for RTCP decryption
- **Integrity Validation**: Verifies RTCP packet authenticity

### RTCP Metrics Collected
- **Round-Trip Delay**: Network latency measurements
- **Fraction Lost**: Receiver-reported loss rates
- **Jitter**: Receiver-calculated jitter values
- **Packet Counts**: Transmission/reception statistics
- **VoIP Metrics**: MOS scores from XR reports
- **Burst Analysis**: Loss pattern characteristics

### Correlation Benefits
- **Validation**: Cross-validates RTP-derived metrics
- **Accuracy**: Improves quality measurement confidence
- **Troubleshooting**: Identifies network vs endpoint issues
- **Comprehensive View**: Provides sender and receiver perspectives

---

## 6. RTP Codec Processing

**File**: `06_rtp_codec_processing.mmd`

### Purpose
Illustrates comprehensive codec detection, audio processing, and quality analysis including support for multiple audio codecs, dynamic payload types, and advanced audio processing features.

### Key Components

#### **Codec Detection (Purple - Detection)**
- **Payload Type Analysis**: Distinguishes static vs dynamic payload types
- **Static Codecs**: Handles standard codecs (G.711, G.729, G.722, etc.)
- **Dynamic Codecs**: Processes SDP RTPMAP for custom codecs (OPUS, etc.)
- **Codec Validation**: Ensures payload type consistency

#### **Codec Configuration (Green - Codec)**
- **Parameter Setting**: Configures sample rates, frame sizes, bit rates
- **Codec-Specific Setup**: Handles unique requirements per codec
- **Quality Baselines**: Sets codec-dependent quality expectations

#### **Audio Processing (Orange - Processing)**
- **Sample Calculation**: Determines samples per frame for each codec
- **Frame Size Computation**: Calculates appropriate frame boundaries
- **PCM Conversion**: Converts codec-specific formats to linear PCM

#### **Digital Signal Processing (Red - Audio)**
- **Energy Detection**: Measures audio signal energy levels
- **Voice Activity Detection**: Identifies speech vs silence periods
- **DTMF Detection**: Recognizes dual-tone multi-frequency signals
- **Silence Detection**: Tracks silent periods in audio stream

#### **Quality Analysis (Pink - Quality)**
- **Codec-Specific Models**: Applies appropriate quality calculations
- **Base MOS Scores**: Uses codec-dependent quality baselines
- **Loss Degradation**: Applies codec-specific loss penalties
- **Final Scoring**: Generates codec-adjusted quality metrics

#### **Recording & Transcoding (Teal - Recording)**
- **Audio Recording**: Saves processed audio to disk
- **Format Conversion**: Handles multiple output formats
- **Transcoding**: Converts between different codec formats
- **Compression**: Applies audio compression for storage

### Supported Codecs
- **G.711 (PCMU/PCMA)**: Standard telephony codec, 8kHz, 64kbps
- **G.729**: Low-bitrate codec, 8kHz, 8kbps
- **G.722**: Wideband codec, 16kHz, 64kbps
- **G.723**: Low-bitrate codec, 8kHz, 5.3/6.3kbps
- **OPUS**: Modern codec, up to 48kHz, variable bitrate
- **GSM**: Mobile codec, 8kHz, 13kbps

### Advanced Features
- **Multi-codec Calls**: Handles different codecs per direction
- **Codec Changes**: Manages mid-call codec transitions
- **Error Recovery**: Handles invalid or corrupted codec data
- **Quality Adaptation**: Adjusts analysis based on codec capabilities

---

## Usage Instructions

### Viewing Diagrams
1. **Mermaid Rendering**: Use any Mermaid-compatible viewer
2. **Online Viewers**: 
   - [Mermaid Live Editor](https://mermaid.live/)
   - [GitHub Markdown](https://github.com) (native support)
3. **IDE Extensions**: Install Mermaid extensions for VS Code, IntelliJ, etc.

### Diagram Navigation
- **Color Coding**: Each diagram uses consistent color coding for component types
- **Flow Direction**: All diagrams flow top-to-bottom showing process progression
- **Decision Points**: Diamond shapes indicate conditional logic
- **Process Blocks**: Rectangular shapes show processing steps
- **Data Stores**: Cylindrical shapes represent data storage

### Technical Integration
These diagrams directly correspond to the VoIPmonitor source code:
- **RTP Class**: Core RTP processing implementation
- **Call Class**: SIP-RTP correlation logic
- **Jitter Buffer**: Asterisk jitter buffer integration
- **Quality Analysis**: MOS calculation algorithms
- **Database Schema**: Statistical storage structures

### Performance Considerations
- **Real-time Processing**: All flows designed for real-time operation
- **Memory Management**: Efficient buffer and object lifecycle management
- **CPU Optimization**: Multi-threaded processing for high-volume environments
- **Storage Efficiency**: Compressed statistics and selective recording

This comprehensive analysis provides complete visibility into VoIPmonitor's RTP processing capabilities, enabling effective troubleshooting, optimization, and quality management for VoIP networks.