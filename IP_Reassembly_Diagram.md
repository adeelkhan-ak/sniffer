# IP Fragmentation and Reassembly - State and Function Diagram

## Overview
This diagram shows the IP fragmentation handling and reassembly process in the VoIP monitoring system, primarily implemented in `ip_frag.h` and `ip_frag.cpp`.

## IP Reassembly State Machine

```mermaid
stateDiagram-v2
    [*] --> PacketReceived : IP packet arrives
    
    PacketReceived --> CheckFragmentation : Extract IP header
    
    CheckFragmentation --> NotFragmented : No fragmentation flags
    CheckFragmentation --> FragmentedPacket : Has MF flag or fragment offset > 0
    
    NotFragmented --> [*] : Pass through to next layer
    
    FragmentedPacket --> GetFragmentKey : Extract (src_ip, frag_id)
    
    GetFragmentKey --> FindFragmentQueue : Look up existing fragments
    
    FindFragmentQueue --> CreateNewQueue : No existing queue found
    FindFragmentQueue --> AddToExistingQueue : Queue exists
    
    CreateNewQueue --> StoreFragment : Create sFrags structure
    AddToExistingQueue --> CheckDuplicate : Verify fragment offset
    
    CheckDuplicate --> DiscardDuplicate : Same offset exists
    CheckDuplicate --> StoreFragment : New fragment offset
    
    StoreFragment --> CheckComplete : Add to ordered map
    
    CheckComplete --> WaitMoreFragments : Missing fragments
    CheckComplete --> ReassemblePacket : All fragments present
    
    WaitMoreFragments --> [*] : Wait for more fragments
    
    ReassemblePacket --> ValidateFragments : Check sequence integrity
    ValidateFragments --> CreateReassembledPacket : All fragments valid
    ValidateFragments --> DiscardFragments : Invalid sequence
    
    CreateReassembledPacket --> CleanupFragments : Merge fragment data
    CleanupFragments --> [*] : Return complete packet
    
    DiscardDuplicate --> [*] : Fragment discarded
    DiscardFragments --> [*] : Invalid fragments discarded
    
    state TimeoutCleanup {
        [*] --> CheckTimeout : Periodic cleanup
        CheckTimeout --> RemoveExpired : Fragments older than 30s
        RemoveExpired --> [*] : Cleanup complete
    }
```

## Key Data Structures

### 1. Fragment Structure (`sFrag`)
```cpp
struct sFrag {
    sHeaderPacket *header_packet;        // Original packet header
    void *header_packet_pqout;           // Packet queue output
    unsigned int header_ip_offset;       // IP header offset in packet
    time_t ts;                          // Timestamp for timeout
    u_int32_t offset;                   // Fragment offset
    u_int32_t len;                      // Fragment length
    u_int16_t iphdr_len;               // IP header length
};
```

### 2. Fragment Collection (`sFrags`)
```cpp
struct sFrags : map<u_int16_t, sFrag*> {
    bool has_last;  // Indicates if last fragment received
};
```

### 3. Defragmentation Map (`sDefrag`)
```cpp
struct sDefrag : map<pair<vmIP, u_int32_t>, sFrags*> {
    // Maps (source_ip, fragment_id) -> fragments
};
```

## Function Call Flow

### Main Defragmentation Function
```mermaid
flowchart TD
    A[cIpFrag::defrag] --> B[Get fragment key: src_ip, frag_id]
    B --> C[Find or create sFrags]
    C --> D[cIpFrag::add]
    
    D --> E[Extract fragment info]
    E --> F[Check if last fragment]
    F --> G[Check for duplicate offset]
    
    G --> H{Duplicate?}
    H -->|Yes| I[Return -1: Discard]
    H -->|No| J[Create sFrag and store]
    
    J --> K[Check completeness]
    K --> L{All fragments present?}
    L -->|No| M[Return 0: Wait for more]
    L -->|Yes| N[cIpFrag::dequeue]
    
    N --> O[Calculate total length]
    O --> P[Allocate reassembled packet]
    P --> Q[Copy fragments in order]
    Q --> R[Update IP header]
    R --> S[Return 1: Complete packet]
    
    I --> T[End]
    M --> T
    S --> T
```

### Fragment Completeness Check Algorithm
```mermaid
flowchart TD
    A[Check Completeness] --> B{Has first fragment?<br/>offset == 0}
    B -->|No| C[Return incomplete]
    B -->|Yes| D{Has last fragment?<br/>has_last == true}
    D -->|No| C
    D -->|Yes| E[Check sequence integrity]
    
    E --> F[lastoffset = 0]
    F --> G[For each fragment in order]
    G --> H{fragment.offset == lastoffset?}
    H -->|No| I[Return incomplete]
    H -->|Yes| J[lastoffset += fragment.len - fragment.iphdr_len]
    J --> K{More fragments?}
    K -->|Yes| G
    K -->|No| L[Return complete]
    
    C --> M[End: Wait for more]
    I --> M
    L --> N[End: Ready to reassemble]
```

## Threading and Memory Management

### Thread Safety
- Uses thread-specific fragment data arrays (`fdata[thread_index]`)
- Thread index calculated from source IP hash: `saddr.getHashNumber() % fdata_threads_split`
- Default split: 16 threads (`DEFRAG_THREADS_SPLIT`)

### Memory Management
```mermaid
flowchart TD
    A[Fragment Creation] --> B[Allocate sFrag]
    B --> C[Copy packet data]
    C --> D[Store in sFrags map]
    
    D --> E{Reassembly complete?}
    E -->|Yes| F[sFrag::destroy for all fragments]
    E -->|No| G[Keep in memory]
    
    F --> H[Delete sFrags]
    H --> I[Remove from defrag map]
    
    G --> J{Timeout reached?}
    J -->|Yes| K[cIpFrag::cleanup]
    J -->|No| L[Continue waiting]
    
    K --> M[Destroy expired fragments]
    M --> N[Free memory]
```

### Cleanup Process
- **Timeout**: 30 seconds default
- **Trigger**: Periodic cleanup or manual cleanup
- **Process**: Remove fragments older than timeout threshold
- **Memory**: Properly destroy fragment objects and free packet data

## Configuration and Optimization

### Key Parameters
- `DEFRAG_THREADS_SPLIT`: Number of thread-specific fragment stores (16)
- `cleanup_limit`: Fragment timeout in seconds (30)
- `DEFRAG_HEADER_IP_COPY`: Whether to copy IP header to avoid corruption

### Performance Features
- **Hash-based threading**: Distributes fragments across threads by source IP
- **Ordered storage**: Uses `std::map` for automatic fragment ordering
- **Memory pooling**: Reuses fragment structures where possible
- **Overflow protection**: Limits total reassembled packet size to 0xFFFF bytes

## Error Handling

### Common Error Scenarios
1. **Duplicate fragments**: Discard duplicate offset fragments
2. **Overflow**: Limit reassembled packet to maximum IP packet size
3. **Timeout**: Clean up incomplete fragment sets after 30 seconds
4. **Memory allocation failure**: Graceful degradation and cleanup
5. **Corrupted fragments**: Validate fragment sequence integrity

### Debug Features
- **Overflow logging**: Log when reassembled packet exceeds size limits
- **Fragment tracking**: Debug output for fragment processing
- **Memory leak detection**: Track allocation and deallocation