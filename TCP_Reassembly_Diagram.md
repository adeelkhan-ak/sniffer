# TCP Reassembly - State and Function Diagram

## Overview
This diagram shows the TCP stream reassembly process in the VoIP monitoring system, primarily implemented in `tcpreassembly.h` and `tcpreassembly.cpp`. The system handles multiple protocol types: HTTP, WebRTC, SSL, SIP, and Diameter.

## TCP Reassembly Architecture Overview

```mermaid
flowchart TD
    A[Packet Capture] --> B[TcpReassembly::push_tcp]
    B --> C{Packet Thread<br/>Enabled?}
    C -->|Yes| D[Queue Packet]
    C -->|No| E[TcpReassembly::_push]
    D --> F[Packet Thread] --> E
    
    E --> G[Find/Create TcpReassemblyLink]
    G --> H[TcpReassemblyLink::push]
    H --> I[Create/Find TcpReassemblyStream]
    I --> J[TcpReassemblyStream::push]
    J --> K[Store TcpReassemblyStream_packet]
    
    K --> L[TcpReassemblyLink::okQueue]
    L --> M[TcpReassemblyStream::ok]
    M --> N{Stream Complete?}
    N -->|Yes| O[TcpReassemblyLink::complete]
    N -->|No| P[Wait for more packets]
    
    O --> Q[Process Complete Data]
    Q --> R[Call Data Callback]
    R --> S[Protocol-specific Processing]
```

## TCP Link State Machine

```mermaid
stateDiagram-v2
    [*] --> STATE_NA : New connection
    
    STATE_NA --> STATE_SYN_SENT : SYN packet received
    STATE_SYN_SENT --> STATE_SYN_RECV : SYN-ACK received
    STATE_SYN_RECV --> STATE_SYN_OK : ACK received (3-way handshake complete)
    
    STATE_NA --> STATE_SYN_FORCE_OK : Force OK without handshake
    STATE_SYN_SENT --> STATE_SYN_FORCE_OK : Skip handshake validation
    
    STATE_SYN_OK --> STATE_RESET : RST packet received
    STATE_SYN_OK --> STATE_CLOSE : FIN packet received
    STATE_CLOSE --> STATE_CLOSED : Connection fully closed
    
    STATE_SYN_OK --> STATE_CRAZY : Crazy sequence detected
    STATE_CRAZY --> STATE_RESET : Reset from crazy state
    STATE_CRAZY --> STATE_CLOSE : Close from crazy state
    
    STATE_RESET --> [*] : Connection terminated
    STATE_CLOSED --> [*] : Connection terminated
    
    note right of STATE_CRAZY
        Handles out-of-order packets
        and sequence number issues
    end note
```

## TCP Stream Processing State Machine

```mermaid
stateDiagram-v2
    [*] --> StreamCreated : New stream for ACK
    
    StreamCreated --> PacketReceived : TCP packet arrives
    
    PacketReceived --> CheckSequence : Validate sequence number
    
    CheckSequence --> StorePacket : Sequence OK
    CheckSequence --> HandleOutOfOrder : Out of sequence
    
    HandleOutOfOrder --> StorePacket : Queue for later
    HandleOutOfOrder --> DiscardPacket : Invalid/duplicate
    
    StorePacket --> CheckCompleteness : Add to packet queue
    
    CheckCompleteness --> StreamIncomplete : Missing packets
    CheckCompleteness --> StreamComplete : All packets present
    
    StreamIncomplete --> PacketReceived : Wait for more packets
    
    StreamComplete --> ValidateData : Check data integrity
    
    ValidateData --> DataValid : Validation passed
    ValidateData --> DataInvalid : Validation failed
    
    DataValid --> ReassembleStream : Merge packet data
    DataInvalid --> StreamIncomplete : Wait for missing data
    
    ReassembleStream --> ProcessProtocol : Identify protocol content
    
    ProcessProtocol --> HTTPProcessing : HTTP data detected
    ProcessProtocol --> SIPProcessing : SIP data detected  
    ProcessProtocol --> SSLProcessing : SSL/TLS data detected
    ProcessProtocol --> WebRTCProcessing : WebRTC data detected
    ProcessProtocol --> DiameterProcessing : Diameter data detected
    
    HTTPProcessing --> StreamProcessed : HTTP callback executed
    SIPProcessing --> StreamProcessed : SIP callback executed
    SSLProcessing --> StreamProcessed : SSL callback executed
    WebRTCProcessing --> StreamProcessed : WebRTC callback executed
    DiameterProcessing --> StreamProcessed : Diameter callback executed
    
    StreamProcessed --> [*] : Stream completed
    DiscardPacket --> [*] : Packet discarded
```

