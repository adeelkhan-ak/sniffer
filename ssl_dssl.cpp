#include "voipmonitor.h"
#include "pcap_queue.h"

#if defined HAVE_OPENSSL101 and defined HAVE_LIBGNUTLS
#include <gcrypt.h>
#endif //HAVE_OPENSSL101 and HAVE_LIBGNUTLS

#include "ssl_dssl.h"


#if defined(HAVE_OPENSSL101) and defined(HAVE_LIBGNUTLS)


extern int isSslIpPort(vmIP sip, vmPort sport, vmIP dip, vmPort dport);
extern string sslIpPort_get_keyfile(vmIP sip, vmPort sport, vmIP dip, vmPort dport);

static void jsonAddKey(JsonExport *json, const char *name, DSSL_Session_get_keys_data_item *key);
static void jsonGetKey(JsonItem *json, const char *name, DSSL_Session_get_keys_data_item *key);

extern int opt_ssl_store_sessions;
extern int opt_ssl_store_sessions_expiration_hours;
extern MySqlStore *sqlStore;
extern int opt_id_sensor;
extern int opt_nocdr;
extern sExistsColumns existsColumns;

static cSslDsslSessions *SslDsslSessions;
static DSSL_Env *SslDsslEnv;

static sSslDsslStats stats;


cSslDsslSession::cSslDsslSession(vmIP ips, vmPort ports, string keyfile, string password) {
	this->ips = ips;
	this->ports = ports;
	this->keyfile = keyfile;
	this->password = password;
	ipc.clear();
	portc.clear();
	pkey = NULL;
	server_info = NULL;
	session = NULL;
	server_error = _se_na;
	process_data_counter = 0;
	process_error = false;
	process_error_code = 0;
	get_keys_ok = false;
	key_items = NULL;
	stored_at = 0;
	restored = false;
	lastTimeSyslog = 0;
	init();
}

cSslDsslSession::~cSslDsslSession() {
	term();
}

void cSslDsslSession::setClientIpPort(vmIP ipc, vmPort portc) {
	this->ipc = ipc;
	this->portc = portc;
}

void cSslDsslSession::init() {
	if(initServer()) {
		initSession();
	}
}

void cSslDsslSession::term() {
	termSession();
	termServer();
	if(key_items) {
		delete (cSslDsslSessionKeys::cSslDsslSessionKeyItems*)key_items;
		key_items = NULL;
	}
}

bool cSslDsslSession::initServer() {
	vector<EVP_PKEY*> pkeys;
	if(keyfile.length()) {
		vector<string> keyfiles = split(keyfile.c_str(), split(",", '|'), true);
		eServerErrors _server_error = _se_na;
		u_int64_t actTime = getTimeMS();
		bool enableSyslog = actTime - 1000 > lastTimeSyslog;
		for(unsigned i = 0; i < keyfiles.size(); i++) {
			FILE* file_keyfile = fopen(keyfiles[i].c_str(), "r");
			if(!file_keyfile) {
				_server_error = _se_keyfile_not_exists;
				if(enableSyslog) {
					syslog(LOG_NOTICE, "ssl - missing keyfile %s", keyfiles[i].c_str());
					lastTimeSyslog = actTime;
				}
				continue;
			}
			EVP_PKEY* pkey = NULL;
			if(!PEM_read_PrivateKey(file_keyfile, &pkey, cSslDsslSession::password_calback_direct, (void*)password.c_str())) {
				fclose(file_keyfile);
				_server_error = _se_load_key_failed;
				if(enableSyslog) {
					syslog(LOG_NOTICE, "ssl - failed read keyfile %s", keyfiles[i].c_str());
					lastTimeSyslog = actTime;
				}
				continue;
			}
			if(pkey) {
				pkeys.push_back(pkey);
			}
			fclose(file_keyfile);
		}
		if(!pkeys.size()) {
			server_error = _server_error;
			return(false);
		}
	}
	this->server_info = new FILE_LINE(0) DSSL_ServerInfo;
	this->server_info->server_ip = *(in_addr*)&ips;
	this->server_info->port = ports.getPort();
	if(pkeys.size()) {
		this->server_info->pkeys = new FILE_LINE(0) EVP_PKEY*[pkeys.size()];
		this->server_info->pkeys_count = pkeys.size();
		this->server_info->pkeys_index_ok = 0;
		for(unsigned i = 0; i < pkeys.size(); i++) {
			this->server_info->pkeys[i] = pkeys[i];
		}
	} else {
		this->server_info->pkeys = NULL;
		this->server_info->pkeys_count = 0;
		this->server_info->pkeys_index_ok = 0;
	}
	server_error = _se_ok;
	return(true);
}

bool cSslDsslSession::initSession() {
	session = new FILE_LINE(0) DSSL_Session;
	DSSL_SessionInit(NULL, session, server_info);
	session->env = SslDsslEnv;
	session->last_packet = new FILE_LINE(0) DSSL_Pkt;
	session->get_keys_fce = this->get_keys;
	session->get_keys_fce_call_data[0] = this;
	session->get_keys_fce_call_data[1] = SslDsslSessions;
	extern bool opt_ssl_ignore_error_invalid_mac;
	session->ignore_error_invalid_mac = opt_ssl_ignore_error_invalid_mac;
	extern bool opt_ssl_ignore_error_bad_finished_digest;
	session->ignore_error_bad_finished_digest = opt_ssl_ignore_error_bad_finished_digest;
	extern int opt_ssl_tls_12_sessionkey_mode;
	session->tls_12_sessionkey_via_ws = opt_ssl_tls_12_sessionkey_mode == 1;
	memset(session->last_packet, 0, sizeof(*session->last_packet));
	DSSL_SessionSetCallback(session, cSslDsslSession::dataCallback, cSslDsslSession::errorCallback, this);
	return(true);
}

void cSslDsslSession::termServer() {
	if(server_info) {
		if(server_info->pkeys) {
			for(unsigned i = 0; i < server_info->pkeys_count; i++) {
				EVP_PKEY_free(server_info->pkeys[i]);
			}
			delete [] server_info->pkeys;
			server_info->pkeys = NULL;
			server_info->pkeys_count = 0;
			server_info->pkeys_index_ok = 0;
		}
		delete server_info;
		server_info = NULL;
	}
	server_error = _se_na;
}

void cSslDsslSession::termSession() {
	if(session) {
		delete session->last_packet;
		session->last_packet = NULL;
		DSSL_SessionDeInit(session);
		delete session;
		session = NULL;
	}
	process_data_counter = 0;
	process_error = false;
	process_error_code = 0;
	stored_at = 0;
	restored = false;
}

void cSslDsslSession::processData(vector<string> *rslt_decrypt, char *data, unsigned int datalen, 
				  vmIP saddr, vmIP daddr, vmPort sport, vmPort dport, 
				  timeval ts, bool init, class cSslDsslSessions *sessions,
				  bool forceTryIfExistsError) {
	rslt_decrypt->clear();
	if(!session) {
		return;
	}
	NM_PacketDir dir = this->getDirection(saddr, sport, daddr, dport);
	if(dir != ePacketDirInvalid) {
		bool reinit = false;
		if(!init && ((process_error && !forceTryIfExistsError) || restored)) {
			if(this->isClientHello(data, datalen, dir)) {
				this->term();
				this->init();
				reinit = true;
			} else if(process_error && !forceTryIfExistsError) {
				return;
			}
		}
		for(unsigned pass = 1; pass <= (init || reinit ? 1 : 2); pass++) {
			if(pass == 2) {
				if(this->isClientHello(data, datalen, dir)) {
					this->term();
					this->init();
					rslt_decrypt->clear();
				} else {
					break;
				}
			}
			session->last_packet->pcap_header.ts = ts;
			this->decrypted_data = rslt_decrypt;
			int rc = DSSL_SessionProcessData(session, dir, (u_char*)data, datalen);
			if(rc == DSSL_RC_OK) {
				if(process_error && forceTryIfExistsError) {
					process_error = false;
					process_error_code = 0;
				}
				if(opt_ssl_store_sessions && !opt_nocdr && !init) {
					this->store_session(sessions, ts, cSslDsslSessions::_tss_db);
				}
				break;
			}
		}
		if(this->get_keys_ok && !this->process_data_counter) {
			uint8_t record_type = data[0];
			uint16_t record_len = ntohs(*(uint16_t*)(data + 3));
		        if(record_type == 22 && record_len + 5u == datalen) {
				uint8_t handshake_type = data[5];
				uint16_t handshake_len = ntohs(*(uint16_t*)(data + 7));
				if(handshake_type == 4 && handshake_len + 9u == datalen) {
					u_char *session_ticket_data = (u_char*)data + 9;
					u_int16_t session_ticket_data_len = datalen - 9;
					if(key_items) {
						SslDsslSessions->setKeysToTicket(session_ticket_data, session_ticket_data_len, ts.tv_sec,
										 (cSslDsslSessionKeys::cSslDsslSessionKeyItems*)key_items);
					}
				}
			}
		}
	}
}

