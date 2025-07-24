# VoIPmonitor Codebase Analysis & Documentation

This repository contains comprehensive analysis documentation for the VoIPmonitor C++ network packet sniffer project. The documentation provides detailed insights into the system architecture, component organization, and data flow patterns.

## 📋 Documentation Overview

### Core Analysis Documents

| Document | Purpose | Content |
|----------|---------|---------|
| **[CODEBASE_ANALYSIS.md](CODEBASE_ANALYSIS.md)** | Complete system architecture analysis | 10 major components, algorithms, performance optimizations |
| **[COMPONENT_INDEX.md](COMPONENT_INDEX.md)** | File-by-file component reference | 150+ files indexed by functional area |
| **[FLOW_DIAGRAMS_DOCUMENTATION.md](FLOW_DIAGRAMS_DOCUMENTATION.md)** | Detailed diagram explanations | Technical documentation for all flow diagrams |
| **[API_REFERENCE.md](API_REFERENCE.md)** | Programming interface documentation | Classes, functions, constants, usage examples |
| **[DATABASE_SCHEMA.md](DATABASE_SCHEMA.md)** | Database structure and optimization | Tables, indexes, queries, partitioning strategy |
| **[DEPLOYMENT_GUIDE.md](DEPLOYMENT_GUIDE.md)** | Installation and configuration guide | System setup, performance tuning, troubleshooting |
| **[.cursorrules](.cursorrules)** | Development guidelines | Coding standards and architectural patterns |

### Visual Flow Diagrams

| Diagram | File | Description |
|---------|------|-------------|
| **Main Processing** | [`01_main_packet_processing_flow.mmd`](diagrams/01_main_packet_processing_flow.mmd) | Network → Protocol Detection → Analysis → Storage |
| **Call Lifecycle** | [`02_call_lifecycle_management.mmd`](diagrams/02_call_lifecycle_management.mmd) | SIP call states and transitions |
| **RTP Processing** | [`03_rtp_stream_processing.mmd`](diagrams/03_rtp_stream_processing.mmd) | Media stream analysis and quality metrics |
| **Database Architecture** | [`04_database_storage_architecture.mmd`](diagrams/04_database_storage_architecture.mmd) | Storage layer and data management |
| **Threading Model** | [`05_multithreading_architecture.mmd`](diagrams/05_multithreading_architecture.mmd) | Thread hierarchy and synchronization |
| **Security Systems** | [`06_security_fraud_detection.mmd`](diagrams/06_security_fraud_detection.mmd) | SSL/TLS processing and fraud detection |
| **Protocol Stack** | [`07_protocol_stack_overview.mmd`](diagrams/07_protocol_stack_overview.mmd) | Network protocol layers and handlers |
| **Audio Pipeline** | [`08_audio_processing_pipeline.mmd`](diagrams/08_audio_processing_pipeline.mmd) | Codec processing and quality analysis |

## 🏗️ System Architecture Summary

VoIPmonitor is a **high-performance, enterprise-grade VoIP monitoring solution** with the following key characteristics:

### **Core Capabilities**
- **Multi-Protocol Support**: SIP, RTP, RTCP, MGCP, SKINNY/SCCP, SS7, WebRTC, Diameter
- **Real-time Processing**: Millions of packets per second with sub-millisecond latency
- **Quality Analysis**: ITU-T G.107 E-model MOS calculation, jitter analysis, packet loss detection
- **Security Features**: SSL/TLS decryption, SRTP processing, fraud detection
- **Scalable Architecture**: Multi-threaded design with lock-free data structures

### **Technical Specifications**
- **Language**: C++11/14 with performance-critical C components
- **Codebase Size**: ~150,000 lines of code across 150+ files
- **Threading Model**: 10+ specialized thread types with lock-free queues
- **Database Support**: MySQL/MariaDB with connection pooling and partitioning
- **Memory Management**: Advanced heap safety and memory pool optimization

### **Enterprise Features**
- **Management Interface**: TCP-based remote monitoring and control
- **Hot Configuration**: Runtime parameter changes without restart
- **Fraud Detection**: AI-based pattern recognition and anomaly detection
- **Audio Processing**: Multi-codec support with transcription capabilities
- **Geographic Analysis**: IP-to-country mapping and location-based fraud detection

## 🚀 Quick Start Guide

### Understanding the Architecture
1. **Start with**: [CODEBASE_ANALYSIS.md](CODEBASE_ANALYSIS.md) for overall system understanding
2. **Navigate using**: [COMPONENT_INDEX.md](COMPONENT_INDEX.md) to find specific components
3. **Visualize with**: Flow diagrams in the `diagrams/` directory