## Key Data Structures

### 1. TcpReassemblyLink (Connection)
```cpp
class TcpReassemblyLink {
    vmIP ip_src, ip_dst;                    // Connection endpoints
    vmPort port_src, port_dst;              // Port numbers
    eState state;                           // Connection state
    map<uint32_t, TcpReassemblyStream*> queue_by_ack;  // Streams by ACK
    deque<TcpReassemblyStream*> queueStreams;          // All streams
    bool exists_data;                       // Has actual data
    u_int64_t last_packet_at_from_header;  // Last activity timestamp
};
```

### 2. TcpReassemblyStream (Data Stream)
```cpp
class TcpReassemblyStream {
    eDirection direction;                   // TO_DEST or TO_SOURCE
    u_int32_t ack, first_seq, last_seq;   // Sequence tracking
    map<uint32_t, TcpReassemblyStream_packet_var> queuePacketVars;  // Packets
    bool is_ok;                            // Stream validation status
    TcpReassemblyDataItem complete_data;   // Reassembled data
    eHttpType http_type;                   // HTTP request type
    bool http_ok;                          // HTTP validation
};
```

### 3. TcpReassemblyStream_packet (Individual Packet)
```cpp
class TcpReassemblyStream_packet {
    timeval time;                          // Packet timestamp
    tcphdr2 header_tcp;                   // TCP header
    u_char *data;                         // Packet data
    u_int32_t datalen, datacaplen;        // Data lengths
    eState state;                         // Packet state (NA/CHECK/FAIL)
};
```

## TCP Reassembly Function Flow

### Main Processing Pipeline
```mermaid
flowchart TD
    A[TcpReassembly::push_tcp] --> B[Extract TCP header and data]
    B --> C[Validate packet bounds]
    C --> D[Calculate sequence numbers]
    D --> E[Find or create TcpReassemblyLink]
    
    E --> F[Determine packet direction]
    F --> G[TcpReassemblyLink::push]
    
    G --> H{Connection State?}
    H -->|Normal| I[push_normal]
    H -->|Crazy| J[push_crazy]
    
    I --> K[Find/create stream by ACK]
    J --> K
    
    K --> L[TcpReassemblyStream::push]
    L --> M[Store packet in sequence map]
    
    M --> N[TcpReassemblyLink::okQueue]
    N --> O[Validate stream completeness]
    
    O --> P{Stream OK?}
    P -->|Yes| Q[TcpReassemblyStream::complete]
    P -->|No| R[Wait for more packets]
    
    Q --> S[Merge packets into data stream]
    S --> T[Protocol-specific validation]
    T --> U[Call data callback]
    
    R --> V[Cleanup expired streams]
    U --> V
    V --> W[End]
```

### Stream Validation Algorithm
```mermaid
flowchart TD
    A[TcpReassemblyStream::ok] --> B[Check reassembly attempts limit]
    B --> C{Limit exceeded?}
    C -->|Yes| D[Return failed]
    C -->|No| E[Clean packet states]
    
    E --> F[Find first sequence number]
    F --> G[Iterate through expected sequences]
    
    G --> H{Packet exists for sequence?}
    H -->|No| I[Check for gaps]
    H -->|Yes| J[Validate packet data]
    
    I --> K{Gap acceptable?}
    K -->|No| L[Return incomplete]
    K -->|Yes| M[Continue with next sequence]
    
    J --> N{Data valid?}
    N -->|No| O[Mark packet as FAIL]
    N -->|Yes| P[Add to OK packets list]
    
    O --> Q{Remove failed packet?}
    Q -->|Yes| R[Remove from OK list]
    Q -->|No| S[Continue validation]
    
    P --> T[Update sequence counters]
    T --> U{More packets to check?}
    U -->|Yes| G
    U -->|No| V[Check stream completeness]
    
    V --> W{All required packets OK?}
    W -->|Yes| X[Return stream complete]
    W -->|No| L
    
    L --> Y[End: Incomplete]
    D --> Y
    X --> Z[End: Complete]
    M --> G
    R --> G
    S --> G
```