bool cSslDsslSession::isClientHello(char *data, unsigned int datalen, NM_PacketDir dir) {
	bool isClientHello = false;
	if(dir == ePacketDirFromClient) {
		NM_ERROR_DISABLE_LOG;
		uint16_t ver = 0;
		if(!ssl_detect_client_hello_version((u_char*)data, datalen, &ver) && ver) {
			isClientHello = true;
		}
		NM_ERROR_ENABLE_LOG;
	}
	return(isClientHello);
}

void cSslDsslSession::setKeyItems(void *key_items) {
	if(this->key_items) {
		delete (cSslDsslSessionKeys::cSslDsslSessionKeyItems*)this->key_items;
	}
	this->key_items = key_items;
}

NM_PacketDir cSslDsslSession::getDirection(vmIP sip, vmPort sport, vmIP dip, vmPort dport) {
	return(dip == this->ips && dport == this->ports ?
		ePacketDirFromClient :
	       sip == this->ips && sport == this->ports ?
		ePacketDirFromServer :
		ePacketDirInvalid);
}

void cSslDsslSession::dataCallback(NM_PacketDir /*dir*/, void* user_data, u_char* data, uint32_t len, DSSL_Pkt* /*pkt*/) {
	cSslDsslSession *me = (cSslDsslSession*)user_data;
	me->decrypted_data->push_back(string((char*)data, len));
	++me->process_data_counter;
}

void cSslDsslSession::errorCallback(void* user_data, int error_code) {
	cSslDsslSession *me = (cSslDsslSession*)user_data;
	if(!me->process_error) {
		extern bool opt_ssl_log_errors;
		if(opt_ssl_log_errors) {
			syslog(LOG_ERR, "SSL decode failed: err code %i, connection %s:%u -> %s:%u", 
			       error_code,
			       me->ipc.getString().c_str(),
			       me->portc.getPort(),
			       me->ips.getString().c_str(),
			       me->ports.getPort());
		}
		me->process_error = true;
	}
	me->process_error_code = error_code;
}

int cSslDsslSession::password_calback_direct(char *buf, int size, int /*rwflag*/, void *userdata) {
	char* password = (char*)userdata;
	int length = strlen(password);
	strncpy(buf, password, size);
	return(length);
}

int cSslDsslSession::get_keys(u_char *client_random, u_char *ticket, u_int32_t ticket_len,
			      DSSL_Session_get_keys_data *get_keys_data, DSSL_Session *session) {
	cSslDsslSessionKeys::cSslDsslSessionKeyItems *key_items_clone = NULL;
	if(((cSslDsslSessions*)session->get_keys_fce_call_data[1])->keysGet(client_random, ticket, ticket_len, 
									    get_keys_data, session->last_packet->pcap_header.ts, true,
									    &key_items_clone)) {
		((cSslDsslSession*)session->get_keys_fce_call_data[0])->get_keys_ok = true;
		if(key_items_clone) {
			((cSslDsslSession*)session->get_keys_fce_call_data[0])->setKeyItems(key_items_clone);
		}
		return(1);
	} else {
		if(key_items_clone) {
			delete key_items_clone;
		}
	}
	return(0);
}

string cSslDsslSession::get_session_data(timeval ts) {
	JsonExport json;
	json.add("version", session->version);
	json.add("cipher_suite", session->cipher_suite);
	json.add("compression_method", session->compression_method);
	json.add("client_random", hexencode(session->client_random, sizeof(session->client_random)));
	if(session->version == TLS1_3_VERSION) {
		jsonAddKey(&json, "key_client_random", &session->get_keys_rslt_data.client_random);
		jsonAddKey(&json, "key_client_handshake_traffic_secret", &session->get_keys_rslt_data.client_handshake_traffic_secret);
		jsonAddKey(&json, "key_server_handshake_traffic_secret", &session->get_keys_rslt_data.server_handshake_traffic_secret);
		jsonAddKey(&json, "key_exporter_secret", &session->get_keys_rslt_data.exporter_secret);
		jsonAddKey(&json, "key_client_traffic_secret_0", &session->get_keys_rslt_data.client_traffic_secret_0);
		jsonAddKey(&json, "key_server_traffic_secret_0", &session->get_keys_rslt_data.server_traffic_secret_0);
		json.add("seq_server", session->tls_session_server_seq);
		json.add("seq_client", session->tls_session_client_seq);
	} else {
		json.add("server_random", hexencode(session->server_random, sizeof(session->server_random)));
		json.add("master_secret", hexencode(session->master_secret, sizeof(session->master_secret)));
		if(session->tls_session) {
			json.add("seq_server", session->tls_session_server_seq);
			json.add("seq_client", session->tls_session_client_seq);
			json.add("state", session->tls_session_state);
		}
	}
	json.add("c_dec_version", session->c_dec.version);
	json.add("s_dec_version", session->s_dec.version);
	json.add("tls_ws", session->tls_session != NULL);
	json.add("stored_at", ts.tv_sec);
	return(json.getJson());
}

