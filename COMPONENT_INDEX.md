# VoIPmonitor Component Index

## Core System Components

### Application Framework
| File | Lines | Purpose | Key Classes/Functions |
|------|-------|---------|----------------------|
| `voipmonitor.cpp` | 10,342 | Main application entry point | `main()`, initialization, signal handling |
| `voipmonitor.h` | 514 | Core system definitions | Global variables, system state |
| `voipmonitor_define.h` | 79 | System constants and version | Version info, feature flags, constants |
| `common.h` | 190 | Common data structures | `sVerbose`, platform definitions |

### Configuration Management
| File | Lines | Purpose | Key Classes/Functions |
|------|-------|---------|----------------------|
| `config_param.cpp` | 2,500+ | Configuration parameter handling | `cConfigItem`, parameter validation |
| `config_param.h` | 771 | Configuration class definitions | Configuration item types |
| `config/voipmonitor.conf` | 2,109 | Default configuration file | All system parameters |

## Packet Processing Pipeline

### Core Packet Handling
| File | Lines | Purpose | Key Classes/Functions |
|------|-------|---------|----------------------|
| `sniff.cpp` | 14,000+ | Main packet processing engine | `process_packet()`, protocol detection |
| `sniff.h` | 1,589 | Packet processing definitions | `packet_s`, `packet_s_process` |
| `pcap_queue.cpp` | 11,000+ | Packet queuing system | `pcap_block_store`, packet buffering |
| `pcap_queue.h` | 1,539 | Queue class definitions | Queue management classes |
| `pcap_queue_block.h` | 1,500+ | Block storage for packets | Memory-efficient packet storage |

### Network Layer Processing
| File | Lines | Purpose | Key Classes/Functions |
|------|-------|---------|----------------------|
| `ip_frag.cpp` | 1,000+ | IP fragmentation handling | Fragment reassembly |
| `ip_frag.h` | 300+ | IP fragmentation definitions | Fragment management |
| `tcpreassembly.cpp` | 4,000+ | TCP stream reassembly | TCP segment reconstruction |
| `tcpreassembly.h` | 1,236 | TCP reassembly definitions | Stream management |

## Protocol Implementation

### SIP Protocol
| File | Lines | Purpose | Key Classes/Functions |
|------|-------|---------|----------------------|
| `sniff.cpp` | (part) | SIP message processing | SIP parsing, call state management |
| `register.cpp` | 2,000+ | SIP registration handling | Registration tracking |
| `register.h` | 350+ | Registration definitions | Registration data structures |

### RTP/RTCP Processing
| File | Lines | Purpose | Key Classes/Functions |
|------|-------|---------|----------------------|
| `rtp.cpp` | 4,000+ | RTP stream processing | `RTP` class, stream analysis |
| `rtp.h` | 996 | RTP definitions | RTP structures, quality metrics |
| `rtcp.cpp` | 1,000+ | RTCP statistics processing | RTCP report handling |
| `rtcp.h` | 200+ | RTCP definitions | RTCP structures |

### Additional Protocols
| File | Lines | Purpose | Key Classes/Functions |
|------|-------|---------|----------------------|
| `mgcp.cpp` | 800+ | MGCP protocol support | MGCP message handling |
| `mgcp.h` | 300+ | MGCP definitions | MGCP structures |
| `skinny.cpp` | 2,500+ | SKINNY/SCCP protocol | Cisco protocol support |
| `skinny.h` | 500+ | SKINNY definitions | SKINNY message types |
| `webrtc.cpp` | 327 | WebRTC support | WebRTC signaling |
| `webrtc.h` | 135 | WebRTC definitions | WebRTC structures |
| `diameter.cpp` | 750 | Diameter protocol | AAA protocol support |
| `diameter.h` | 215 | Diameter definitions | Diameter message handling |

## Call Management

### Call Lifecycle
| File | Lines | Purpose | Key Classes/Functions |
|------|-------|---------|----------------------|
| `calltable.cpp` | 15,000+ | Call management system | `Call`, `CallBranch`, `Calltable` |
| `calltable.h` | 4,515 | Call definitions | Call structures, state management |
| `calltable_base.h` | 500+ | Base call definitions | Common call functionality |

