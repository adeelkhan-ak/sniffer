# VoIPmonitor API Reference

## Core Classes and Interfaces

### Call Management API

#### `Call` Class
```cpp
class Call : public CallStructs, public Call_abstract {
public:
    // Call lifecycle management
    void processPacket(packet_s_process *packetS);
    void setCallFlags(unsigned long int flags);
    bool read_rtp(CallBranch *c_branch, packet_s_process_0 *packetS, int iscaller);
    
    // Call state management
    void set_first_packet_time_us(u_int64_t time_us);
    void set_last_packet_time_us(u_int64_t time_us);
    CallBranch* branch_main();
    
    // Quality analysis
    void calculate_mos();
    double get_mos_lqo();
    void save_to_db(bool enableBatchIfPossible = true);
};
```

#### `CallBranch` Class
```cpp
class CallBranch : public CallStructs {
public:
    string caller;
    string called;
    string caller_domain;
    string called_domain;
    
    // SIP response handling
    int lastSIPresponseNum;
    string lastSIPresponse;
    
    // Quality metrics
    double mos_lqo;
    double mos_lqo_mult10;
    
    // Call timing
    u_int64_t first_packet_time_us;
    u_int64_t last_packet_time_us;
};
```

### Packet Processing API

#### `packet_s` Structure
```cpp
struct packet_s {
    vmIP _saddr;
    vmIP _daddr;
    u_int32_t _datalen;
    pcap_pkthdr *header_pt;
    const u_char *packet;
    
    // Packet metadata
    u_int16_t _source;
    u_int16_t _dest;
    u_int16_t handle_index;
    u_int16_t dlt;
    
    // Processing flags
    bool need_sip_process : 1;
    bool is_rtp : 1;
    bool skip : 1;
};
```

#### `pcap_block_store` Class
```cpp
class pcap_block_store {
public:
    bool add_packet(pcap_pkthdr *header, u_char *packet);
    bool get_packet(u_int32_t index, pcap_pkthdr **header, u_char **packet);
    u_int32_t count;
    u_int32_t size_compress;
    bool compress();
    bool uncompress();
};
```

### Database API

#### `SqlDb` Class
```cpp
class SqlDb {
public:
    bool connect();
    void disconnect();
    bool query(string query);
    SqlDb_result query_result(string query);
    
    // Transaction management
    void beginTransaction();
    void commitTransaction();
    void rollbackTransaction();
    
    // Connection management
    bool isConnected();
    string getLastError();
};
```

#### `SqlDb_row` Class
```cpp
class SqlDb_row {
public:
    string operator[](const char *fieldName);
    string operator[](int indexField);
    
    // Field management
    void add(const char *content, string fieldName);
    void add(int content, string fieldName);
    void add(double content, string fieldName);
    
    size_t getCountFields();
    bool isEmpty();
};
```

### RTP Processing API

#### `RTP` Class
```cpp
class RTP {
public:
    // Stream initialization
    void init(class Call *call);
    void setSRtpDecrypt(class RTPsecure *srtp_decrypt);
    
    // Packet processing
    bool read(CallBranch *c_branch, packet_s_process_0 *packetS);
    void jt_tail(struct ast_channel *channel);
    
    // Quality metrics
    double calculate_mos();
    u_int32_t getLostPackets();
    double getJitter();
    
    // Stream properties
    u_int32_t ssrc;
    u_int16_t seq;
    u_int32_t timestamp;
    int codec;
};
```

### Configuration API

#### `cConfigItem` Class
```cpp
class cConfigItem {
public:
    cConfigItem(const char *name);
    virtual bool setParamFromValueStr(string value_str) = 0;
    virtual string getValueStr(bool configFile = false) = 0;
    
    // Configuration management
    cConfigItem* addAlias(const char *name_alias);
    cConfigItem* setDefaultValueStr(const char *defaultValueStr);
    cConfigItem* setDescription(const char *description);
    cConfigItem* setHelp(const char *help);
};
```

### Management Interface API