bool cSslDsslSession::restore_session_data(const char *data) {
	if(!session) {
		return(false);
	}
	JsonItem jsonData;
	jsonData.parse(data);
	session->version = atoi(jsonData.getValue("version").c_str());
	session->cipher_suite = atoi(jsonData.getValue("cipher_suite").c_str());
	session->compression_method = atoi(jsonData.getValue("compression_method").c_str());
	hexdecode(session->client_random, jsonData.getValue("client_random").c_str(), sizeof(session->client_random));
	if(session->version == TLS1_3_VERSION) {
		jsonGetKey(&jsonData, "key_client_random", &session->get_keys_rslt_data.client_random);
		jsonGetKey(&jsonData, "key_client_handshake_traffic_secret", &session->get_keys_rslt_data.client_handshake_traffic_secret);
		jsonGetKey(&jsonData, "key_server_handshake_traffic_secret", &session->get_keys_rslt_data.server_handshake_traffic_secret);
		jsonGetKey(&jsonData, "key_exporter_secret", &session->get_keys_rslt_data.exporter_secret);
		jsonGetKey(&jsonData, "key_client_traffic_secret_0", &session->get_keys_rslt_data.client_traffic_secret_0);
		jsonGetKey(&jsonData, "key_server_traffic_secret_0", &session->get_keys_rslt_data.server_traffic_secret_0);
		session->tls_session_server_seq = 
		session->tls_session_server_seq_saved = atoll(jsonData.getValue("seq_server").c_str());
		session->tls_session_client_seq = 
		session->tls_session_client_seq_saved =  atoll(jsonData.getValue("seq_client").c_str());
		if(!tls_13_generate_keys(session, true)) {
			return(false);
		}
	} else if((session->version == TLS1_2_VERSION || session->version == TLS1_1_VERSION || session->version == TLS1_VERSION) && atoi(jsonData.getValue("tls_ws").c_str())) {
		hexdecode(session->server_random, jsonData.getValue("server_random").c_str(), sizeof(session->server_random));
		hexdecode(session->master_secret, jsonData.getValue("master_secret").c_str(), sizeof(session->master_secret));
		session->tls_session_server_seq = 
		session->tls_session_server_seq_saved = atoll(jsonData.getValue("seq_server").c_str());
		session->tls_session_client_seq = 
		session->tls_session_client_seq_saved = atoll(jsonData.getValue("seq_client").c_str());
		session->tls_session_state = atol(jsonData.getValue("state").c_str());
		if(!tls_12_generate_keys(session, true)) {
			return(false);
		}
	} else {
		hexdecode(session->server_random, jsonData.getValue("server_random").c_str(), sizeof(session->server_random));
		hexdecode(session->master_secret, jsonData.getValue("master_secret").c_str(), sizeof(session->master_secret));
		if(ssls_generate_keys(session) != DSSL_RC_OK ||
		   ssls_set_session_version(session, session->version) != DSSL_RC_OK) {
			return(false);
		}
	}
	if(dssl_decoder_stack_flip_cipher(&session->c_dec) != DSSL_RC_OK ||
	   dssl_decoder_stack_flip_cipher(&session->s_dec) != DSSL_RC_OK) {
		return(false);
	}
	session->c_dec.sess = session;
	session->s_dec.sess = session;
	if(dssl_decoder_stack_set(&session->c_dec, session, atoi(jsonData.getValue("c_dec_version").c_str())) == DSSL_RC_OK &&
	   dssl_decoder_stack_set(&session->s_dec, session, atoi(jsonData.getValue("s_dec_version").c_str())) == DSSL_RC_OK) {
		restored = true;
		stored_at = atol(jsonData.getValue("stored_at").c_str());
		return(true);
	}
	return(false);
}

void cSslDsslSession::store_session(cSslDsslSessions *sessions, timeval ts, int type_store) {
	if(this->process_data_counter > 0 &&
	   this->session->c_dec.version && this->session->s_dec.version &&
	    ((type_store & cSslDsslSessions::_tss_force) || 
	     ((type_store & cSslDsslSessions::_tss_db) && (!this->stored_at || this->stored_at < (u_long)(ts.tv_sec - (session->version == TLS1_3_VERSION ? 60 : 3600)))))) {
		string session_data = get_session_data(ts);
		if(type_store & cSslDsslSessions::_tss_mem) {
			sessions->setSessionData(ips, ports, ipc, portc, session_data.c_str());
		}
		if((type_store & cSslDsslSessions::_tss_db) &&
		   opt_ssl_store_sessions && !opt_nocdr && sessions->exists_sessions_table) {
			SqlDb_row session_row_insert;
			session_row_insert.add(existsColumns.ssl_sessions_id_sensor_is_unsigned && opt_id_sensor < 0 ? 0 : opt_id_sensor, "id_sensor");
			session_row_insert.add(ips, "serverip", false, sessions->sqlDb, sessions->storeSessionsTableName().c_str());
			session_row_insert.add(ports.getPort(), "serverport");
			session_row_insert.add(ipc, "clientip", false, sessions->sqlDb, sessions->storeSessionsTableName().c_str());
			session_row_insert.add(portc.getPort(), "clientport");
			session_row_insert.add(sqlDateTimeString(ts.tv_sec), "stored_at");
			session_row_insert.add(session_data, "session");
			SqlDb_row session_row_update;
			session_row_update.add(sqlDateTimeString(ts.tv_sec), "stored_at");
			session_row_update.add(session_data, "session");
			if(!sessions->sqlDb) {
				sessions->sqlDb = createSqlObject();
			}
			sqlStore->query_lock(MYSQL_ADD_QUERY_END(
					     sessions->sqlDb->insertOrUpdateQuery(sessions->storeSessionsTableName(), session_row_insert, session_row_update, false, true)),
					     STORE_PROC_ID_OTHER, 0);
			this->stored_at = ts.tv_sec;
			this->session->tls_session_server_seq_saved = this->session->tls_session_server_seq;
			this->session->tls_session_client_seq_saved = this->session->tls_session_client_seq;
			sessions->deleteOldSessions(ts);
		}
	}
}


cSslDsslSessionKeys::cSslDsslSessionKeyIndex::cSslDsslSessionKeyIndex(u_char *client_random) {
	if(client_random) {
		memcpy(this->client_random, client_random, SSL3_RANDOM_SIZE);
	}
}

cSslDsslSessionKeys::cSslDsslSessionKeyItem::cSslDsslSessionKeyItem(cSslDsslSessionKeyItem *orig) {
	memcpy(this->key, orig->key, orig->key_length);
	this->key_length = orig->key_length;
	set_at = orig->set_at;
}

cSslDsslSessionKeys::cSslDsslSessionKeyItem::cSslDsslSessionKeyItem(u_char *key, unsigned key_length) {
	if(key) {
		memcpy(this->key, key, key_length);
		this->key_length = key_length;
		set_at = getTimeS();
	}
}

void cSslDsslSessionKeys::cSslDsslSessionKeyItems::clear() {
	for(map<eSessionKeyType, cSslDsslSessionKeyItem*>::iterator iter = keys.begin(); iter != keys.end(); iter++) {
		if(iter->second) {
			delete iter->second;
			iter->second = NULL;
		}
	}
}

cSslDsslSessionKeys::cSslDsslSessionKeyItems *cSslDsslSessionKeys::cSslDsslSessionKeyItems::clone() {
	cSslDsslSessionKeyItems *clone = new FILE_LINE(0) cSslDsslSessionKeyItems;
	for(map<eSessionKeyType, cSslDsslSessionKeyItem*>::iterator iter = keys.begin(); iter != keys.end(); iter++) {
		if(iter->second) {
			clone->keys[iter->first] = new FILE_LINE(0) cSslDsslSessionKeyItem(iter->second);
		}
	}
	return(clone);
}

cSslDsslSessionKeys::cSslDsslSessionTicket::cSslDsslSessionTicket(u_char *ticket_data, u_int32_t ticket_data_len, u_int32_t ts_s, bool ticket_only) {
	init();
	if(ticket_data && ticket_data_len) {
		if(ticket_only) {
			ticket_len = ticket_data_len;
			ticket = new FILE_LINE(0) u_char[ticket_len];
			memcpy(ticket, ticket_data, ticket_len);
		} else {
			ticket_len = ntohs(*(u_int16_t*)(ticket_data + 4));
			if(ticket_len == ticket_data_len - 6) {
				lifetime = ntohl(*(u_int32_t*)ticket_data);
				ticket = new FILE_LINE(0) u_char[ticket_len];
				memcpy(ticket, ticket_data + 6, ticket_len);
			} else {
				ticket_len = 0;
			}
			set_at = ts_s;
		}
	}
}

void cSslDsslSessionKeys::cSslDsslSessionTicket::clone(const cSslDsslSessionTicket& orig) {
	destroy();
	if(orig.ticket && orig.ticket_len) {
		this->ticket = new FILE_LINE(0) u_char[orig.ticket_len];
		memcpy(this->ticket, orig.ticket, orig.ticket_len);
		this->ticket_len = orig.ticket_len;
	}
	this->lifetime = orig.lifetime;
	this->set_at = orig.set_at;
}

void cSslDsslSessionKeys::cSslDsslSessionTicket::destroy() {
	if(ticket) {
		delete [] ticket;
	}
	init();
}

void cSslDsslSessionKeys::cSslDsslSessionTicket::init() {
	ticket = NULL;
	ticket_len = 0;
	lifetime = 0;
	set_at = 0;
}