### Call Quality & Analysis
| File | Lines | Purpose | Key Classes/Functions |
|------|-------|---------|----------------------|
| `dsp.cpp` | 1,637 | Digital signal processing | Audio analysis, MOS calculation |
| `dsp.h` | 477 | DSP definitions | Audio processing structures |

## Audio Processing

### Jitter Buffer (Asterisk-based)
| File | Lines | Purpose | Key Classes/Functions |
|------|-------|---------|----------------------|
| `jitterbuffer/abstract_jb.c` | 1,022 | Abstract jitter buffer | Jitter buffer interface |
| `jitterbuffer/fixedjitterbuf.c` | 580 | Fixed jitter buffer | Fixed-size buffer implementation |
| `jitterbuffer/jitterbuf.c` | 877 | Adaptive jitter buffer | Dynamic buffer sizing |

### Audio Codecs
| File | Lines | Purpose | Key Classes/Functions |
|------|-------|---------|----------------------|
| `codec_alaw.cpp` | 25 | A-law codec | G.711 A-law implementation |
| `codec_ulaw.cpp` | 25 | μ-law codec | G.711 μ-law implementation |
| `codecs.h` | 120+ | Codec definitions | Codec constants and structures |

## Database Layer

### Database Abstraction
| File | Lines | Purpose | Key Classes/Functions |
|------|-------|---------|----------------------|
| `sql_db.cpp` | 5,000+ | Database abstraction layer | `SqlDb`, connection management |
| `sql_db.h` | 1,646 | Database definitions | Database classes and structures |
| `sql_db_global.h` | 330 | Global database definitions | Database configuration |

## Security & Encryption

### SSL/TLS Processing
| File | Lines | Purpose | Key Classes/Functions |
|------|-------|---------|----------------------|
| `ssl.cpp` | 3,966 | SSL/TLS processing | TLS decryption, certificate handling |
| `ssl.h` | 1,573 | SSL definitions | SSL structures and classes |
| `ssl_dssl.cpp` | 1,507 | DSSL integration | SSL library integration |
| `ssl_dssl.h` | 363 | DSSL definitions | SSL processing definitions |
| `ssldata.cpp` | 829 | SSL data handling | SSL session management |
| `ssldata.h` | 106 | SSL data definitions | SSL data structures |

### DSSL Library (SSL Decryption)
| Directory | Purpose | Key Components |
|-----------|---------|----------------|
| `dssl/` | SSL/TLS decryption library | Session management, cipher suites, decryption |

### SRTP (Secure RTP)
| File | Lines | Purpose | Key Classes/Functions |
|------|-------|---------|----------------------|
| `srtp.cpp` | 887 | SRTP processing | Secure RTP decryption |
| `srtp.h` | 233 | SRTP definitions | SRTP structures |

### Fraud Detection
| File | Lines | Purpose | Key Classes/Functions |
|------|-------|---------|----------------------|
| `fraud.cpp` | 4,000+ | Fraud detection engine | Pattern analysis, alert generation |
| `fraud.h` | 1,429 | Fraud definitions | Fraud detection structures |

## Utilities & Tools

### Core Utilities
| File | Lines | Purpose | Key Classes/Functions |
|------|-------|---------|----------------------|
| `tools.cpp` | 11,106 | Utility functions | String handling, file operations, network utils |
| `tools.h` | 4,977 | Utility definitions | Utility classes and functions |
| `tools_global.cpp` | 3,250 | Global utilities | System-wide utility functions |
| `tools_global.h` | 1,436 | Global utility definitions | Global utility structures |

### Specialized Tools
| File | Lines | Purpose | Key Classes/Functions |
|------|-------|---------|----------------------|
| `tools_dynamic_buffer.cpp` | 1,810 | Dynamic buffer management | Memory-efficient buffers |
| `tools_dynamic_buffer.h` | 355 | Buffer definitions | Dynamic buffer classes |
| `tools_fifo_buffer.h` | 317 | FIFO buffer implementation | First-in-first-out buffers |
| `tools_tables_content.cpp` | 532 | Table content management | Database table utilities |