### Key Entry Points
- **Main Application**: `voipmonitor.cpp` (10,342 lines) - System initialization and control
- **Packet Processing**: `sniff.cpp` (14,000+ lines) - Core packet analysis engine
- **Call Management**: `calltable.cpp` (15,000+ lines) - Call lifecycle and state management
- **Database Layer**: `sql_db.cpp` (5,000+ lines) - Data persistence and storage

### Critical Data Flows
1. **Packet Capture**: Network → libpcap → Queue → Protocol Detection
2. **Call Processing**: SIP INVITE → Call Object → RTP Setup → Quality Analysis
3. **Database Storage**: Processing Threads → SQL Queue → Connection Pool → MySQL
4. **Security Processing**: Traffic → SSL Detection → Decryption → Analysis

## 📊 Performance Characteristics

### **High-Performance Design**
- **Zero-copy Techniques**: Minimize memory allocations in hot paths
- **Lock-free Queues**: Inter-thread communication without blocking
- **Memory Pools**: Pre-allocated memory for frequent operations
- **SIMD Optimization**: Vectorized operations for bulk processing
- **CPU Affinity**: Thread-to-core binding for optimal performance

### **Scalability Features**
- **Horizontal Scaling**: Multi-threaded processing pipeline
- **Database Partitioning**: Time-based data organization
- **Connection Pooling**: Efficient database resource utilization
- **Load Balancing**: Dynamic work distribution across threads

## 🔧 Development Guidelines

### **Code Standards**
- Follow the [Cursor Rules](.cursorrules) for consistent development
- Use RAII principles for resource management
- Implement proper error handling and logging
- Maintain thread safety in multi-threaded contexts

### **Architecture Patterns**
- **Packet Processing Pipeline**: Modular, extensible protocol handlers
- **Call State Management**: Finite state machine for SIP calls
- **Database Abstraction**: Clean separation between logic and storage
- **Thread Pool Design**: Efficient parallel processing architecture

## 🔍 Key Components Deep Dive

### **Packet Processing Engine** (`sniff.cpp/h`)
- Protocol detection and classification
- SIP message parsing and call state management
- RTP stream analysis and quality metrics
- Multi-threaded processing with load balancing

### **Call Management System** (`calltable.cpp/h`)
- Complete call lifecycle management
- Multi-party call support with transfers
- Real-time quality analysis and MOS calculation
- CDR generation with comprehensive metrics

### **Database Layer** (`sql_db.cpp/h`)
- MySQL/MariaDB integration with connection pooling
- Prepared statements for security and performance
- Time-based partitioning for large-scale data management
- Asynchronous storage with thread-safe queues

### **Security Framework** (`ssl.cpp/h`, `fraud.cpp/h`)
- Real-time SSL/TLS traffic decryption
- SRTP secure media stream processing
- Advanced fraud detection with machine learning
- Geographic anomaly detection and pattern analysis

## 📈 Use Cases & Applications

### **Telecommunications Operators**
- Network quality monitoring and troubleshooting
- Regulatory compliance and call recording
- Fraud detection and prevention
- Performance optimization and capacity planning

### **Service Providers**
- SIP trunk monitoring and quality assurance
- Customer experience management
- Security monitoring and threat detection
- Business intelligence and analytics

### **Enterprise Deployments**
- Internal VoIP system monitoring
- Quality of service analysis
- Security audit and compliance
- Cost optimization and usage analysis

## 🛠️ Integration & Deployment

### **System Requirements**
- Linux/FreeBSD operating system
- MySQL/MariaDB database server
- libpcap for packet capture
- SSL libraries for encryption support
- Sufficient memory for high-volume processing

### **Configuration Management**
- Comprehensive configuration file system
- Runtime parameter adjustment
- Hot reload capabilities
- Database-driven configuration options

## 📚 Additional Resources

### **Technical Documentation**
- Protocol specifications (SIP, RTP, RTCP)
- Database schema documentation
- API reference for management interface
- Performance tuning guidelines

### **Development Tools**
- Build system based on GNU Autotools
- Debugging and profiling utilities
- Memory analysis and leak detection
- Performance monitoring and statistics

## 🤝 Contributing

When working with this codebase:
1. Review the architecture documentation thoroughly
2. Follow the established coding patterns and standards
3. Understand the threading model and synchronization requirements
4. Test changes under high-load conditions
5. Update documentation for architectural changes

---

**VoIPmonitor** represents a sophisticated, enterprise-grade solution for VoIP network monitoring and analysis. This documentation provides the foundation for understanding, maintaining, and extending this complex telecommunications monitoring system.

For specific technical questions or implementation details, refer to the individual documentation files and flow diagrams provided in this analysis package.