cSslDsslSessionKeys::sSessionKeyType cSslDsslSessionKeys::session_key_types[] = {
	{ "CLIENT_RANDOM", cSslDsslSessionKeys::_skt_client_random, 0 },
	{ "CLIENT_HANDSHAKE_TRAFFIC_SECRET", cSslDsslSessionKeys::_skt_client_handshake_traffic_secret, 0 },
	{ "SERVER_HANDSHAKE_TRAFFIC_SECRET", cSslDsslSessionKeys::_skt_server_handshake_traffic_secret, 0 },
	{ "EXPORTER_SECRET", cSslDsslSessionKeys::_skt_exporter_secret, 0 },
	{ "CLIENT_TRAFFIC_SECRET_0", cSslDsslSessionKeys::_skt_client_traffic_secret_0, 0 },
	{ "SERVER_TRAFFIC_SECRET_0", cSslDsslSessionKeys::_skt_server_traffic_secret_0, 0 },
	{ NULL, cSslDsslSessionKeys::_skt_na, 0 }
};

cSslDsslSessionKeys::cSslDsslSessionKeys() {
	_sync_map_keys = 0;
	_sync_map_tickets = 0;
	last_cleanup_at = 0;
	for(unsigned i = 0; session_key_types[i].str; i++) {
		session_key_types[i].length = strlen(session_key_types[i].str);
	}
}

cSslDsslSessionKeys::~cSslDsslSessionKeys() {
	clear();
}

void cSslDsslSessionKeys::set(const char *type, u_char *client_random, u_char *key, unsigned key_length) {
	eSessionKeyType type_e = strToEnumType(type);
	if(type_e == _skt_na) {
		return;
	}
	set(type_e, client_random, key, key_length);
}

void cSslDsslSessionKeys::set(eSessionKeyType type, u_char *client_random, u_char *key, unsigned key_length) {
	cSslDsslSessionKeyIndex index(client_random);
	cSslDsslSessionKeyItem *item = new FILE_LINE(0) cSslDsslSessionKeyItem(key, key_length);
	lock_map_keys();
	if(keys.find(index) == keys.end()) {
		keys[index] = new FILE_LINE(0) cSslDsslSessionKeyItems;
	}
	if(keys[index]->keys[type]) {
		delete keys[index]->keys[type];
	}
	keys[index]->keys[type] = item;
	unlock_map_keys();
}

bool cSslDsslSessionKeys::get(u_char *client_random, eSessionKeyType type, u_char *key, unsigned *key_length, timeval ts, bool use_wait) {
	extern int opt_disable_wait_for_ssl_key;
	if(opt_disable_wait_for_ssl_key && use_wait) {
		use_wait = false;
	}
	string log_ssl_sessionkey;
	if(ssl_sessionkey_enable()) {
		log_ssl_sessionkey = 
			string("find clientrandom with type ") + enumToStrType(type) +
			"; clientrandom: " + hexdump_to_string(client_random, SSL3_RANDOM_SIZE);
	}
	bool rslt = false;
	cSslDsslSessionKeyIndex index(client_random);
	int64_t waitUS = -1;
	extern int ssl_client_random_maxwait_ms;
	if(ssl_client_random_maxwait_ms > 0 && use_wait) {
		extern PcapQueue_readFromFifo *pcapQueueQ;
		if(pcapQueueQ) {
			u_int64_t pcapQueueQ_lastUS = pcapQueueQ->getLastUS();
			u_int64_t ts_us = getTimeUS(ts);
			waitUS = pcapQueueQ_lastUS > ts_us ? pcapQueueQ_lastUS - ts_us : 0;
		}
	}
	do {
		lock_map_keys();
		map<cSslDsslSessionKeyIndex, cSslDsslSessionKeyItems*>::iterator iter1 = keys.find(index);
		if(iter1 != keys.end()) {
			map<eSessionKeyType, cSslDsslSessionKeyItem*>::iterator iter2 = iter1->second->keys.find(type);
			if(iter2 != iter1->second->keys.end()) {
				memcpy(key, iter2->second->key, iter2->second->key_length);
				*key_length = iter2->second->key_length;
				rslt = true;
			}
		}
		unlock_map_keys();
		if(!rslt) {
			if(waitUS >= 0 && waitUS < ssl_client_random_maxwait_ms * 1000ll) {
				USLEEP(1000);
				waitUS += 1000;
			} else {
				break;
			}
		}
	} while(!rslt && waitUS >= 0);
	if(ssl_sessionkey_enable()) {
		if(rslt) {
			log_ssl_sessionkey +=
				"; FOUND: " + hexdump_to_string(key, *key_length);
		} else {
			log_ssl_sessionkey +=
				"; NOT FOUND";
		}
		ssl_sessionkey_log(log_ssl_sessionkey);
	}
	return(rslt);
}

bool cSslDsslSessionKeys::get(u_char *client_random, u_char *ticket, u_int32_t ticket_len, 
			      DSSL_Session_get_keys_data *keys, timeval ts, bool use_wait,
			      cSslDsslSessionKeyItems **key_items_clone) {
	/*
	static u_char _client_random[SSL3_RANDOM_SIZE];
	if(!_client_random[0]) {
		mempcpy(_client_random, client_random, SSL3_RANDOM_SIZE);
	} else {
		client_random = _client_random;
	}
	*/
	extern int opt_disable_wait_for_ssl_key;
	if(opt_disable_wait_for_ssl_key && use_wait) {
		use_wait = false;
	}
	string log_ssl_sessionkey;
	if(ssl_sessionkey_enable()) {
		if(client_random) {
			log_ssl_sessionkey = 
				string("find clientrandom for all type ") +
				"; clientrandom: " + hexdump_to_string(client_random, SSL3_RANDOM_SIZE);
		} else {
			log_ssl_sessionkey = 
				string("find ticket for all type ") +
				"; ticket: " + hexdump_to_string(ticket, min(ticket_len, 50u)) + (ticket_len > 50 ? "..." : "");
		}
	}
	if(sverb.ssl_stats && client_random) {
		stats.delay_keys_get_begin.add_delay_from_act(getTimeUS(ts));
	}
	bool rslt = false;
	int64_t waitUS = -1;
	extern int ssl_client_random_maxwait_ms;
	if(ssl_client_random_maxwait_ms > 0 && use_wait && client_random) {
		extern PcapQueue_readFromFifo *pcapQueueQ;
		if(pcapQueueQ) {
			u_int64_t pcapQueueQ_lastUS = pcapQueueQ->getLastUS();
			u_int64_t ts_us = getTimeUS(ts);
			waitUS = pcapQueueQ_lastUS > ts_us ? pcapQueueQ_lastUS - ts_us : 0;
		}
	}
	do {
		cSslDsslSessionKeyItems *key_items = NULL;
		if(client_random) {
			lock_map_keys();
			cSslDsslSessionKeyIndex index(client_random);
			map<cSslDsslSessionKeyIndex, cSslDsslSessionKeyItems*>::iterator iter = this->keys.find(index);
			if(iter != this->keys.end() && iter->second->keys.size()) {
				key_items = iter->second;
			}
		} else {
			lock_map_tickets();
			cSslDsslSessionTicket index(ticket, ticket_len, 0, true);
			map<cSslDsslSessionTicket, cSslDsslSessionKeyItems*>::iterator iter = this->tickets.find(index);
			if(iter != this->tickets.end() && iter->second->keys.size() &&
			   ts.tv_sec < iter->first.set_at + iter->first.lifetime) {
				key_items = iter->second;
			}
		}
		if(key_items) {
			map<eSessionKeyType, cSslDsslSessionKeyItem*>::iterator iter;
			for(iter = key_items->keys.begin(); iter != key_items->keys.end(); iter++) {
				DSSL_Session_get_keys_data_item *key_dst = NULL;
				switch(iter->first) {
				case _skt_client_random:
					key_dst = &keys->client_random;
					break;
				case _skt_client_handshake_traffic_secret:
					key_dst = &keys->client_handshake_traffic_secret;
					break;
				case _skt_server_handshake_traffic_secret:
					key_dst = &keys->server_handshake_traffic_secret;
					break;
				case _skt_exporter_secret:
					key_dst = &keys->exporter_secret;
					break;
				case _skt_client_traffic_secret_0:
					key_dst = &keys->client_traffic_secret_0;
					break;
				case _skt_server_traffic_secret_0:
					key_dst = &keys->server_traffic_secret_0;
					break;
				case _skt_na:
					break;
				}
				if(key_dst) {
					memcpy(key_dst->key, iter->second->key, iter->second->key_length);
					key_dst->length = iter->second->key_length;
				}
			}
			if(isSetKey(&keys->client_random) ||
			   (isSetKey(&keys->client_traffic_secret_0) && isSetKey(&keys->server_traffic_secret_0))) {
				rslt =true;
				keys->set = true;
				if(key_items_clone) {
					*key_items_clone = key_items->clone();
				}
			}
		}
		if(client_random) {
			unlock_map_keys();
		} else {
			unlock_map_tickets();
		}
		if(!rslt) {
			if(waitUS >= 0 && waitUS < ssl_client_random_maxwait_ms * 1000ll && client_random) {
				USLEEP(1000);
				waitUS += 1000;
			} else {
				break;
			}
		}
	} while(!rslt && waitUS >= 0);
	if(ssl_sessionkey_enable()) {
		if(rslt) {
			log_ssl_sessionkey +=
				"; FOUND";
		} else {
			log_ssl_sessionkey +=
				"; NOT FOUND";
		}
		ssl_sessionkey_log(log_ssl_sessionkey);
	}
	if(sverb.ssl_stats && client_random) {
		stats.delay_keys_get_end.add_delay_from_act(getTimeUS(ts));
	}
	return(rslt);
}

