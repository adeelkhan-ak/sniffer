# VoipMonitor System - State Diagrams for All Modules

## Overview
VoipMonitor is a comprehensive VoIP monitoring system that captures, processes, and analyzes VoIP traffic. This document presents state diagrams for all major modules in the system.

## 1. Main System Module (voipmonitor.cpp)

```mermaid
stateDiagram-v2
    [*] --> Initializing
    Initializing --> ConfigLoading : Load configuration
    ConfigLoading --> DatabaseInit : Config loaded
    DatabaseInit --> InterfaceSetup : DB connected
    InterfaceSetup --> ThreadCreation : Interfaces ready
    ThreadCreation --> Running : All threads started
    Running --> Running : Process packets
    Running --> Reloading : Reload config signal
    Reloading --> Running : Config reloaded
    Running --> Terminating : Terminate signal
    Terminating --> Cleanup : Stop threads
    Cleanup --> [*] : System shutdown
    
    state Running {
        [*] --> PacketCapture
        PacketCapture --> PacketProcessing
        PacketProcessing --> CallAnalysis
        CallAnalysis --> DataStorage
        DataStorage --> PacketCapture
    }
```

## 2. Call Table Module (calltable.h/cpp)

```mermaid
stateDiagram-v2
    [*] --> CallCreated
    CallCreated --> InProgress : SIP INVITE received
    InProgress --> Ringing : 18x response
    InProgress --> Connected : 2xx response
    Ringing --> Connected : 2xx response
    Ringing --> Failed : 4xx/5xx response
    Connected --> OnHold : Re-INVITE with hold
    OnHold --> Connected : Re-INVITE resume
    Connected --> Disconnecting : BYE received
    InProgress --> Cancelled : CANCEL received
    InProgress --> Failed : Timeout/Error
    Disconnecting --> CallEnded : BYE processed
    Failed --> CallEnded : Error handled
    Cancelled --> CallEnded : Cancel processed
    CallEnded --> Archived : Data saved
    Archived --> [*] : Call destroyed
    
    state InProgress {
        [*] --> WaitingResponse
        WaitingResponse --> ProcessingRTP : RTP detected
        ProcessingRTP --> QualityAnalysis
        QualityAnalysis --> ProcessingRTP
    }
```

## 3. RTP Processing Module (rtp.h/cpp)

```mermaid
stateDiagram-v2
    [*] --> Idle
    Idle --> StreamDetected : RTP packet received
    StreamDetected --> Analyzing : Stream validated
    Analyzing --> Active : Stream established
    Active --> Active : Process RTP packets
    Active --> QualityCheck : Periodic analysis
    QualityCheck --> Active : Quality OK
    QualityCheck --> Degraded : Quality issues
    Degraded --> Active : Quality improved
    Degraded --> StreamLost : No packets
    Active --> StreamLost : Timeout
    StreamLost --> Idle : Stream cleanup
    Active --> Terminated : Call ended
    Degraded --> Terminated : Call ended
    Terminated --> [*] : RTP stream closed
    
    state Active {
        [*] --> PacketReceive
        PacketReceive --> JitterBuffer
        JitterBuffer --> CodecDecode
        CodecDecode --> QualityMeasure
        QualityMeasure --> PacketReceive
    }
```

## 4. TCP Reassembly Module (tcpreassembly.h/cpp)

```mermaid
stateDiagram-v2
    [*] --> Waiting
    Waiting --> SynReceived : TCP SYN packet
    SynReceived --> Established : Connection established
    Established --> DataReceiving : Data packets
    DataReceiving --> Reassembling : Fragmented data
    Reassembling --> DataComplete : All fragments received
    DataComplete --> DataReceiving : More data
    DataReceiving --> FinReceived : FIN packet
    Reassembling --> FinReceived : FIN packet
    FinReceived --> Closing : Process remaining data
    Closing --> Closed : Connection closed
    Closed --> [*] : Resources freed
    
    state DataReceiving {
        [*] --> BufferData
        BufferData --> CheckSequence
        CheckSequence --> OrderData : Sequence OK
        CheckSequence --> BufferData : Out of order
        OrderData --> ProcessData
        ProcessData --> BufferData
    }
```

## 5. SIP Registration Module (register.h/cpp)

```mermaid
stateDiagram-v2
    [*] --> Idle
    Idle --> RegisterReceived : REGISTER request
    RegisterReceived --> Authenticating : Process credentials
    Authenticating --> Authenticated : Auth success
    Authenticating --> AuthFailed : Auth failure
    Authenticated --> Registered : Registration success
    AuthFailed --> Idle : Send 401/403
    Registered --> Active : Registration active
    Active --> Refreshing : Re-REGISTER
    Refreshing --> Active : Refresh success
    Refreshing --> Expired : Refresh failed
    Active --> Unregistering : Expires=0
    Unregistering --> Idle : Unregister complete
    Expired --> Idle : Registration expired
    
    state Active {
        [*] --> Monitoring
        Monitoring --> CheckExpiry : Timer check
        CheckExpiry --> Monitoring : Not expired
        CheckExpiry --> Expired : Registration expired
    }
```

