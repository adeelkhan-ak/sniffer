#ifndef SSL_DSSL_H
#define SSL_DSSL_H


#include "config.h"
#include "sql_db.h"

#include <map>
#include <string>
#include <vector>


#if defined(HAVE_OPENSSL101) and defined(HAVE_LIBGNUTLS)


#include <openssl/ssl.h>
#include <openssl/pem.h>

#include "tools.h"

#include "dssl/dssl_defs.h"
#include "dssl/errors.h"
#include "dssl/packet.h"
#include "dssl/ssl_session.h"
#include "dssl/ssl_decode_hs.h"

extern "C" {
#include "dssl/tls-ext.h"
}


using namespace std;


class cSslDsslSession {
public:
	enum eServerErrors {
		_se_na,
		_se_ok,
		_se_keyfile_not_exists,
		_se_load_key_failed
	};
public:
	cSslDsslSession(vmIP ips, vmPort ports, string keyfile, string password = "");
	~cSslDsslSession();
	void setClientIpPort(vmIP ipc, vmPort portc);
	void init();
	void term();
	bool initServer();
	bool initSession();
	void termServer();
	void termSession();
	void processData(vector<string> *rslt_decrypt, char *data, unsigned int datalen, 
			 vmIP saddr, vmIP daddr, vmPort sport, vmPort dport, 
			 timeval ts, bool init, class cSslDsslSessions *sessions,
			 bool forceTryIfExistsError = false);
	bool isClientHello(char *data, unsigned int datalen, NM_PacketDir dir);
	void setKeyItems(void *key_items);
private:
	NM_PacketDir getDirection(vmIP sip, vmPort sport, vmIP dip, vmPort dport);
	static void dataCallback(NM_PacketDir dir, void* user_data, u_char* data, uint32_t len, DSSL_Pkt* pkt);
	static void errorCallback(void* user_data, int error_code);
	static int password_calback_direct(char *buf, int size, int rwflag, void *userdata);
	static int get_keys(u_char *client_random, u_char *ticket, u_int32_t ticket_len, 
			    DSSL_Session_get_keys_data *get_keys_data, DSSL_Session *session);
	string get_session_data(timeval ts);
	bool restore_session_data(const char *data);
	void store_session(class cSslDsslSessions *sessions, timeval ts, int type_store);
private:
	vmIP ips;
	vmPort ports;
	string keyfile;
	string password;
	vmIP ipc;
	vmPort portc;
	EVP_PKEY *pkey;
	DSSL_ServerInfo* server_info;
	DSSL_Session* session;
	eServerErrors server_error;
	unsigned process_data_counter;
	bool process_error;
	int process_error_code;
	vector<string> *decrypted_data;
	bool get_keys_ok;
	void *key_items;
	u_long stored_at;
	bool restored;
	u_int64_t lastTimeSyslog;
friend class cSslDsslSessions;
};