void cSslDsslSessionKeys::erase(u_char *client_random) {
	cSslDsslSessionKeyIndex index(client_random);
	lock_map_keys();
	map<cSslDsslSessionKeyIndex, cSslDsslSessionKeyItems*>::iterator iter = keys.find(index);
	if(iter != keys.end()) {
		delete iter->second;
		keys.erase(iter);
	}
	unlock_map_keys();
}

void cSslDsslSessionKeys::cleanup() {
	u_int32_t now = getTimeS();
	if(!last_cleanup_at || last_cleanup_at + 600 < now) {
		lock_map_keys();
		for(map<cSslDsslSessionKeyIndex, cSslDsslSessionKeyItems*>::iterator iter1 = keys.begin(); iter1 != keys.end();) {
			map<eSessionKeyType, cSslDsslSessionKeyItem*>::iterator iter2;
			for(iter2 = iter1->second->keys.begin(); iter2 != iter1->second->keys.end();) {
				if(iter2->second->set_at + 3600 < now) {
					delete iter2->second;
					iter1->second->keys.erase(iter2++);
				} else {
					iter2++;
				}
			}
			if(!iter1->second->keys.size()) {
				delete iter1->second;
				keys.erase(iter1++);
			} else {
				iter1++;
			}
		}
		unlock_map_keys();
		lock_map_tickets();
		for(map<cSslDsslSessionTicket, cSslDsslSessionKeyItems*>::iterator iter1 = tickets.begin(); iter1 != tickets.end();) {
			if(iter1->first.set_at + iter1->first.lifetime + 600 < now) {
				delete iter1->second;
				tickets.erase(iter1++);
			} else {
				iter1++;
			}
		}
		unlock_map_tickets();
		last_cleanup_at = now;
	}
}

void cSslDsslSessionKeys::clear() {
	lock_map_keys();
	for(map<cSslDsslSessionKeyIndex, cSslDsslSessionKeyItems*>::iterator iter = keys.begin(); iter != keys.end(); iter++) {
		delete iter->second;
	}
	keys.clear();
	unlock_map_keys();
	lock_map_tickets();
	for(map<cSslDsslSessionTicket, cSslDsslSessionKeyItems*>::iterator iter = tickets.begin(); iter != tickets.end(); iter++) {
		delete iter->second;
	}
	tickets.clear();
	unlock_map_tickets();
}

cSslDsslSessionKeys::eSessionKeyType cSslDsslSessionKeys::strToEnumType(const char *type) {
	for(unsigned i = 0; session_key_types[i].str; i++) {
		if(!strcasecmp(session_key_types[i].str, type)) {
			return(session_key_types[i].type);
		}
	}
	return(_skt_na);
}

const char *cSslDsslSessionKeys::enumToStrType(eSessionKeyType type) {
	for(unsigned i = 0; session_key_types[i].str; i++) {
		if(session_key_types[i].type ==  type) {
			return(session_key_types[i].str);
		}
	}
	return("");
}

void cSslDsslSessionKeys::setKeysToTicket(u_char *ticket_data, u_int32_t ticket_data_len, u_int32_t ts_s, cSslDsslSessionKeyItems *key_items) {
	if(!ticket_data || !ticket_data_len || !key_items) {
		return;
	}
	cSslDsslSessionTicket ticket(ticket_data, ticket_data_len, ts_s, false);
	if(ticket.ticket) {
		lock_map_tickets();
		if(tickets.find(ticket) != tickets.end() && tickets[ticket]) {
			delete tickets[ticket];
		}
		tickets[ticket] = key_items->clone();
		unlock_map_tickets();
	}
}

cSslDsslSessions::cSslDsslSessions() {
	_sync_sessions = 0;
	_sync_sessions_db = 0;
	sqlDb = NULL;
	last_delete_old_sessions_at = 0;
	exists_sessions_table = false;
	loadSessions();
	init();
}

cSslDsslSessions::~cSslDsslSessions() {
	if(sqlDb) {
		delete sqlDb;
	}
	term();
}

void cSslDsslSessions::processData(vector<string> *rslt_decrypt, char *data, unsigned int datalen, vmIP saddr, vmIP daddr, vmPort sport, vmPort dport, timeval ts,
				   bool forceTryIfExistsError) {
	/*
	if(!(sport == 50404 || dport == 50404)) {
		return;
	}
	if(ts.tv_sec < 1533040717) {
		return;
	}
	if(getTimeUS(ts) < 1487014991237727ull) {
		return;
	}
	*/
	lock_sessions();
	NM_PacketDir dir = checkIpPort(saddr, sport, daddr, dport);
	if(dir == ePacketDirInvalid) {
		rslt_decrypt->clear();
		unlock_sessions();
		return;
	}
	if(sverb.ssl_stats) {
		stats.delay_processData_begin.add_delay_from_act(getTimeUS(ts));
	}
	vmIP server_addr, client_addr;
	vmPort server_port, client_port;
	server_addr = dir == ePacketDirFromClient ? daddr : saddr;
	server_port = dir == ePacketDirFromClient ? dport : sport;
	client_addr = dir == ePacketDirFromClient ? saddr : daddr;
	client_port = dir == ePacketDirFromClient ? sport : dport;
	cSslDsslSession *session = NULL;
	sStreamId sid(server_addr, server_port, client_addr, client_port);
	map<sStreamId, cSslDsslSession*>::iterator iter_session;
	iter_session = sessions.find(sid);
	if(iter_session != sessions.end()) {
		session = iter_session->second;
	}
	bool init_client_hello = false;
	bool init_store_session = false;
	if(!session && dir == ePacketDirFromClient) {
		NM_ERROR_DISABLE_LOG;
		uint16_t ver = 0;
		if(!ssl_detect_client_hello_version((u_char*)data, datalen, &ver) && ver) {
			init_client_hello = true;
		}
		NM_ERROR_ENABLE_LOG;
		if(init_client_hello) {
			string keyfile = sslIpPort_get_keyfile(client_addr, client_port, server_addr, server_port);
			session = addSession(server_addr, server_port, keyfile);
			session->setClientIpPort(client_addr, client_port);
			sessions[sid] = session;
			lock_sessions_db();
			if(sessions_db.find(sid) != sessions_db.end()) {
				sessions_db.erase(sid);
			}
			unlock_sessions_db();
		}
	}
	if(!session) {
		sSessionData session_data;
		lock_sessions_db();
		map<sStreamId, sSessionData>::iterator iter_session_db = sessions_db.find(sid);
		if(iter_session_db != sessions_db.end()) {
			session_data = iter_session_db->second;
		}
		unlock_sessions_db();
		if(!session_data.data.empty()) {
			string keyfile = sslIpPort_get_keyfile(client_addr, client_port, server_addr, server_port);
			session = addSession(server_addr, server_port, keyfile);
			session->setClientIpPort(client_addr, client_port);
			if(session->restore_session_data(session_data.data.c_str())) {
				/*
				cout << "S: " << sid.s.ip.getString() << ":" << sid.s.port.getString() << "  "
				     << "C: " << sid.c.ip.getString() << ":" << sid.c.port.getString() << endl;
				*/
				sessions[sid] = session;
				init_store_session = true;
				lock_sessions_db();
				sessions_db.erase(sid);
				unlock_sessions_db();
			} else {
				delete session;
				session = NULL;
			}
		}
	}
	if(session) {
		session->processData(rslt_decrypt, data, datalen, 
				     saddr, daddr, sport, dport, 
				     ts, init_client_hello || init_store_session, this,
				     forceTryIfExistsError);
	}
	if(sverb.ssl_stats) {
		stats.delay_processData_end.add_delay_from_act(getTimeUS(ts));
	}
	unlock_sessions();
}

