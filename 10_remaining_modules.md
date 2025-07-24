# Remaining Modules State Diagrams

## 10. Billing Module State Diagram (billing.h/cpp)

### Overview
The Billing Module handles call billing and charging calculations, managing billing rules, rate tables, and generating billing records for VoIP calls.

### State Diagram

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

---

## 11. Configuration Module State Diagram (config_param.h/cpp)

### Overview
The Configuration Module manages system configuration parameters, handles configuration file parsing, and provides dynamic configuration updates.

### State Diagram

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

---

## 12. Audio Processing/DSP Module State Diagram (dsp.h/cpp)

### Overview
The Audio Processing/DSP Module handles digital signal processing for audio streams, including tone detection, quality analysis, and audio enhancement.

### State Diagram

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

---

## 13. Transcription Module State Diagram (transcribe.h/cpp)

### Overview
The Transcription Module handles speech-to-text conversion for recorded calls, using machine learning models to generate text transcripts from audio.

### State Diagram

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

---

## 14. Charts/Statistics Module State Diagram (charts.h/cpp)

### Overview
The Charts/Statistics Module generates performance reports, call quality statistics, and visual charts for system monitoring and analysis.

### State Diagram

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

---

## 15. Country Detection Module State Diagram (country_detect.h/cpp)

### Overview
The Country Detection Module analyzes phone numbers to determine geographic origins, supporting international call analysis and routing decisions.

### State Diagram

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

---

## Module Integration Overview

### Data Flow Between Modules

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

### State Synchronization

All modules maintain their states independently but coordinate through:

1. **Event-driven Communication**: Modules communicate through events and callbacks
2. **Shared Data Structures**: Common data structures for call and session information
3. **Configuration Synchronization**: Centralized configuration management
4. **Database Coordination**: Shared database for persistent state
5. **Error Propagation**: Error conditions propagated across related modules

### Common State Patterns

Each module follows similar state transition patterns:

1. **Initialization Phase**: Setup and resource allocation
2. **Active Processing**: Main operational states with data processing
3. **Error Handling**: Recovery mechanisms and error states
4. **Cleanup Phase**: Resource deallocation and state cleanup

### Performance Considerations

- **Concurrent Processing**: Multiple instances of each module can run concurrently
- **State Isolation**: Each instance maintains independent state
- **Resource Management**: Careful resource allocation and cleanup
- **Scalability**: Modules designed to scale with system load