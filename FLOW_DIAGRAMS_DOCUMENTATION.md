# VoIPmonitor Flow Diagrams Documentation

This document provides detailed explanations for all the flow diagrams that illustrate the VoIPmonitor system architecture and data processing pipelines.

## Diagram Files Overview

| Diagram | File | Purpose |
|---------|------|---------|
| 1 | `01_main_packet_processing_flow.mmd` | Overall packet processing pipeline |
| 2 | `02_call_lifecycle_management.mmd` | SIP call state management |
| 3 | `03_rtp_stream_processing.mmd` | RTP media stream analysis |
| 4 | `04_database_storage_architecture.mmd` | Database storage and management |
| 5 | `05_multithreading_architecture.mmd` | Thread architecture and synchronization |
| 6 | `06_security_fraud_detection.mmd` | Security and fraud detection systems |
| 7 | `07_protocol_stack_overview.mmd` | Network protocol stack handling |
| 8 | `08_audio_processing_pipeline.mmd` | Audio codec and quality analysis |

## Detailed Diagram Explanations

### 1. Main Packet Processing Flow
**File**: `diagrams/01_main_packet_processing_flow.mmd`

This diagram shows the high-level packet processing pipeline from network capture to final analysis.

#### Key Components:
- **Network Interface**: Physical or virtual network interface where packets are captured
- **libpcap Capture**: Low-level packet capture using libpcap library
- **Packet Queue**: Memory-efficient queuing system for captured packets
- **Protocol Detection**: Automatic identification of VoIP protocols (SIP, RTP, RTCP, etc.)
- **Processing Modules**: Specialized handlers for each protocol type
- **Database Storage**: Persistent storage of processed data
- **Output Systems**: CDR generation, statistics, and fraud detection

#### Data Flow:
1. Raw packets captured from network interface
2. Packets queued in memory-efficient storage blocks
3. Protocol detection determines packet type
4. Appropriate processing module handles the packet
5. Results stored in database for analysis and reporting

### 2. Call Lifecycle Management
**File**: `diagrams/02_call_lifecycle_management.mmd`

This diagram illustrates the complete SIP call lifecycle from initiation to termination.

#### Key States:
- **INVITE**: Initial call setup request
- **RINGING (18x)**: Call is being established
- **ANSWERED (200)**: Call successfully connected
- **REJECTED (4xx/5xx)**: Call failed or rejected
- **ACTIVE**: Call in progress with media flow
- **BYE**: Call termination request

#### Advanced Features:
- **Call Transfer**: Handling of call transfers between parties
- **Call Hold**: Managing call hold and resume operations
- **Quality Analysis**: Real-time monitoring of call quality
- **CDR Generation**: Creation of detailed call records

#### Processing Flow:
1. SIP INVITE triggers call object creation
2. SDP parsing establishes media parameters
3. RTP streams are configured based on SDP
4. Call state machine manages all state transitions
5. Quality analysis runs throughout active call
6. CDR generated upon call completion

### 3. RTP Stream Processing
**File**: `diagrams/03_rtp_stream_processing.mmd`

This diagram details the processing of RTP media streams for quality analysis.

#### Core Processing Steps:
- **Sequence Check**: Validates RTP sequence numbers for packet loss detection
- **Timestamp Analysis**: Analyzes timing for jitter calculation
- **Payload Extraction**: Extracts audio/video data from RTP packets
- **Codec Detection**: Identifies audio codec (G.711, G.729, G.722, etc.)
- **Audio Decode**: Decodes compressed audio to PCM format

#### Quality Metrics:
- **Jitter Calculation**: Measures delay variation
- **Packet Loss Detection**: Identifies missing packets
- **MOS Calculation**: Mean Opinion Score for call quality
- **Burst Loss Analysis**: Detects periods of consecutive packet loss

#### Security Features:
- **SRTP Detection**: Identifies encrypted RTP streams
- **Key Derivation**: Extracts encryption keys for decryption
- **Secure Processing**: Handles encrypted media streams

### 4. Database Storage Architecture
**File**: `diagrams/04_database_storage_architecture.mmd`

This diagram shows the database layer architecture for persistent data storage.

#### Storage Components:
- **SQL Queue**: Thread-safe queue for database operations
- **Connection Pool**: Efficient database connection management
- **Data Classification**: Different data types routed to appropriate tables
- **Partitioning**: Time-based data partitioning for performance
- **Cleanup Process**: Automated old data removal

#### Data Types:
- **CDR Data**: Call detail records with quality metrics
- **SIP Messages**: Complete SIP message logging
- **RTP Statistics**: Detailed stream quality data
- **Registration Data**: SIP registration tracking
- **Fraud Events**: Security alert information
- **RTCP Reports**: Real-time control protocol data

#### Performance Features:
- **Connection Pooling**: Multiple database connections for scalability
- **Daily Partitions**: Time-based table partitioning
- **Index Optimization**: Automated index maintenance
- **Replication Support**: Master-slave database replication

### 5. Multithreading Architecture
**File**: `diagrams/05_multithreading_architecture.mmd`

This diagram illustrates the complex multithreading architecture used for high-performance processing.

#### Thread Categories:
- **Main Thread**: System initialization and coordination
- **Packet Capture Thread**: Network packet capture
- **Pre-process Thread**: Initial packet processing
- **Process Thread Pool**: SIP and protocol analysis
- **RTP Thread Pool**: Media stream processing
- **Database Store Threads**: Asynchronous database operations