### Memory Management
| File | Lines | Purpose | Key Classes/Functions |
|------|-------|---------|----------------------|
| `heap_safe.h` | 500+ | Memory safety utilities | Heap corruption detection |
| `tcmalloc_hugetables.cpp` | 440 | TCMalloc integration | Memory allocation optimization |

## Monitoring & Management

### Management Interface
| File | Lines | Purpose | Key Classes/Functions |
|------|-------|---------|----------------------|
| `manager.cpp` | 4,000+ | Management interface | TCP control interface, monitoring |
| `manager.h` | 200+ | Management definitions | Management classes |

### Performance Monitoring
| File | Lines | Purpose | Key Classes/Functions |
|------|-------|---------|----------------------|
| `pstat.cpp` | 300+ | Performance statistics | CPU, memory usage tracking |
| `pstat.h` | 50+ | Performance definitions | Statistics structures |

### Statistics & Reporting
| File | Lines | Purpose | Key Classes/Functions |
|------|-------|---------|----------------------|
| `rrd.cpp` | 1,000+ | RRD database integration | Time-series data storage |
| `rrd.h` | 250+ | RRD definitions | RRD structures |

## Data Processing & Storage

### File Handling
| File | Lines | Purpose | Key Classes/Functions |
|------|-------|---------|----------------------|
| `tar.cpp` | 2,569 | TAR archive handling | Archive creation and management |
| `tar.h` | 580 | TAR definitions | Archive structures |
| `cleanspool.h` | 400+ | Spool directory cleanup | File cleanup utilities |

### Data Compression
| File | Lines | Purpose | Key Classes/Functions |
|------|-------|---------|----------------------|
| `tools.cpp` | (part) | Compression utilities | Gzip, LZ4 compression support |

## Network Services

### Cloud Integration
| Directory | Purpose | Key Components |
|-----------|---------|----------------|
| `cloud_router/` | Cloud connectivity | Client-server communication |

### WebSocket Support
| File | Lines | Purpose | Key Classes/Functions |
|------|-------|---------|----------------------|
| `websocket.cpp` | 66 | WebSocket implementation | WebSocket protocol support |
| `websocket.h` | 129 | WebSocket definitions | WebSocket structures |

## Specialized Features

### Country Detection
| File | Lines | Purpose | Key Classes/Functions |
|------|-------|---------|----------------------|
| `country_detect.cpp` | 1,305 | Geographic location detection | IP-to-country mapping |
| `country_detect.h` | 372 | Country detection definitions | Location structures |

### Transcription
| File | Lines | Purpose | Key Classes/Functions |
|------|-------|---------|----------------------|
| `transcribe.cpp` | 1,073 | Audio transcription | Speech-to-text processing |
| `transcribe.h` | 225 | Transcription definitions | Transcription structures |

### IP Accounting
| File | Lines | Purpose | Key Classes/Functions |
|------|-------|---------|----------------------|
| `ipaccount.cpp` | 1,500+ | IP traffic accounting | Bandwidth monitoring |
| `ipaccount.h` | 400+ | IP accounting definitions | Traffic accounting structures |

### IPFIX Support
| File | Lines | Purpose | Key Classes/Functions |
|------|-------|---------|----------------------|
| `ipfix.cpp` | 400+ | IPFIX protocol support | Flow export protocol |
| `ipfix.h` | 250+ | IPFIX definitions | Flow structures |

## Build System & Configuration

### Build Configuration
| File | Purpose |
|------|---------|
| `configure.in` | Autotools configuration script |
| `Makefile.in` | Makefile template |
| `config.h.in` | Configuration header template |

### Packaging
| Directory | Purpose |
|-----------|---------|
| `debian/` | Debian packaging files |
| `config/` | System configuration files |
| `scripts/` | Installation and utility scripts |

## Testing & Development

### Test Utilities
| File | Lines | Purpose |
|------|-------|---------|
| `tests_utils.cpp` | 1,793 | Testing utilities and functions |

### Development Tools
| File | Purpose |
|------|---------|
| `create_graphs.sh` | Graph generation script |
| `wireshark.cpp` | Wireshark integration |

This index provides a comprehensive reference for navigating the VoIPmonitor codebase, organized by functional areas and including file sizes and key components for each module.