## 6. SSL/TLS Processing Module (ssl.h/cpp)

```mermaid
stateDiagram-v2
    [*] --> Idle
    Idle --> HandshakeStart : ClientHello received
    HandshakeStart --> ServerHello : ServerHello sent
    ServerHello --> CertificateExchange : Certificate phase
    CertificateExchange --> KeyExchange : Key exchange
    KeyExchange --> HandshakeComplete : Handshake finished
    HandshakeComplete --> SecureSession : SSL established
    SecureSession --> DataTransfer : Encrypted data
    DataTransfer --> DataTransfer : Process SSL data
    DataTransfer --> SessionClose : Close notify
    SecureSession --> SessionClose : Connection close
    SessionClose --> [*] : SSL session ended
    
    state DataTransfer {
        [*] --> Decrypt
        Decrypt --> ProcessPlaintext
        ProcessPlaintext --> Encrypt
        Encrypt --> Decrypt
    }
```

## 7. Database Module (sql_db.h/cpp)

```mermaid
stateDiagram-v2
    [*] --> Disconnected
    Disconnected --> Connecting : Connect request
    Connecting --> Connected : Connection success
    Connecting --> ConnectionFailed : Connection error
    ConnectionFailed --> Disconnected : Retry later
    Connected --> Idle : Ready for queries
    Idle --> Executing : Query submitted
    Executing --> ResultReady : Query complete
    Executing --> QueryError : Query failed
    ResultReady --> Idle : Result processed
    QueryError --> Idle : Error handled
    Idle --> Disconnected : Connection lost
    Connected --> Disconnected : Explicit disconnect
    
    state Executing {
        [*] --> Prepare
        Prepare --> Execute
        Execute --> Fetch
        Fetch --> Prepare : More queries
    }
```

## 8. Packet Capture Module (pcap_queue.h/cpp)

```mermaid
stateDiagram-v2
    [*] --> Initializing
    Initializing --> InterfaceOpen : Open capture interface
    InterfaceOpen --> Capturing : Interface ready
    Capturing --> PacketReceived : Packet captured
    PacketReceived --> Filtering : Apply filters
    Filtering --> Queued : Packet accepted
    Filtering --> Dropped : Packet filtered
    Queued --> Processing : Send to processor
    Processing --> Capturing : Continue capture
    Dropped --> Capturing : Continue capture
    Capturing --> Stopping : Stop signal
    Stopping --> InterfaceClosed : Close interface
    InterfaceClosed --> [*] : Capture ended
    
    state Processing {
        [*] --> Decode
        Decode --> Classify
        Classify --> Route
        Route --> [*]
    }
```

## 9. WebRTC Processing Module (webrtc.h/cpp)

```mermaid
stateDiagram-v2
    [*] --> Idle
    Idle --> HandshakeDetected : WebRTC handshake
    HandshakeDetected --> Negotiating : Process offer/answer
    Negotiating --> ICEGathering : Gather ICE candidates
    ICEGathering --> ICEConnecting : ICE connectivity
    ICEConnecting --> MediaFlowing : Media established
    MediaFlowing --> MediaFlowing : Process media
    MediaFlowing --> Reconnecting : Connection issue
    Reconnecting --> MediaFlowing : Reconnected
    Reconnecting --> Failed : Reconnect failed
    MediaFlowing --> Closing : Close request
    Failed --> Closing : Handle failure
    Closing --> [*] : WebRTC session ended
    
    state MediaFlowing {
        [*] --> ProcessVideo
        ProcessVideo --> ProcessAudio
        ProcessAudio --> QualityCheck
        QualityCheck --> ProcessVideo
    }
```

## 10. Billing Module (billing.h/cpp)

```mermaid
stateDiagram-v2
    [*] --> Idle
    Idle --> CallStart : Call initiated
    CallStart --> RatingLookup : Find billing rules
    RatingLookup --> RatingFound : Rules found
    RatingLookup --> DefaultRating : Use default
    RatingFound --> Calculating : Calculate charges
    DefaultRating --> Calculating : Calculate charges
    Calculating --> CallActive : Billing active
    CallActive --> CallActive : Update charges
    CallActive --> CallEnd : Call terminated
    CallEnd --> FinalCalculation : Final billing
    FinalCalculation --> BillGenerated : Bill ready
    BillGenerated --> Idle : Bill processed
    
    state CallActive {
        [*] --> TimerUpdate
        TimerUpdate --> ChargeCalculation
        ChargeCalculation --> TimerUpdate
    }
```

## 11. Configuration Module (config_param.h/cpp)