### Data Completion and Merging
```mermaid
flowchart TD
    A[TcpReassemblyStream::complete] --> B[Validate OK packets list]
    B --> C[Calculate total data length]
    C --> D[Allocate complete data buffer]
    
    D --> E[Iterate through OK packets]
    E --> F[Copy packet data to buffer]
    F --> G{More packets?}
    G -->|Yes| E
    G -->|No| H[Update complete_data]
    
    H --> I[Set completion timestamp]
    I --> J[Mark stream as completed]
    J --> K[Return complete data]
```

## Protocol-Specific Processing

### HTTP Processing
```mermaid
flowchart TD
    A[HTTP Data Detected] --> B[Parse HTTP headers]
    B --> C{Request or Response?}
    C -->|Request| D[Extract method, URI, headers]
    C -->|Response| E[Extract status, headers, body]
    
    D --> F[Check Content-Length]
    E --> F
    F --> G{Expect: 100-continue?}
    G -->|Yes| H[Handle expect-continue flow]
    G -->|No| I[Standard HTTP processing]
    
    H --> J[Wait for 100 Continue response]
    J --> K[Process request body]
    K --> I
    
    I --> L[Store HTTP transaction]
    L --> M[Call HTTP callback]
```

### SIP Processing
```mermaid
flowchart TD
    A[SIP Data Detected] --> B[Check for WebSocket framing]
    B --> C{WebSocket?}
    C -->|Yes| D[Unwrap WebSocket frame]
    C -->|No| E[Direct SIP parsing]
    
    D --> E
    E --> F[Parse SIP message]
    F --> G{Valid SIP?}
    G -->|Yes| H[Extract SIP headers]
    G -->|No| I[Discard data]
    
    H --> J[Identify message type]
    J --> K[Store SIP transaction]
    K --> L[Call SIP callback]
```

## Threading and Concurrency

### Thread Safety Mechanisms
```mermaid
flowchart TD
    A[Multiple Threads] --> B[TcpReassembly Lock Management]
    
    B --> C[Push Lock: _sync_push]
    B --> D[Links Lock: _sync_links]
    B --> E[Cleanup Lock: _sync_cleanup]
    
    C --> F[Protects packet insertion]
    D --> G[Protects link map access]
    E --> H[Protects cleanup operations]
    
    F --> I[TcpReassemblyLink Queue Lock]
    G --> I
    H --> I
    
    I --> J[Protects stream operations]
    J --> K[Safe concurrent access]
```

### Cleanup and Memory Management
```mermaid
flowchart TD
    A[Cleanup Thread] --> B{Cleanup Type?}
    B -->|Simple| C[cleanup_simple]
    B -->|Full| D[cleanup]
    
    C --> E[Check stream timeouts]
    D --> F[Full validation and cleanup]
    
    E --> G[Remove expired streams]
    F --> G
    
    G --> H[Free packet memory]
    H --> I[Remove empty links]
    I --> J[Update statistics]
    
    J --> K{Cleanup complete?}
    K -->|No| L[Continue cleanup]
    K -->|Yes| M[Sleep until next cycle]
    
    L --> E
    M --> A
```

## Configuration Parameters

### Key Settings
- **linkTimeout**: Connection timeout (default: 2 minutes)
- **maxReassemblyAttempts**: Maximum validation attempts (default: 50)
- **maxStreamLength**: Maximum packets per stream (default: unlimited)
- **cleanupPeriod**: Cleanup interval (default: 20 seconds)
- **enableCrazySequence**: Handle out-of-order packets
- **simpleByAck**: Use simplified ACK-based processing

### Performance Optimizations
- **enablePacketThread**: Separate thread for packet processing
- **enableCleanupThread**: Dedicated cleanup thread
- **enableSmartCompleteData**: Intelligent data completion
- **enableValidateDataViaCheckData**: Protocol-specific validation

## Error Handling and Recovery

### Common Error Scenarios
1. **Sequence gaps**: Handle missing TCP segments
2. **Out-of-order packets**: Reorder using sequence numbers
3. **Duplicate packets**: Detect and discard duplicates
4. **Memory exhaustion**: Cleanup expired connections
5. **Protocol violations**: Validate and recover from malformed data

### Debug and Monitoring
- **Verbose logging**: Detailed packet and stream information
- **Performance statistics**: CPU usage and memory consumption
- **Protocol-specific logs**: HTTP, SIP, SSL transaction logs
- **Packet dumping**: Save problematic packets for analysis