#### `ManagerClientThread` Class
```cpp
class ManagerClientThread {
public:
    void run();
    bool parseCommand(string command);
    void sendResponse(string response);
    
    // Command handlers
    void cmd_list_calls();
    void cmd_get_call_stats(string call_id);
    void cmd_reload_config();
    void cmd_get_system_stats();
};
```

## Function APIs

### Core Processing Functions

```cpp
// Main packet processing
void process_packet(packet_s_process *packetS);
void process_packet__push_batch();

// Protocol detection
bool check_sip20(char *data, unsigned long len);
bool check_websocket(char *data, unsigned long len);

// Call management
Call* calltable_add_call(packet_s_process *packetS, char *call_id);
void calltable_remove_call(Call *call);

// RTP processing
int rtp_read_thread_func(void *arg);
void process_rtp_packets_hash(packet_s_process_0 *packetS);

// Database operations
void store_process_query(string query, int store_id);
void store_process_query_compl(SqlDb_mysql *sqlDb);
```

### Utility Functions

```cpp
// String utilities
string intToString(long long i);
vector<string> split(const char* str, const char* delim);
string trim(string str);

// Network utilities
vmIP str_2_vmIP(const char *str);
string vmIP_2_string(vmIP ip);
bool check_ip_in_net(vmIP ip, vmIP net, int mask);

// Time utilities
u_int64_t getTimeMS_rdtsc();
string sqlDateTimeString(time_t unixTime);
tm time_r(time_t *time, const char *timezone = NULL);

// File utilities
bool file_exists(const char *filename);
long long get_file_size(const char *filename);
bool copy_file(const char *src, const char *dst);
```

## Constants and Enums

### SIP Method Constants
```cpp
#define INVITE 1
#define BYE 2
#define CANCEL 3
#define RES2XX 200
#define RES4XX 400
#define RES5XX 500
#define REGISTER 4
#define MESSAGE 5
#define OPTIONS 8
```

### Call Flags
```cpp
#define FLAG_SAVESIP (1ULL << 0)
#define FLAG_SAVERTP (1ULL << 1)
#define FLAG_SAVEAUDIO (1ULL << 10)
#define FLAG_SAVEGRAPH (1ULL << 15)
#define FLAG_SKIPCDR (1ULL << 17)
```

### Codec Constants
```cpp
#define PAYLOAD_PCMU 0
#define PAYLOAD_PCMA 8
#define PAYLOAD_G729 18
#define PAYLOAD_G722 9
#define PAYLOAD_G723 4
```

## Error Codes

### Database Error Codes
```cpp
enum SqlDbError {
    SQL_DB_OK = 0,
    SQL_DB_ERROR_CONN = 1,
    SQL_DB_ERROR_QUERY = 2,
    SQL_DB_ERROR_TIMEOUT = 3
};
```

### Call Processing Error Codes
```cpp
enum CallError {
    CALL_OK = 0,
    CALL_ERROR_INVALID_PACKET = 1,
    CALL_ERROR_NO_MEMORY = 2,
    CALL_ERROR_DATABASE = 3
};
```

## Usage Examples

### Creating and Managing Calls
```cpp
// Create new call from SIP INVITE
Call* call = new Call(INVITE, call_id, time_us);
call->set_caller(caller_number);
call->set_called(called_number);

// Process RTP packet for call
if (call->read_rtp(c_branch, packetS, iscaller)) {
    call->calculate_mos();
}

// Save call to database
call->save_to_db();
```

### Database Operations
```cpp
// Connect to database
SqlDb* sqlDb = new SqlDb_mysql();
if (sqlDb->connect()) {
    // Execute query
    SqlDb_result result = sqlDb->query_result("SELECT * FROM cdr LIMIT 10");
    while (SqlDb_row row = result.fetchRow()) {
        cout << row["caller"] << " -> " << row["called"] << endl;
    }
}
```

### Configuration Management
```cpp
// Set configuration parameter
cConfigItem_integer* max_calls = new cConfigItem_integer("max_calls");
max_calls->setDefaultValueStr("1000");
max_calls->setParamFromValueStr("2000");

// Get configuration value
int max_calls_value = max_calls->getValueInt();
```

This API reference provides the essential interfaces for working with the VoIPmonitor codebase programmatically.