#### Synchronization Mechanisms:
- **Lock-free Queues**: High-performance inter-thread communication
- **Thread Pool Manager**: Dynamic thread allocation
- **Load Balancing**: Work distribution across threads
- **Memory Barriers**: Memory ordering guarantees
- **Atomic Operations**: Thread-safe primitive operations

#### Management Threads:
- **Management Thread**: TCP control interface
- **Cleanup Thread**: Resource cleanup and maintenance
- **Statistics Thread**: Performance metrics collection

### 6. Security and Fraud Detection
**File**: `diagrams/06_security_fraud_detection.mmd`

This diagram shows the comprehensive security and fraud detection system.

#### Security Processing:
- **SSL/TLS Detection**: Identifies encrypted traffic
- **Certificate Validation**: X.509 certificate verification
- **Session Key Extraction**: Extracts keys for decryption
- **SRTP Processing**: Secure RTP stream handling

#### Fraud Detection Methods:
- **Call Pattern Analysis**: Statistical analysis of calling patterns
- **Geographic Analysis**: Location-based anomaly detection
- **Rate Limiting**: Frequency-based abuse detection
- **Registration Monitoring**: SIP registration abuse detection

#### Advanced Detection:
- **Machine Learning Models**: AI-based pattern recognition
- **Anomaly Detection**: Statistical outlier identification
- **Real-time Alerts**: Immediate notification system

#### Response Actions:
- **Alert Classification**: Categorization of fraud types
- **Database Logging**: Persistent fraud event storage
- **Blocking Actions**: Automated response mechanisms

### 7. Protocol Stack Overview
**File**: `diagrams/07_protocol_stack_overview.mmd`

This diagram provides a comprehensive view of all supported network protocols.

#### Network Layers:
- **Network Layer**: Ethernet, IP packet processing
- **Transport Layer**: UDP, TCP, SCTP support
- **Application Layer**: VoIP protocol implementations

#### VoIP Protocols:
- **SIP**: Session Initiation Protocol (UDP/TCP)
- **RTP/RTCP**: Real-time Transport Protocol
- **MGCP**: Media Gateway Control Protocol
- **SKINNY/SCCP**: Cisco proprietary protocol
- **SS7/SIGTRAN**: Signaling System 7
- **WebRTC**: Web Real-Time Communication

#### Security Protocols:
- **TLS/SSL**: Transport Layer Security
- **SRTP**: Secure Real-time Transport Protocol
- **Certificate Handling**: X.509 certificate processing

#### Special Features:
- **Fragmentation Handling**: IP fragment reassembly
- **QoS Analysis**: DSCP marking and quality of service
- **NAT Traversal**: STUN/TURN protocol support

### 8. Audio Processing Pipeline
**File**: `diagrams/08_audio_processing_pipeline.mmd`

This diagram details the complete audio processing and analysis pipeline.

#### Codec Support:
- **G.711**: A-law and μ-law variants
- **G.729**: Low-bitrate codec
- **G.722**: Wideband audio codec
- **G.723**: Low-bitrate speech codec
- **Generic Decoder**: Support for additional codecs

#### Audio Processing:
- **PCM Conversion**: Convert to uncompressed audio
- **Jitter Buffer**: Adaptive buffering for smooth playback
- **Buffer Management**: Underrun/overrun handling
- **Packet Loss Concealment**: Audio quality improvement

#### Quality Analysis:
- **Volume Analysis**: Audio level monitoring
- **Frequency Analysis**: Spectral analysis
- **DTMF Detection**: Dual-tone multi-frequency recognition
- **Voice Activity Detection**: Speech/silence detection
- **MOS Calculation**: Mean Opinion Score computation

#### Output Formats:
- **Recording Formats**: WAV, OGG, MP3 support
- **Multi-channel Mixing**: Stereo audio combination
- **Transcription**: Speech-to-text conversion
- **File Storage**: Persistent audio storage

## Usage Instructions

### Viewing Diagrams
1. Use any Mermaid-compatible viewer or editor
2. Copy the diagram content from the `.mmd` files
3. Paste into online Mermaid editors like:
   - [Mermaid Live Editor](https://mermaid.live/)
   - [Draw.io](https://app.diagrams.net/) (with Mermaid plugin)
   - GitHub/GitLab (native Mermaid support)

### Editing Diagrams
1. Modify the `.mmd` files directly
2. Follow Mermaid syntax guidelines
3. Test changes in a Mermaid editor
4. Update this documentation if flow changes

### Integration with Documentation
These diagrams are referenced in:
- `CODEBASE_ANALYSIS.md` - Main architecture documentation
- `COMPONENT_INDEX.md` - Component reference guide
- Development documentation and presentations

## Technical Notes

### Styling
Each diagram uses consistent color coding:
- **Blue tones**: Input/Network layers
- **Purple tones**: Protocol processing
- **Green tones**: Core processing
- **Orange tones**: Storage/Database
- **Pink tones**: Output/Management
- **Red tones**: Security/Alerts

### Performance Considerations
The diagrams reflect the high-performance architecture:
- Lock-free data structures
- Memory-efficient processing
- Parallel processing pipelines
- Optimized database operations
- Real-time processing capabilities

### Scalability Features
Architecture supports:
- Horizontal scaling through threading
- Database partitioning and replication
- Load balancing across processing threads
- Memory and CPU optimization techniques
- High-volume packet processing (millions of packets per second)

This documentation provides the foundation for understanding VoIPmonitor's complex architecture through visual flow diagrams and detailed explanations.