void cSslDsslSessions::destroySession(vmIP saddr, vmIP daddr, vmPort sport, vmPort dport) {
	lock_sessions();
	NM_PacketDir dir = checkIpPort(saddr, sport, daddr, dport);
	if(dir == ePacketDirInvalid) {
		unlock_sessions();
		return;
	}
	sStreamId sid(dir == ePacketDirFromClient ? daddr : saddr,
		      dir == ePacketDirFromClient ? dport : sport,
		      dir == ePacketDirFromClient ? saddr : daddr,
		      dir == ePacketDirFromClient ? sport : dport);
	map<sStreamId, cSslDsslSession*>::iterator iter_session;
	iter_session = sessions.find(sid);
	if(iter_session != sessions.end()) {
		if(iter_session->second->session) {
			if(iter_session->second->session->tls_session_server_seq != iter_session->second->session->tls_session_server_seq_saved ||
			   iter_session->second->session->tls_session_client_seq != iter_session->second->session->tls_session_client_seq_saved) {
				timeval ts = getTimeval();
				iter_session->second->store_session(this, ts, cSslDsslSessions::_tss_force_all);
			} else {
				timeval ts = getTimeval();
				iter_session->second->store_session(this, ts, cSslDsslSessions::_tss_force | cSslDsslSessions::_tss_mem);
			}
			if(iter_session->second->get_keys_ok) {
				keyErase(iter_session->second->session->client_random);
			}
		}
		delete iter_session->second;
		sessions.erase(iter_session);
	}
	unlock_sessions();
}

bool cSslDsslSessions::keySet(const char *data, unsigned data_length) {
	string type;
	string client_random;
	string key;
	unsigned offset = 0;
	for(unsigned i = 0; cSslDsslSessionKeys::session_key_types[i].str; i++) {
		char *type_begin = strncasestr((char*)data, cSslDsslSessionKeys::session_key_types[i].str, data_length);
		if(type_begin) {
			type = cSslDsslSessionKeys::session_key_types[i].str;
			offset = cSslDsslSessionKeys::session_key_types[i].length + (type_begin - data);
			break;
		}
	}
	if(offset && offset < data_length - 1 && data[offset] == ' ') {
		++offset;
		while(offset < data_length && isalnum(data[offset])) {
			client_random += data[offset];
			++offset;
		}
		if(client_random.length() == SSL3_RANDOM_SIZE * 2 && data[offset] == ' ') {
			++offset;
			while(offset < data_length && isalnum(data[offset])) {
				key += data[offset];
				++offset;
			}
			if(key.length() == SSL3_MASTER_SECRET_SIZE * 2 || key.length() == 32 * 2) {
				return(keySet(type.c_str(), client_random.c_str(), key.c_str()));
			}
		}
	}
	return(false);
}

bool cSslDsslSessions::keySet(const char *type, const char *client_random, const char *key) {
	unsigned client_random_length = strlen(client_random);
	unsigned key_length = strlen(key);
	if(client_random_length == SSL3_RANDOM_SIZE * 2 && 
	   (key_length == SSL3_MASTER_SECRET_SIZE * 2 || key_length == 32 * 2)) {
		u_char client_random_bin[SSL3_RANDOM_SIZE];
		u_char key_bin[SSL3_MASTER_SECRET_SIZE];
		unsigned key_bin_length = key_length / 2;
		hexdecode(client_random_bin, client_random, SSL3_RANDOM_SIZE);
		hexdecode(key_bin, key, key_length);
		SslDsslSessions->keySet(type, client_random_bin, key_bin, key_bin_length);
		if(ssl_sessionkey_enable()) {
			string log_ssl_sessionkey =
				string("set clientrandom with type ") + type +
				"; clientrandom: " + hexdump_to_string(client_random_bin, SSL3_RANDOM_SIZE) +
				"; key: " + hexdump_to_string(key_bin, key_bin_length);
			ssl_sessionkey_log(log_ssl_sessionkey);
		}
		return(true);
	}
	return(false);
}

void cSslDsslSessions::keySet(const char *type, u_char *client_random, u_char *key, unsigned key_length) {
	this->session_keys.set(type, client_random, key, key_length);
}

bool cSslDsslSessions::keyGet(u_char *client_random, cSslDsslSessionKeys::eSessionKeyType type, u_char *key, unsigned *key_length, timeval ts, bool use_wait) {
	return(this->session_keys.get(client_random, type, key, key_length, ts, use_wait));
}

bool cSslDsslSessions::keysGet(u_char *client_random, u_char *ticket, u_int32_t ticket_len,
			       DSSL_Session_get_keys_data *get_keys_data, timeval ts, bool use_wait,
			       cSslDsslSessionKeys::cSslDsslSessionKeyItems **key_items_clone) {
	return(this->session_keys.get(client_random, ticket, ticket_len, get_keys_data, ts, use_wait, key_items_clone));
}

void cSslDsslSessions::keyErase(u_char *client_random) {
	extern bool ssl_client_random_keep;
	if(!ssl_client_random_keep) {
		this->session_keys.erase(client_random);
	}
}

void cSslDsslSessions::keysCleanup() {
	this->session_keys.cleanup();
}

void cSslDsslSessions::setKeysToTicket(u_char *ticket_data, u_int32_t ticket_data_len, u_int32_t ts_s, cSslDsslSessionKeys::cSslDsslSessionKeyItems *key_items) {
	this->session_keys.setKeysToTicket(ticket_data, ticket_data_len, ts_s, key_items);
}

void cSslDsslSessions::storeSessions() {
	if(opt_ssl_store_sessions && !opt_nocdr) {
		timeval ts = getTimeval();
		for(map<sStreamId, cSslDsslSession*>::iterator iter = sessions.begin(); iter != sessions.end(); iter++) {
			if(iter->second->session->tls_session_server_seq != iter->second->session->tls_session_server_seq_saved ||
			   iter->second->session->tls_session_client_seq != iter->second->session->tls_session_client_seq_saved) {
				iter->second->store_session(this, ts, cSslDsslSessions::_tss_force | cSslDsslSessions::_tss_db);
			}
		}
	}
}

cSslDsslSession *cSslDsslSessions::addSession(vmIP ips, vmPort ports, string keyfile) {
	cSslDsslSession *session = new FILE_LINE(0) cSslDsslSession(ips, ports, keyfile);
	return(session);
}