class cSslDsslSessionKeys {
public:
	enum eSessionKeyType {
		_skt_na,
		_skt_client_random,
		_skt_client_handshake_traffic_secret,
		_skt_server_handshake_traffic_secret,
		_skt_exporter_secret,
		_skt_client_traffic_secret_0,
		_skt_server_traffic_secret_0
	};
	struct sSessionKeyType {
		const char *str;
		eSessionKeyType type;
		unsigned length;
	};
	class cSslDsslSessionKeyIndex {
	public:
		cSslDsslSessionKeyIndex(u_char *client_random = NULL);
		bool operator == (const cSslDsslSessionKeyIndex& other) const { 
			return(!memcmp(this->client_random, other.client_random, SSL3_RANDOM_SIZE)); 
		}
		bool operator < (const cSslDsslSessionKeyIndex& other) const { 
			return(memcmp(this->client_random, other.client_random, SSL3_RANDOM_SIZE) < 0); 
		}
	public:
		u_char client_random[SSL3_RANDOM_SIZE];
	};
	class cSslDsslSessionKeyItem {
	public:
		cSslDsslSessionKeyItem(cSslDsslSessionKeyItem *orig);
		cSslDsslSessionKeyItem(u_char *key = NULL, unsigned key_length = 0);
	public:
		u_char key[SSL3_MASTER_SECRET_SIZE];
		unsigned key_length;
		u_int32_t set_at;
	};
	class cSslDsslSessionKeyItems {
	public:
		~cSslDsslSessionKeyItems() {
			clear();
		}
		void clear();
		cSslDsslSessionKeyItems *clone();
	public:
		map<eSessionKeyType, cSslDsslSessionKeyItem*> keys;
	};
	class cSslDsslSessionTicket {
	public:
		cSslDsslSessionTicket(u_char *ticket_data, u_int32_t ticket_data_len, u_int32_t ts_s, bool ticket_only);
		cSslDsslSessionTicket(const cSslDsslSessionTicket& orig) {
			init();
			clone(orig);
		}
		~cSslDsslSessionTicket() {
			destroy();
		}
		cSslDsslSessionTicket& operator = (const cSslDsslSessionTicket& orig) {
			clone(orig);
			return(*this);
		}
		bool operator == (const cSslDsslSessionTicket& other) const { 
			return(this->ticket_len == other.ticket_len &&
			       !memcmp(this->ticket, other.ticket, this->ticket_len)); 
		}
		bool operator < (const cSslDsslSessionTicket& other) const { 
			return(this->ticket_len != other.ticket_len ?
				this->ticket_len < other.ticket_len :
				memcmp(this->ticket, other.ticket, this->ticket_len) < 0); 
		}
		void clone(const cSslDsslSessionTicket& orig);
		void destroy();
		void init();
	public:
		u_char *ticket;
		u_int32_t ticket_len;
		u_int32_t lifetime;
		u_int32_t set_at;
	};
public:
	cSslDsslSessionKeys();
	~cSslDsslSessionKeys();
	void set(const char *type, u_char *client_random, u_char *key, unsigned key_length);
	void set(eSessionKeyType type, u_char *client_random, u_char *key, unsigned key_length);
	bool get(u_char *client_random, eSessionKeyType type, u_char *key, unsigned *key_length, timeval ts, bool use_wait = true);
	bool get(u_char *client_random, u_char *ticket, u_int32_t ticket_len,
		 DSSL_Session_get_keys_data *keys, timeval ts, bool use_wait,
		 cSslDsslSessionKeyItems **key_items_clone);
	void erase(u_char *client_random);
	void cleanup();
	void clear();
	eSessionKeyType strToEnumType(const char *type);
	const char *enumToStrType(eSessionKeyType type);
	void setKeysToTicket(u_char *ticket_data, u_int32_t ticket_data_len, u_int32_t ts_s, cSslDsslSessionKeyItems *key_items);
private:
	void lock_map_keys() {
		__SYNC_LOCK(this->_sync_map_keys);
	}
	void unlock_map_keys() {
		__SYNC_UNLOCK(this->_sync_map_keys);
	}
	void lock_map_tickets() {
		__SYNC_LOCK(this->_sync_map_tickets);
	}
	void unlock_map_tickets() {
		__SYNC_UNLOCK(this->_sync_map_tickets);
	}
private:
	map<cSslDsslSessionKeyIndex, cSslDsslSessionKeyItems*> keys;
	map<cSslDsslSessionTicket, cSslDsslSessionKeyItems*> tickets;
	volatile int _sync_map_keys;
	volatile int _sync_map_tickets;
	u_int32_t last_cleanup_at;
public:
	static sSessionKeyType session_key_types[];
};