```mermaid
stateDiagram-v2
    [*] --> Uninitialized
    Uninitialized --> Loading : Load config file
    Loading --> Parsing : Parse parameters
    Parsing --> Validating : Validate values
    Validating --> Applied : Config applied
    Validating --> Error : Validation failed
    Error --> Loading : Retry with defaults
    Applied --> Active : System running
    Active --> Reloading : Reload signal
    Reloading --> Parsing : Re-parse config
    Active --> Saving : Save config
    Saving --> Active : Config saved
    
    state Active {
        [*] --> Monitoring
        Monitoring --> ParameterChange : Parameter modified
        ParameterChange --> Monitoring : Change applied
    }
```

## 12. Audio Processing/DSP Module (dsp.h/cpp)

```mermaid
stateDiagram-v2
    [*] --> Idle
    Idle --> AudioReceived : Audio data input
    AudioReceived --> Buffering : Buffer audio samples
    Buffering --> Processing : Process audio
    Processing --> ToneDetection : Detect tones/DTMF
    ToneDetection --> QualityAnalysis : Analyze quality
    QualityAnalysis --> AudioReady : Processing complete
    AudioReady --> Idle : Output audio
    
    state Processing {
        [*] --> Decode
        Decode --> Filter
        Filter --> Enhance
        Enhance --> Encode
        Encode --> [*]
    }
    
    state QualityAnalysis {
        [*] --> MOSCalculation
        MOSCalculation --> JitterAnalysis
        JitterAnalysis --> PacketLossCheck
        PacketLossCheck --> [*]
    }
```

## 13. Transcription Module (transcribe.h/cpp)

```mermaid
stateDiagram-v2
    [*] --> Idle
    Idle --> AudioReceived : Audio for transcription
    AudioReceived --> Preprocessing : Prepare audio
    Preprocessing --> LanguageDetection : Detect language
    LanguageDetection --> Transcribing : Start transcription
    Transcribing --> Processing : Process speech
    Processing --> TextGeneration : Generate text
    TextGeneration --> PostProcessing : Clean up text
    PostProcessing --> Complete : Transcription ready
    Complete --> Idle : Return results
    
    state Processing {
        [*] --> SpeechDetection
        SpeechDetection --> FeatureExtraction
        FeatureExtraction --> ModelInference
        ModelInference --> [*]
    }
```

## 14. Charts/Statistics Module (charts.h/cpp)

```mermaid
stateDiagram-v2
    [*] --> Idle
    Idle --> DataCollection : Collect metrics
    DataCollection --> Aggregating : Aggregate data
    Aggregating --> Calculating : Calculate statistics
    Calculating --> ChartGeneration : Generate charts
    ChartGeneration --> Ready : Charts ready
    Ready --> Idle : Serve charts
    Ready --> Updating : Update request
    Updating --> DataCollection : Refresh data
    
    state Calculating {
        [*] --> MOSCalculation
        MOSCalculation --> CallVolumeStats
        CallVolumeStats --> QualityMetrics
        QualityMetrics --> [*]
    }
```

## 15. Country Detection Module (country_detect.h/cpp)

```mermaid
stateDiagram-v2
    [*] --> Idle
    Idle --> NumberReceived : Phone number input
    NumberReceived --> Parsing : Parse number format
    Parsing --> PrefixLookup : Lookup country prefix
    PrefixLookup --> CountryFound : Match found
    PrefixLookup --> DefaultCountry : No match
    CountryFound --> ValidationCheck : Validate result
    DefaultCountry --> ValidationCheck : Use default
    ValidationCheck --> ResultReady : Country determined
    ResultReady --> Idle : Return country info
```

## Module Interaction Overview

```mermaid
graph TD
    A[Main System] --> B[Packet Capture]
    B --> C[TCP Reassembly]
    C --> D[SIP Processing]
    D --> E[Call Table]
    E --> F[RTP Processing]
    F --> G[Audio/DSP]
    G --> H[Transcription]
    D --> I[Registration]
    C --> J[SSL/TLS]
    E --> K[Billing]
    E --> L[Database]
    A --> M[Configuration]
    E --> N[Charts/Stats]
    D --> O[Country Detection]
    F --> P[WebRTC]
    
    style A fill:#f9f,stroke:#333,stroke-width:4px
    style E fill:#bbf,stroke:#333,stroke-width:2px
    style F fill:#bfb,stroke:#333,stroke-width:2px
```

## State Transition Summary

Each module follows a typical lifecycle:
1. **Initialization** - Module setup and resource allocation
2. **Active Processing** - Main operational state with data processing
3. **Error Handling** - Recovery from failures and exceptions  
4. **Cleanup/Termination** - Graceful shutdown and resource deallocation

The system maintains state consistency through:
- Synchronized access to shared resources
- Event-driven state transitions
- Proper error handling and recovery mechanisms
- Resource cleanup on state transitions