NM_PacketDir cSslDsslSessions::checkIpPort(vmIP sip, vmPort sport, vmIP dip, vmPort dport) {
	int rslt = isSslIpPort(sip, sport, dip, dport);
	return(rslt == 1 ? ePacketDirFromClient :
	       rslt == 2 ? ePacketDirFromServer :
			   ePacketDirInvalid);
}

void cSslDsslSessions::init() {
	SSL_library_init();	
	OpenSSL_add_all_ciphers();
	OpenSSL_add_all_digests();
}

void cSslDsslSessions::term() {
	map<sStreamId, cSslDsslSession*>::iterator iter_session;
	for(iter_session = sessions.begin(); iter_session != sessions.end();) {
		delete iter_session->second;
		sessions.erase(iter_session++);
	}
}

void cSslDsslSessions::loadSessions() {
	if(!opt_ssl_store_sessions || opt_nocdr) {
		return;
	}
	if(!sqlDb) {
		sqlDb = createSqlObject();
	}
	syslog(LOG_NOTICE, "try load ssl/tls sessions from table %s", storeSessionsTableName().c_str());
	exists_sessions_table = sqlDb->existsTable(storeSessionsTableName());
	if(!exists_sessions_table) {
		syslog(LOG_NOTICE, "sessions table %s is missing", storeSessionsTableName().c_str());
		return;
	}
	list<SqlDb_condField> cond;
	cond.push_back(SqlDb_condField("id_sensor", intToString(existsColumns.ssl_sessions_id_sensor_is_unsigned && opt_id_sensor < 0 ? 0 : opt_id_sensor)));
	cond.push_back(SqlDb_condField("stored_at", sqlDateTimeString(getTimeS() - opt_ssl_store_sessions_expiration_hours * 3600)).setOper(">"));
	sqlDb->select(storeSessionsTableName(), NULL, &cond);
	SqlDb_row row;
	unsigned session_rows = 0;
	while((row = sqlDb->fetchRow())) {
		sStreamId sid(mysql_ip_2_vmIP(&row, "serverip"), atoi(row["serverport"].c_str()), 
			      mysql_ip_2_vmIP(&row, "clientip"), atoi(row["clientport"].c_str()));
		sSessionData session_data;
		session_data.data = row["session"];
		lock_sessions_db();
		sessions_db[sid] = session_data;
		unlock_sessions_db();
		++session_rows;
	}
	if(session_rows) {
		syslog(LOG_NOTICE, "load %u current ssl/tls sessions", session_rows);
	} else {
		syslog(LOG_NOTICE, "there are no current ssl/tls sessions");
	}
}

void cSslDsslSessions::setSessionData(vmIP sip, vmPort sport, vmIP cip, vmPort cport, const char *session_data) {
	sStreamId sid(sip, sport, cip, cport);
	sSessionData sessionData;
	sessionData.data = session_data;
	lock_sessions_db();
	sessions_db[sid] = sessionData;
	unlock_sessions_db();
}

void cSslDsslSessions::deleteOldSessions(timeval ts) {
	if(!opt_ssl_store_sessions || opt_nocdr || !exists_sessions_table) {
		return;
	}
	if(!last_delete_old_sessions_at || last_delete_old_sessions_at < (u_long)(ts.tv_sec - 3600)) {
		if(!sqlDb) {
			sqlDb = createSqlObject();
		}
		list<SqlDb_condField> cond;
		cond.push_back(SqlDb_condField("id_sensor", intToString(opt_id_sensor)));
		cond.push_back(SqlDb_condField("stored_at", sqlDateTimeString(ts.tv_sec - opt_ssl_store_sessions_expiration_hours * 3600)).setOper("<"));
		sqlStore->query_lock(MYSQL_ADD_QUERY_END(
				     "delete from " + storeSessionsTableName() + " where " + sqlDb->getCondStr(&cond)),
				     STORE_PROC_ID_OTHER, 0);
		last_delete_old_sessions_at = ts.tv_sec;
	}
}

string cSslDsslSessions::storeSessionsTableName() {
	return(opt_ssl_store_sessions == 1 ? "ssl_sessions_mem" :
	       opt_ssl_store_sessions == 2 ? "ssl_sessions" : "");
}


cClientRandomServer::cClientRandomServer() {
}

cClientRandomServer::~cClientRandomServer() {
}

void cClientRandomServer::createConnection(cSocket *socket) {
	if(is_terminating()) {
		return;
	}
	cClientRandomConnection *connection = new FILE_LINE(0) cClientRandomConnection(socket);
	connection->connection_start();
}

cClientRandomConnection::cClientRandomConnection(cSocket *socket)
: cServerConnection(socket) {
}

cClientRandomConnection::~cClientRandomConnection() {
}

void cClientRandomConnection::connection_process() {
	socket->setBlockHeaderString("ssl_key_socket_block");
	string rsltRsaKey;
	if(!socket->readBlock(&rsltRsaKey) || rsltRsaKey.find("key") == string::npos) {
		socket->setError("failed read rsa key");
		delete this;
		return;
	}
	JsonItem jsonRsaKey;
	jsonRsaKey.parse(rsltRsaKey);
	string rsa_key = jsonRsaKey.getValue("rsa_key");
	socket->set_rsa_pub_key(rsa_key);
	socket->generate_aes_keys();
	JsonExport json_keys;
	string aes_ckey, aes_ivec;
	socket->get_aes_keys(&aes_ckey, &aes_ivec);
	json_keys.add("aes_ckey", aes_ckey);
	json_keys.add("aes_ivec", aes_ivec);
	if(!socket->writeBlock(json_keys.getJson(), cSocket::_te_rsa)) {
		socket->setError("failed send aes keys");
		delete this;
		return;
	}
	string rsltOk;
	if(!socket->readBlock(&rsltOk, cSocket::_te_aes) || rsltOk != "OK") {
		socket->setError("failed read ok");
		delete this;
		return;
	}
	while(!is_terminating() && !socket->isError()) {
		u_char *data;
		size_t dataLen;
		data = socket->readBlock(&dataLen, cSocket::_te_aes);
		if(data) {
			evData(data, dataLen);
		} else {
			USLEEP(1000);
		}
	}
	delete this;
}

void cClientRandomConnection::evData(u_char *data, size_t dataLen) {
	ssl_parse_client_random(data, dataLen);
}

string sSslDsslStatsItem_time::str() {
	ostringstream str;
	str << "c" << count << "/"
	    << "m" << max_ms <<  "/"
	    << "a" << avg_ms();
	return(str.str());
}

string sSslDsslStats::str() {
	ostringstream str;
	str << "SSL_STATS "
	    << "delay_processData_begin: " << delay_processData_begin.str() << "; "
	    << "delay_processData_end: " << delay_processData_end.str() << "; "
	    << "delay_keys_get_begin: " << delay_keys_get_begin.str() << "; "
	    << "delay_keys_get_end: " << delay_keys_get_end.str() << "; "
	    << "delay_processPacket: " << delay_processPacket.str() << "; "
	    << "delay_parseSdp: " << delay_parseSdp.str();
	return(str.str());
}


#endif //HAVE_OPENSSL101 && HAVE_LIBGNUTLS


void ssl_dssl_init() {
	#if defined(HAVE_OPENSSL101) and defined(HAVE_LIBGNUTLS)
	SslDsslSessions = new FILE_LINE(0) cSslDsslSessions;
	SslDsslEnv = DSSL_EnvCreate(10000 /*sessionTableSize*/, 4 * 60 * 60 /*key_timeout_interval*/);
	extern bool init_lib_gcrypt();
	init_lib_gcrypt();
	#endif //HAVE_OPENSSL101 && HAVE_LIBGNUTLS
}