class cSslDsslSessions {
public:
	enum eTypeStoreSession {
		_tss_force = 1 << 0,
		_tss_db = 1 << 1,
		_tss_mem = 1 << 2,
		_tss_force_all = _tss_force | _tss_db | _tss_mem
	};
	struct sSessionData {
		string data;
	};
public:
	cSslDsslSessions();
	~cSslDsslSessions();
public:
	void processData(vector<string> *rslt_decrypt, char *data, unsigned int datalen, vmIP saddr, vmIP daddr, vmPort sport, vmPort dport, timeval ts,
			 bool forceTryIfExistsError = false);
	void destroySession(vmIP saddr, vmIP daddr, vmPort sport, vmPort dport);
	bool keySet(const char *data, unsigned data_length);
	bool keySet(const char *type, const char *client_random, const char *key);
	void keySet(const char *type, u_char *client_random, u_char *key, unsigned key_length);
	bool keyGet(u_char *client_random, cSslDsslSessionKeys::eSessionKeyType type, u_char *key, unsigned *key_length, timeval ts, bool use_wait = true);
	bool keysGet(u_char *client_random, u_char *ticket, u_int32_t ticket_len, 
		     DSSL_Session_get_keys_data *get_keys_data, timeval ts, bool use_wait,
		     cSslDsslSessionKeys::cSslDsslSessionKeyItems **key_items_clone);
	void keyErase(u_char *client_random);
	void keysCleanup();
	void setKeysToTicket(u_char *ticket_data, u_int32_t ticket_data_len, u_int32_t ts_s, cSslDsslSessionKeys::cSslDsslSessionKeyItems *key_items);
	void storeSessions();
private:
	cSslDsslSession *addSession(vmIP ips, vmPort ports, string keyfile);
	NM_PacketDir checkIpPort(vmIP sip, vmPort sport, vmIP dip, vmPort dport);
	void init();
	void term();
	void loadSessions();
	void setSessionData(vmIP sip, vmPort sport, vmIP cip, vmPort cport, const char *session_data);
	void deleteOldSessions(timeval ts);
	string storeSessionsTableName();
	void lock_sessions() {
		__SYNC_LOCK(this->_sync_sessions);
	}
	void unlock_sessions() {
		__SYNC_UNLOCK(this->_sync_sessions);
	}
	void lock_sessions_db() {
		__SYNC_LOCK(this->_sync_sessions_db);
	}
	void unlock_sessions_db() {
		__SYNC_UNLOCK(this->_sync_sessions_db);
	}
private:
	map<sStreamId, cSslDsslSession*> sessions;
	map<sStreamId, sSessionData> sessions_db;
	volatile int _sync_sessions;
	volatile int _sync_sessions_db;
	cSslDsslSessionKeys session_keys;
	SqlDb *sqlDb;
	u_long last_delete_old_sessions_at;
	bool exists_sessions_table;
friend class cSslDsslSession;
};

class cClientRandomServer : public cServer {
public:
	cClientRandomServer();
	~cClientRandomServer();
	virtual void createConnection(cSocket *socket);
};

class cClientRandomConnection : public cServerConnection {
public:
	cClientRandomConnection(cSocket *socket);
	~cClientRandomConnection();
	virtual void connection_process();
	virtual void evData(u_char *data, size_t dataLen);
};

struct sSslDsslStatsItem_time {
	sSslDsslStatsItem_time() {
		reset();
	}
	void add_delay_from_act(u_int64_t time_us) {
		u_int64_t act = getTimeMS_rdtsc();
		if(act >= time_us / 1000) {
			u_int64_t delay = act - time_us / 1000;
			sum_ms += delay;
			++count;
			if(delay > max_ms) {
				max_ms = delay;
			}
		}
	}
	u_int32_t avg_ms() {
		if(count > 0) {
			return(sum_ms / count);
		}
		return(0);
	}
	void reset() {
		memset(this, 0, sizeof(*this));
	}
	string str();
	u_int32_t max_ms;
	u_int64_t sum_ms;
	u_int32_t count;
};

struct sSslDsslStats {
	void reset() {
		delay_processData_begin.reset();
		delay_processData_end.reset();
		delay_keys_get_begin.reset();
		delay_keys_get_end.reset();
		delay_processPacket.reset();
		delay_parseSdp.reset();
	}
	string str();
	sSslDsslStatsItem_time delay_processData_begin;
	sSslDsslStatsItem_time delay_processData_end;
	sSslDsslStatsItem_time delay_keys_get_begin;
	sSslDsslStatsItem_time delay_keys_get_end;
	sSslDsslStatsItem_time delay_processPacket;
	sSslDsslStatsItem_time delay_parseSdp;
};


#endif //HAVE_OPENSSL101 && HAVE_LIBGNUTLS


void ssl_dssl_init();
void ssl_dssl_clean();
void decrypt_ssl_dssl(vector<string> *rslt_decrypt, char *data, unsigned int datalen, vmIP saddr, vmIP daddr, vmPort sport, vmPort dport, timeval ts,
		      bool forceTryIfExistsError = false);
void end_decrypt_ssl_dssl(vmIP saddr, vmIP daddr, vmPort sport, vmPort dport);
bool ssl_parse_client_random(u_char *data, unsigned datalen);
void ssl_parse_client_random(const char *fileName);

void clientRandomServerStart(const char *host, int port);
void clientRandomServerStop();

bool find_master_secret(u_char *client_random, u_char *key, unsigned *key_length);
void erase_client_random(u_char *client_random);

string ssl_stats_str();
void ssl_stats_reset();
void ssl_stats_add_delay_processPacket(u_int64_t time_us);
void ssl_stats_add_delay_parseSdp(u_int64_t time_us);

bool ssl_sessionkey_enable();
void ssl_sessionkey_log(string &str);


#endif //SSL_DSSL_H