void ssl_dssl_clean() {
	#if defined(HAVE_OPENSSL101) and defined(HAVE_LIBGNUTLS)
	if(SslDsslSessions) {
		SslDsslSessions->storeSessions();
		delete SslDsslSessions;
		SslDsslSessions = NULL;
	}
	if(SslDsslEnv) {
		DSSL_EnvDestroy(SslDsslEnv);
		SslDsslEnv = NULL;
	}
	#endif //HAVE_OPENSSL101 && HAVE_LIBGNUTLS
}


void decrypt_ssl_dssl(vector<string> *rslt_decrypt, char *data, unsigned int datalen, vmIP saddr, vmIP daddr, vmPort sport, vmPort dport, timeval ts,
		      bool forceTryIfExistsError) {
	#if defined(HAVE_OPENSSL101) and defined(HAVE_LIBGNUTLS)
	SslDsslSessions->processData(rslt_decrypt, data, datalen, saddr, daddr, sport, dport, ts,
				     forceTryIfExistsError);
	#endif //HAVE_OPENSSL101 && HAVE_LIBGNUTLS
}

void end_decrypt_ssl_dssl(vmIP saddr, vmIP daddr, vmPort sport, vmPort dport) {
	#if defined(HAVE_OPENSSL101) and defined(HAVE_LIBGNUTLS)
	SslDsslSessions->destroySession(saddr, daddr, sport, dport);
	SslDsslSessions->keysCleanup();
	#endif //HAVE_OPENSSL101 && HAVE_LIBGNUTLS
}

bool ssl_parse_client_random(u_char *data, unsigned datalen) {
	#if defined(HAVE_OPENSSL101) and defined(HAVE_LIBGNUTLS)
	if(!SslDsslSessions) {
		return(false);
	}
	string data_s((char*)data, datalen);
	if(isJsonObject(data_s)) {
		string type;
		string client_random;
		string key;
		JsonItem jsonData;
		jsonData.parse(data_s.c_str());
		if(jsonData.getItem("sessionid") && jsonData.getItem("mastersecret")) {
			type = "client_random";
			client_random = jsonData.getValue("sessionid");
			key = jsonData.getValue("mastersecret");
		} else if(jsonData.getItem("type") && jsonData.getItem("client_random") && jsonData.getItem("key")) {
			type = jsonData.getValue("type");
			client_random = jsonData.getValue("client_random");
			key = jsonData.getValue("key");
		}
		return(SslDsslSessions->keySet(type.c_str(), client_random.c_str(), key.c_str()));
	} else {
		return(SslDsslSessions->keySet((const char*)data, datalen));
	}
	#endif //HAVE_OPENSSL101 && HAVE_LIBGNUTLS
	return(false);
}

void ssl_parse_client_random(const char *fileName) {
	#if defined(HAVE_OPENSSL101) and defined(HAVE_LIBGNUTLS)
	if(!SslDsslSessions) {
		return;
	}
	FILE *file = fopen(fileName, "r");
	if(!file) {
		return;
	}
	char buff[1024];
	while(fgets(buff, sizeof(buff), file)) {
		unsigned length = strlen(buff);
		while(length > 0 && (buff[length - 1] == '\n' || buff[length - 1] == '\r')) {
			buff[length - 1] = 0;
			--length;
		}
		SslDsslSessions->keySet(buff, length);
	}
	fclose(file);
	#endif //HAVE_OPENSSL101 && HAVE_LIBGNUTLS
}


#if defined(HAVE_OPENSSL101) and defined(HAVE_LIBGNUTLS)
void jsonAddKey(JsonExport *json, const char *name, DSSL_Session_get_keys_data_item *key) {
	json->add(name, hexencode(key->key, key->length));
}

void jsonGetKey(JsonItem *json, const char *name, DSSL_Session_get_keys_data_item *key) {
	string key_str = json->getValue(name);
	if(!key_str.empty()) {
		hexdecode(key->key, key_str.c_str(), key_str.length());
		key->length = key_str.length() / 2;
	}
}
#endif //HAVE_OPENSSL101 && HAVE_LIBGNUTLS


#if defined(HAVE_OPENSSL101) and defined(HAVE_LIBGNUTLS)
static cClientRandomServer *clientRandomServer;
#endif //HAVE_OPENSSL101 && HAVE_LIBGNUTLS

void clientRandomServerStart(const char *host, int port) {
	#if defined(HAVE_OPENSSL101) and defined(HAVE_LIBGNUTLS)
	if(clientRandomServer) {
		delete clientRandomServer;
	}
	clientRandomServer =  new FILE_LINE(0) cClientRandomServer;
	clientRandomServer->setStartVerbString("START SSL_SESSIONKEY LISTEN");
	clientRandomServer->listen_start("client_random_server", host, port);
	#endif //HAVE_OPENSSL101 && HAVE_LIBGNUTLS
}

void clientRandomServerStop() {
	#if defined(HAVE_OPENSSL101) and defined(HAVE_LIBGNUTLS)
	if(clientRandomServer) {
		delete clientRandomServer;
		clientRandomServer = NULL;
	}
	#endif //HAVE_OPENSSL101 && HAVE_LIBGNUTLS
}

bool find_master_secret(u_char *client_random, u_char *key, unsigned *key_length) {
	#if defined(HAVE_OPENSSL101) and defined(HAVE_LIBGNUTLS)
	if(!SslDsslSessions) {
		return(false);
	}
	timeval ts;
	ts.tv_sec = 0;
	ts.tv_usec = 0;
	unsigned _key_length;
	bool rslt = SslDsslSessions->keyGet(client_random, cSslDsslSessionKeys::_skt_client_random, key, &_key_length, ts, false);
	if(rslt) {
		*key_length = _key_length;
	}
	return(rslt);
	#else
	return(false);
	#endif
}

void erase_client_random(u_char *client_random) {
	#if defined(HAVE_OPENSSL101) and defined(HAVE_LIBGNUTLS)
	if(SslDsslSessions) {
		SslDsslSessions->keyErase(client_random);
	}
	#endif
}

string ssl_stats_str() {
	#if defined(HAVE_OPENSSL101) and defined(HAVE_LIBGNUTLS)
	if(sverb.ssl_stats) {
		return(stats.str());
	}
	#endif
	return("");
}

void ssl_stats_reset() {
	#if defined(HAVE_OPENSSL101) and defined(HAVE_LIBGNUTLS)
	if(sverb.ssl_stats) {
		stats.reset();
	}
	#endif
}

void ssl_stats_add_delay_processPacket(u_int64_t time_us) {
	#if defined(HAVE_OPENSSL101) and defined(HAVE_LIBGNUTLS)
	if(sverb.ssl_stats) {
		stats.delay_processPacket.add_delay_from_act(time_us);
	}
	#endif
}

void ssl_stats_add_delay_parseSdp(u_int64_t time_us) {
	#if defined(HAVE_OPENSSL101) and defined(HAVE_LIBGNUTLS)
	if(sverb.ssl_stats) {
		stats.delay_parseSdp.add_delay_from_act(time_us);
	}
	#endif
}

bool ssl_sessionkey_enable() {
	return(sverb.ssl_sessionkey || sverb.ssl_sessionkey_to_file);
}

void ssl_sessionkey_log(string &str) {
	static FILE *log_file = NULL;
	static volatile int log_sync = 0;
	__SYNC_LOCK(log_sync);
	if(sverb.ssl_sessionkey) {
		syslog(LOG_NOTICE, "%s", str.c_str());
	}
	if(sverb.ssl_sessionkey_to_file) {
		if(!log_file) {
			log_file = fopen(sverb.ssl_sessionkey_to_file, "a");
		}
		if(log_file) {
			fprintf(log_file, "%s: ", sqlDateTimeString(getTimeS_rdtsc()).c_str());
			fwrite(str.c_str(), str.length(), 1, log_file);
			fwrite("\n", 1, 1, log_file);
			fflush(log_file);
		}
	}
	__SYNC_UNLOCK(log_sync);
}
