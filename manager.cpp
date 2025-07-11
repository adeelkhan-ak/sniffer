#include "config.h"
#include <iostream>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <poll.h>
#include <unistd.h>
#include <string>
#include <fcntl.h>
#include <errno.h>
#include <syslog.h>
#include <netdb.h>
#include <resolv.h>
#include <vorbis/codec.h>
#include <vorbis/vorbisenc.h>
#include <pcap.h>
#include <math.h>
#include <time.h>
#ifdef HAVE_OPENSSL
#include <openssl/crypto.h>  
#endif
#include <sstream>

#include "ipaccount.h"
#include "voipmonitor.h"
#include "calltable.h"
#include "sniff.h"
#include "codec_alaw.h"
#include "codec_ulaw.h"
#include "tools.h"
#include "calltable.h"
#include "cleanspool.h"
#include "pcap_queue.h"
#include "manager.h"
#include "country_detect.h"
#include "fraud.h"
#include "billing.h"
#include "rrd.h"
#include "tar.h"
#include "http.h"
#include "send_call_info.h"
#include "config_param.h"
#include "sniff_proc_class.h"
#include "register.h"
#include "options.h"
#include "server.h"
#include "filter_mysql.h"
#include "charts.h"
#include "diameter.h"
#include "ssldata.h"
#include "audio_convert.h"

#ifndef FREEBSD
#include <malloc.h>
#endif

#if HAVE_LIBTCMALLOC
#include <gperftools/malloc_extension.h>
#endif
#if HAVE_LIBTCMALLOC_HEAPPROF
#include <gperftools/heap-profiler.h>
#endif

//#define BUFSIZE 1024
//define BUFSIZE 20480
#define BUFSIZE 4096		//block size?

void init_management_functions(void);

extern Calltable *calltable;
extern volatile int terminating;
extern int opt_manager_port;
extern char opt_manager_ip[32];
extern string opt_manager_socket;
extern string opt_manager_socket_run_via_manager;
extern int opt_manager_nonblock_mode;
extern volatile int calls_counter;
extern volatile int registers_counter;
extern char mac[32];
extern int verbosity;
extern char opt_php_path[1024];
extern pthread_t manager_file_thread;
extern bool manager_file_terminating;
extern int manager_socket_server;
extern int manager_socket_file_server;
extern bool manager_socket_file_server_terminating;
extern int opt_nocdr;
extern int global_livesniffer;
extern map<unsigned int, octects_live_t*> ipacc_live;
extern int opt_t2_boost;

extern map<unsigned int, livesnifferfilter_s*> usersniffer;
extern map<unsigned int, string> usersniffer_kill_reason;
extern volatile int usersniffer_sync;

extern char ssh_host[1024];
extern int ssh_port;
extern char ssh_username[256];
extern char ssh_password[256];
extern char ssh_remote_listenhost[1024];
extern unsigned int ssh_remote_listenport;
extern int enable_bad_packet_order_warning;
extern bool opt_socket_use_poll;

extern cConfig CONFIG;
extern volatile bool cloud_activecheck_sshclose;

extern char opt_call_id_alternative[256];
extern string binaryNameWithPath;

extern int opt_tar_move;
extern string opt_tar_move_destination_path;

int opt_blocktarwrite = 0;
int opt_blockasyncprocess = 0;
int opt_blockprocesspacket = 0;
int opt_blockcleanupcalls = 0;
int opt_sleepprocesspacket = 0;
int opt_blockqfile = 0;
int opt_block_alloc_stack = 0;
int opt_disable_wait_for_ssl_key = 0;

using namespace std;

struct sMgmtCmdsReg {
	int (*mgmtFce)(Mgmt_params *params);
	string command;
	string help;
	int notNeedAes;
};
map<string, sMgmtCmdsReg> MgmtCmdsRegTable;

int Mgmt_params::registerCommand(const char *cmd, const char *help, int notNeedAes) {
	sMgmtCmdsReg cmd_reg;
	cmd_reg.mgmtFce = this->mgmtFce;
	cmd_reg.command = cmd;
	if(help && *help) {
		cmd_reg.help = help;
	}
	cmd_reg.notNeedAes = notNeedAes;
	MgmtCmdsRegTable[cmd] = cmd_reg;
	return(0);
}

int Mgmt_params::registerCommand(commandAndHelp *cmdHelp) {
	while (cmdHelp->command) {
		registerCommand(cmdHelp->command, cmdHelp->help, cmdHelp->notNeedAes);
		cmdHelp++;
	}
	return(0);
}

int Mgmt_params::sendString(const char *str) {
	string tstr = str;
	return(sendString(&tstr));
}

int Mgmt_params::sendString(const char *str, ssize_t size) {
	if(_send(str, size) == -1) {
		cerr << "Error sending data to client" << endl;
		return -1;
	}
	return(0);
}

int Mgmt_params::sendString(ostringstream *str) {
	string tstr = str->str();
	return(sendString(&tstr));
}

int Mgmt_params::sendString(int value) {
	std::stringstream s;
	s << value;
	string tstr = s.str();
	return(sendString(&tstr));
}

int Mgmt_params::sendString(string str) {
	return(sendString(&str));
}

int Mgmt_params::sendString(string *str) {
	if(str->empty()) {
		return(0);
	}
	CompressStream *compressStream = NULL;
	if(zip &&
	   ((*str)[0] != 0x1f || (str->length() > 1 && (unsigned char)(*str)[1] != 0x8b))) {
		compressStream = new FILE_LINE(13021) CompressStream(CompressStream::gzip, 1024, 0);
		compressStream->setSendParameters(this);
	}
	unsigned chunkLength = 4096;
	unsigned processedLength = 0;
	while(processedLength < str->length()) {
		unsigned processLength = MIN(chunkLength, str->length() - processedLength);
		if(compressStream) {
			compressStream->compress((char*)str->c_str() + processedLength, processLength, false, compressStream);
			if(compressStream->isError()) {
				cerr << "Error compress stream" << endl;
			return -1;
			}
		} else {
			if(_send((char*)str->c_str() + processedLength, processLength) == -1) {
				cerr << "Error sending data to client" << endl;
				return -1;
			}
		}
		processedLength += processLength;
	}
	if(compressStream) {
		compressStream->compress(NULL, 0, true, compressStream);
		delete compressStream;
	}
	return(0);
}

int Mgmt_params::sendFile(const char *fileName, u_int64_t tailMaxSize) {
	int fd = open(fileName, O_RDONLY);
	if(fd < 0) {
		string str = "error: cannot open file " + string(fileName);
		sendString(&str);
		return -1;
	}
	u_int64_t startPos = 0;
	if(tailMaxSize) {
		u_int64_t fileSize = GetFileSize(fileName);
		if(fileSize > tailMaxSize) {
			startPos = fileSize - tailMaxSize;
		}
	}
	if(startPos) {
		lseek(fd, startPos);
	}
	RecompressStream *recompressStream = new FILE_LINE(0) RecompressStream(RecompressStream::compress_na, zip ? RecompressStream::gzip : RecompressStream::compress_na);
	recompressStream->setSendParameters(this);
	ssize_t nread;
	size_t read_size = 0;
	char rbuf[4096];
	while(nread = read(fd, rbuf, sizeof(rbuf)), nread > 0) {
		if(!read_size) {
			if(nread >= 2 &&
			   (unsigned char)rbuf[0] == 0x1f &&
			   (unsigned char)rbuf[1] == 0x8b) {
				if(zip) {
					recompressStream->setTypeCompress(RecompressStream::compress_na);
					recompressStream->setTypeDecompress(RecompressStream::compress_na);
				}
			} else if(nread >= 3 &&
				  rbuf[0] == 'L' && rbuf[1] == 'Z' && rbuf[2] == 'O') {
				recompressStream->setTypeDecompress(RecompressStream::lzo, true);
			}
		}
		read_size += nread;
		recompressStream->processData(rbuf, nread);
		if(recompressStream->isError()) {
			close(fd);
			return -1;
		}
	}
	close(fd);
	delete recompressStream;
	return(0);
}

int Mgmt_params::sendConfigurationFile(const char *fileName, list<string> *hidePasswordForOptions) {
	FILE *file = fopen(fileName, "r");
	if(!file) {
		string str = "error: cannot open file " + string(fileName);
		sendString(&str);
		return -1;
	}
	RecompressStream *recompressStream = new FILE_LINE(0) RecompressStream(RecompressStream::compress_na, zip ? RecompressStream::gzip : RecompressStream::compress_na);
	recompressStream->setSendParameters(this);
	char lineBuffer[10000];
	while(fgets(lineBuffer, sizeof(lineBuffer), file)) {
		string lineBufferSubst;
		if(hidePasswordForOptions) {
			char *optionSeparatorPos = strchr(lineBuffer, '=');
			if(optionSeparatorPos) {
				string option = trim_str(string(lineBuffer, optionSeparatorPos - lineBuffer));
				string value = trim_str(string(optionSeparatorPos + 1));
				for(list<string>::iterator iter = hidePasswordForOptions->begin(); iter != hidePasswordForOptions->end(); iter++) {
					if(option == *iter) {
						lineBufferSubst = option + " = ****\n";
						break;
					}
				}
			}
		}
		if(lineBufferSubst.empty()) {
			recompressStream->processData(lineBuffer, strlen(lineBuffer));
		} else {
			recompressStream->processData((char*)lineBufferSubst.c_str(), lineBufferSubst.length());
		}
		if(recompressStream->isError()) {
			fclose(file);
			return -1;
		}
	}
	fclose(file);
	delete recompressStream;
	return(0);
}

int Mgmt_params::sendPexecOutput(const char *cmd) {
	int exitCode;
	string result_out;
	string result_err;
        #if true
		SimpleBuffer out;
		SimpleBuffer err;
		vm_pexec(cmd, &out, &err, &exitCode);
		result_out = (char*)out;
		result_err = (char*)err;
	#else
		result_out = pexec((char*)cmd, &exitCode);
		if(!result_out.empty()) {
			exitCide = 0;
		}
	#endif
	if(exitCode == 0 && !result_out.empty()) {
		return(sendString(result_out));
	} else {
		string failed_str = string("failed ") + cmd;
		if(result_err.size()) {
			if(result_err[result_err.length() - 1] == '\n') {
				result_err.resize(result_err.length() - 1);
			}
			failed_str += result_err;
		}
		return(sendString(failed_str));
	}
}

int Mgmt_params::_send(const char *buf, ssize_t len) {
	int res = 0;
	if(c_client) {
		extern cCR_Receiver_service *cloud_receiver;
		extern cSnifferClientService *snifferClientService;
		string aes_ckey, aes_ivec;
		if(cloud_receiver) {
			cloud_receiver->get_aes_keys(&aes_ckey, &aes_ivec);
		} else if(snifferClientService) {
			snifferClientService->get_aes_keys(&aes_ckey, &aes_ivec);
		}
		res = c_client->writeAesEnc((u_char*)buf, len, aes_ckey.c_str(), aes_ivec.c_str()) ? 0: -1;
	} else {
		if(aes_key.isSetKeys()) {
			if(!aes) {
				aes = new FILE_LINE(0) cAes();
				aes->setKeys(&aes_key);
				aes->setCipher(aes_cipher.c_str());
				send(client.handler, "aes:", 4, 0);
			}
			u_char *data_enc;
			size_t datalen_enc;
			if(aes->encrypt((u_char*)buf, len, &data_enc, &datalen_enc, false)) {
				res = send(client.handler, data_enc, datalen_enc, 0);
				delete [] data_enc;
			}
		} else {
			res = send(client.handler, buf, len, 0);
		}
	}
	return res;
}

Mgmt_params::Mgmt_params(char *ibuf, int isize, sClientInfo iclient, cClient *ic_client, cAesKey *aes_key, const char *aes_cipher, ManagerClientThread **imanagerClientThread) {
	buf = ibuf;
	size = isize;
	client = iclient;
	c_client = ic_client;
	managerClientThread = imanagerClientThread;
	mgmtFce = NULL;
	zip = false;
	task = mgmt_task_na;
	if(aes_key) {
		this->aes_key = *aes_key;
	}
	if(aes_cipher) {
		this->aes_cipher = aes_cipher;
	}
	aes = NULL;
}

Mgmt_params::~Mgmt_params() {
	if(aes) {
		u_char *data_enc;
		size_t datalen_enc;
		if(aes->encrypt((u_char*)"", 0, &data_enc, &datalen_enc, true)) {
			send(client.handler, data_enc, datalen_enc, 0);
			delete [] data_enc;
		}
		delete aes;
	}
}

int Mgmt_help(Mgmt_params *params);
int Mgmt_getversion(Mgmt_params *params);
int Mgmt_listcalls(Mgmt_params *params);
int Mgmt_reindexfiles(Mgmt_params *params);
int Mgmt_offon(Mgmt_params *params);
int Mgmt_check_filesindex(Mgmt_params *params);
int Mgmt_reindexspool(Mgmt_params *params);
int Mgmt_printspool(Mgmt_params *params);
int Mgmt_listopentars(Mgmt_params *params);
int Mgmt_totalcalls(Mgmt_params *params);
int Mgmt_totalregisters(Mgmt_params *params);
int Mgmt_creategraph(Mgmt_params *params);
int Mgmt_is_register_new(Mgmt_params *params);
int Mgmt_listregisters(Mgmt_params *params);
int Mgmt_list_sip_msg(Mgmt_params *params);
int Mgmt_list_history_sip_msg(Mgmt_params *params);
int Mgmt_cleanupregisters(Mgmt_params *params);
int Mgmt_d_close_call(Mgmt_params *params);
int Mgmt_d_pointer_to_call(Mgmt_params *params);
int Mgmt_d_lc_all(Mgmt_params *params);
int Mgmt_d_lc_bye(Mgmt_params *params);
int Mgmt_d_lc_for_destroy(Mgmt_params *params);
int Mgmt_destroy_close_calls(Mgmt_params *params);
int Mgmt_cleanup_tcpreassembly(Mgmt_params *params);
int Mgmt_expire_registers(Mgmt_params *params);
int Mgmt_cleanup_registers(Mgmt_params *params);
int Mgmt_cleanup_calls(Mgmt_params *params);
int Mgmt_dtls_queue_cleanup(Mgmt_params *params);
int Mgmt_getipaccount(Mgmt_params *params);
int Mgmt_ipaccountfilter(Mgmt_params *params);
int Mgmt_stopipaccount(Mgmt_params *params);
int Mgmt_fetchipaccount(Mgmt_params *params);
int Mgmt_livefilter(Mgmt_params *params);
int Mgmt_startlivesniffer(Mgmt_params *params);
int Mgmt_getlivesniffer(Mgmt_params *params);
int Mgmt_stoplivesniffer(Mgmt_params *params);
int Mgmt_getactivesniffers(Mgmt_params *params);
int Mgmt_readaudio(Mgmt_params *params);
int Mgmt_listen(Mgmt_params *params);
int Mgmt_listen_stop(Mgmt_params *params);
int Mgmt_active_call_info(Mgmt_params *params);
int Mgmt_options_qualify_refresh(Mgmt_params *params);
int Mgmt_send_call_info_refresh(Mgmt_params *params);
int Mgmt_fraud_refresh(Mgmt_params *params);
int Mgmt_fraud_rcc_dump(Mgmt_params *params);
int Mgmt_set_json_config(Mgmt_params *params);
int Mgmt_get_json_config(Mgmt_params *params);
int Mgmt_hot_restart(Mgmt_params *params);
int Mgmt_crules_print(Mgmt_params *params);
int Mgmt_reload(Mgmt_params *params);
int Mgmt_custom_headers_refresh(Mgmt_params *params);
int Mgmt_custom_headers_dump(Mgmt_params *params);
int Mgmt_no_hash_message_rules_refresh(Mgmt_params *params);
int Mgmt_billing_refresh(Mgmt_params *params);
int Mgmt_country_detect_refresh(Mgmt_params *params);
int Mgmt_flush_tar(Mgmt_params *params);
int Mgmt_fileexists(Mgmt_params *params);
int Mgmt_file_exists(Mgmt_params *params);
int Mgmt_getfile(Mgmt_params *params);
int Mgmt_getfile_in_tar(Mgmt_params *params);
int Mgmt_getfile_in_tar_check_complete(Mgmt_params *params);
int Mgmt_getfile_is_zip_support(Mgmt_params *params);
int Mgmt_genhttppcap(Mgmt_params *params);
int Mgmt_upgrade_restart(Mgmt_params *params);
int Mgmt_custipcache_vect_print(Mgmt_params *params);
int Mgmt_custipcache_refresh(Mgmt_params *params);
int Mgmt_custipcache_get_cust_id(Mgmt_params *params);
int Mgmt_syslogstr(Mgmt_params *params);
int Mgmt_coutstr(Mgmt_params *params);
int Mgmt_terminating(Mgmt_params *params);
int Mgmt_pcapstat(Mgmt_params *params);
int Mgmt_sniffer_threads(Mgmt_params *params);
int Mgmt_sniffer_stat(Mgmt_params *params);
int Mgmt_datadir_stat(Mgmt_params *params);
int Mgmt_gitUpgrade(Mgmt_params *params);
int Mgmt_login_screen_popup(Mgmt_params *params);
int Mgmt_processing_limitations(Mgmt_params *params);
int Mgmt_enable_bad_packet_order_warning(Mgmt_params *params);
int Mgmt_sipports(Mgmt_params *params);
int Mgmt_skinnyports(Mgmt_params *params);
int Mgmt_ignore_rtcp_jitter(Mgmt_params *params);
int Mgmt_convertchars(Mgmt_params *params);
int Mgmt_natalias(Mgmt_params *params);
int Mgmt_jemalloc_stat(Mgmt_params *params);
int Mgmt_heapprof(Mgmt_params *params);
int Mgmt_list_active_clients(Mgmt_params *params);
int Mgmt_memory_stat(Mgmt_params *params);
int Mgmt_sqlexport(Mgmt_params *params);
int Mgmt_sql_time_information(Mgmt_params *params);
int Mgmt_pausecall(Mgmt_params *params);
int Mgmt_unpausecall(Mgmt_params *params);
int Mgmt_setverbparam(Mgmt_params *params);
int Mgmt_cleanverbparams(Mgmt_params *params);
int Mgmt_set_pcap_stat_period(Mgmt_params *params);
int Mgmt_memcrash_test(Mgmt_params *params);
int Mgmt_memalloc_test(Mgmt_params *params);
int Mgmt_get_oldest_spooldir_date(Mgmt_params *params);
int Mgmt_get_sensor_information(Mgmt_params *params);
int Mgmt_alloc_trim(Mgmt_params *params);
int Mgmt_alloc_test(Mgmt_params *params);
int Mgmt_tcmalloc_stats(Mgmt_params *params);
int Mgmt_hashtable_stats(Mgmt_params *params);
int Mgmt_thread(Mgmt_params *params);
int Mgmt_usleep(Mgmt_params *params);
int Mgmt_charts_cache(Mgmt_params *params);
int Mgmt_packetbuffer_log(Mgmt_params *params);
int Mgmt_diameter_packets_stack(Mgmt_params *params);
int Mgmt_aes(Mgmt_params *params);
int Mgmt_manager_file(Mgmt_params *params);
int Mgmt_ssl_ipport(Mgmt_params *params);
int Mgmt_sql_errors_skip(Mgmt_params *params);

int (* MgmtFuncArray[])(Mgmt_params *params) = {
	Mgmt_help,
	Mgmt_getversion,
	Mgmt_listcalls,
	Mgmt_reindexfiles,
	Mgmt_offon,
	Mgmt_check_filesindex,
	Mgmt_reindexspool,
	Mgmt_printspool,
	Mgmt_listopentars,
	Mgmt_totalcalls,
	Mgmt_totalregisters,
	Mgmt_creategraph,
	Mgmt_is_register_new,
	Mgmt_listregisters,
	Mgmt_list_sip_msg,
	Mgmt_list_history_sip_msg,
	Mgmt_cleanupregisters,
	Mgmt_d_close_call,
	Mgmt_d_pointer_to_call,
	Mgmt_d_lc_all,
	Mgmt_d_lc_bye,
	Mgmt_d_lc_for_destroy,
	Mgmt_destroy_close_calls,
	Mgmt_cleanup_tcpreassembly,
	Mgmt_expire_registers,
	Mgmt_cleanup_registers,
	Mgmt_cleanup_calls,
	Mgmt_dtls_queue_cleanup,
	Mgmt_getipaccount,
	Mgmt_ipaccountfilter,
	Mgmt_stopipaccount,
	Mgmt_fetchipaccount,
	Mgmt_livefilter,
	Mgmt_startlivesniffer,
	Mgmt_getlivesniffer,
	Mgmt_stoplivesniffer,
	Mgmt_getactivesniffers,
	Mgmt_readaudio,
	Mgmt_listen,
	Mgmt_listen_stop,
	Mgmt_active_call_info,
	Mgmt_options_qualify_refresh,
	Mgmt_send_call_info_refresh,
	Mgmt_fraud_refresh,
	Mgmt_fraud_rcc_dump,
	Mgmt_set_json_config,
	Mgmt_get_json_config,
	Mgmt_hot_restart,
	Mgmt_crules_print,
	Mgmt_reload,
	Mgmt_custom_headers_refresh,
	Mgmt_custom_headers_dump,
	Mgmt_no_hash_message_rules_refresh,
	Mgmt_billing_refresh,
	Mgmt_country_detect_refresh,
	Mgmt_flush_tar,
	Mgmt_fileexists,
	Mgmt_file_exists,
	Mgmt_getfile,
	Mgmt_getfile_in_tar,
	Mgmt_getfile_in_tar_check_complete,
	Mgmt_getfile_is_zip_support,
	Mgmt_genhttppcap,
	Mgmt_upgrade_restart,
	Mgmt_custipcache_vect_print,
	Mgmt_custipcache_refresh,
	Mgmt_custipcache_get_cust_id,
	Mgmt_syslogstr,
	Mgmt_coutstr,
	Mgmt_terminating,
	Mgmt_pcapstat,
	Mgmt_sniffer_threads,
	Mgmt_sniffer_stat,
	Mgmt_datadir_stat,
	Mgmt_gitUpgrade,
	Mgmt_login_screen_popup,
	Mgmt_processing_limitations,
	Mgmt_enable_bad_packet_order_warning,
	Mgmt_sipports,
	Mgmt_skinnyports,
	Mgmt_ignore_rtcp_jitter,
	Mgmt_convertchars,
	Mgmt_natalias,
	Mgmt_jemalloc_stat,
	Mgmt_heapprof,
	Mgmt_list_active_clients,
	Mgmt_memory_stat,
	Mgmt_sqlexport,
	Mgmt_sql_time_information,
	Mgmt_pausecall,
	Mgmt_unpausecall,
	Mgmt_setverbparam,
	Mgmt_cleanverbparams,
	Mgmt_set_pcap_stat_period,
	Mgmt_memcrash_test,
	Mgmt_memalloc_test,
	Mgmt_get_oldest_spooldir_date,
	Mgmt_get_sensor_information,
	Mgmt_alloc_trim,
	Mgmt_alloc_test,
	Mgmt_tcmalloc_stats,
	Mgmt_hashtable_stats,
	Mgmt_thread,
	Mgmt_usleep,
	Mgmt_charts_cache,
	Mgmt_packetbuffer_log,
	Mgmt_diameter_packets_stack,
	Mgmt_aes,
	Mgmt_manager_file,
	Mgmt_ssl_ipport,
	Mgmt_sql_errors_skip
};

struct listening_worker_arg {
	Call *call;
};

static void updateLivesnifferfilters();
static bool cmpCallBy_destroy_call_at(Call* a, Call* b);
static bool cmpCallBy_first_packet_time(Call* a, Call* b);

livesnifferfilter_use_siptypes_s livesnifferfilterUseSipTypes;

ManagerClientThreads ClientThreads;

volatile int ssh_threads;
volatile int ssh_threads_break; 

class c_getfile_in_tar_completed {
public:
	c_getfile_in_tar_completed() {
		_sync = 0;
	}
	void add(const char *tar, const char *file, const char *key) {
		lock();
		data[string(tar) + "/" + file + "/" + key] = getTimeMS();
		unlock();
	}
	bool check(const char *tar, const char *file, const char *key) {
		lock();
		map<string, u_int64_t>::iterator iter = data.find(string(tar) + "/" + file + "/" + key);
		bool rslt =  iter != data.end();
		unlock();
		cleanup();
		return(rslt);
	}
	void cleanup() {
		lock();
		u_int64_t actTime = getTimeMS();
		map<string, u_int64_t>::iterator iter = data.begin();
		while(iter != data.end()) {
			if(actTime - iter->second > 10000ull) {
				data.erase(iter++);
			} else {
				++iter;
			}
		}
		unlock();
	}
	void lock() {
		__SYNC_LOCK(_sync);
	}
	void unlock() {
		__SYNC_UNLOCK(_sync);
	}
private:
	map<string, u_int64_t> data;
	volatile int _sync;
} getfile_in_tar_completed;

class c_listening_clients {
public:
	struct s_client {
		s_client(const char *id, Call *call) {
			this->id = id;
			this->call = call;
			last_activity_time = getTimeS();
			spybuffer_start_pos = 0;
			spybuffer_last_send_pos = 0;
		}
		string id;
		Call *call;
		u_int32_t last_activity_time;
		u_int64_t spybuffer_start_pos;
		u_int64_t spybuffer_last_send_pos;
	};
public:
	c_listening_clients() {
		_sync = 0;
		_sync_map = 0;
	}
	~c_listening_clients() {
		lock_map();
		while(clients.size()) {
			map<string, s_client*>::iterator iter = clients.begin();
			delete iter->second;
			clients.erase(iter++);
		}
		unlock_map();
	}
	s_client *add(const char *id, Call *call) {
		s_client *client = new FILE_LINE(13001) s_client(id, call);
		string cid = string(id) + '/' + intToString((long long)call);
		lock_map();
		clients[cid] = client;
		unlock_map();
		return(client);
	}
	s_client *get(const char *id, Call *call) {
		string cid = string(id) + '/' + intToString((long long)call);
		lock_map();
		map<string, s_client*>::iterator iter = clients.find(cid);
		if(iter != clients.end()) {
			unlock_map();
			return(iter->second);
		} else {
			unlock_map();
			return(NULL);
		}
	}
	void remove(const char *id, Call *call) {
		string cid = string(id) + '/' + intToString((long long)call);
		lock_map();
		map<string, s_client*>::iterator iter = clients.find(cid);
		if(iter != clients.end()) {
			delete iter->second;
			clients.erase(iter);
		}
		unlock_map();
	}
	void remove(s_client *client) {
		remove(client->id.c_str(), client->call);
	}
	void cleanup() {
		lock_map();
		u_int64_t actTime = getTimeS();
		for(map<string, s_client*>::iterator iter = clients.begin(); iter != clients.end(); ) {
			if(iter->second->last_activity_time < actTime - 10) {
				delete iter->second;
				clients.erase(iter++);
			} else {
				iter++;
			}
		}
		unlock_map();
	}
	bool exists(Call *call) {
		bool exists = false;
		for(map<string, s_client*>::iterator iter = clients.begin(); iter != clients.end(); iter++) {
			if(iter->second->call == call) {
				exists = true;
				break;
			}
		}
		return(exists);
	}
	u_int64_t get_min_use_spybuffer_pos(Call *call) {
		u_int64_t min_pos = (u_int64_t)-1;
		lock_map();
		for(map<string, s_client*>::iterator iter = clients.begin(); iter != clients.end(); iter++) {
			if(iter->second->call == call &&
			   max(iter->second->spybuffer_start_pos, iter->second->spybuffer_last_send_pos) < min_pos) {
				min_pos = max(iter->second->spybuffer_start_pos, iter->second->spybuffer_last_send_pos);
			}
		}
		unlock_map();
		return(min_pos == (u_int64_t)-1 ? 0 : min_pos);
	}
	void lock() {
		__SYNC_LOCK(_sync);
	}
	void unlock() {
		__SYNC_UNLOCK(_sync);
	}
	void lock_map() {
		__SYNC_LOCK(_sync_map);
	}
	void unlock_map() {
		__SYNC_UNLOCK(_sync_map);
	}
private:
	map<string, s_client*> clients;
	volatile int _sync;
	volatile int _sync_map;
} listening_clients;

class c_listening_workers {
public:
	struct s_worker {
		s_worker(Call *call) {
			this->call = call;
			spybuffer = new FILE_LINE(13002) FifoBuffer((string("spybuffer for call ") + call->call_id).c_str());
			spybuffer->setMinItemBufferLength(1000);
			spybuffer->setMaxSize(10000000);
			thread = 0;
			running = false;
			stop = false;
		}
		~s_worker() {
			if(spybuffer) {
				delete spybuffer;
			}
		}
		Call *call;
		FifoBuffer *spybuffer;
		pthread_t thread;
		volatile bool running;
		volatile bool stop;
	};
	c_listening_workers() {
		_sync = 0;
		_sync_map = 0;
	}
	~c_listening_workers() {
		lock_map();
		while(workers.size()) {
			map<Call*, s_worker*>::iterator iter = workers.begin();
			delete iter->second;
			workers.erase(iter++);
		}
		unlock_map();
	}
	s_worker *add(Call *call) {
		s_worker *worker = new FILE_LINE(13003) s_worker(call);
		lock_map();
		workers[call] = worker;
		unlock_map();
		return(worker);
	}
	s_worker *get(Call *call) {
		lock_map();
		map<Call*, s_worker*>::iterator iter = workers.find(call);
		if(iter != workers.end()) {
			unlock_map();
			return(iter->second);
		} else {
			unlock_map();
			return(NULL);
		}
	}
	void remove(Call *call) {
		lock_map();
		map<Call*, s_worker*>::iterator iter = workers.find(call);
		if(iter != workers.end()) {
			iter->second->call->disableListeningBuffers();
			delete iter->second;
			workers.erase(iter);
		}
		unlock_map();
	}
	void remove(s_worker *worker) {
		remove(worker->call);
	}
	void run(s_worker *worker) {
		worker->call->createListeningBuffers();
		worker->running = true;
		worker->stop = false;
		vm_pthread_create_autodestroy("manager - listening worker",
					      &worker->thread, NULL, worker_thread_function, (void*)worker, __FILE__, __LINE__);
	}
	void stop(s_worker *worker) {
		worker->stop = true;
	}
	static void *worker_thread_function(void *arguments);
	void cleanup() {
		for(map<Call*, s_worker*>::iterator iter = workers.begin(); iter != workers.end(); ) {
			if(!listening_clients.exists(iter->second->call)) {
				stop(iter->second);
				while(iter->second->running) {
					USLEEP(100);
				}
			}
			if(!iter->second->running) {
				iter->second->call->disableListeningBuffers();
				delete iter->second;
				workers.erase(iter++);
			} else {
				iter++;
			}
		}
	}
	void lock() {
		__SYNC_LOCK(_sync);
	}
	void unlock() {
		__SYNC_UNLOCK(_sync);
	}
	void lock_map() {
		__SYNC_LOCK(_sync_map);
	}
	void unlock_map() {
		__SYNC_UNLOCK(_sync_map);
	}
private:
	map<Call*, s_worker*> workers;
	volatile int _sync;
	volatile int _sync_map;
} listening_workers;

/* 
 * this function runs as thread. It reads RTP audio data from call
 * and write it to output buffer 
 *
 * input parameter is structure where call 
 *
*/
void* c_listening_workers::worker_thread_function(void *arguments) {
 
	c_listening_workers::s_worker *worker = (c_listening_workers::s_worker*)arguments;
	Call *call = worker->call;
	worker->running = true;

	alaw_init();
	ulaw_init();

	// if call is hanged hup it will set listening_worker_run in its destructor to 0
	int listening_worker_run = 1;
	call->listening_worker_run = &listening_worker_run;
	pthread_mutex_lock(&call->listening_worker_run_lock);

	FILE *out = NULL;
	if(sverb.call_listening) {
		out = fopen("/tmp/test.raw", "w");
	}

	/*
	vorbis_desc ogg;
	ogg_header(out, &ogg);
	fclose(out);
	pthread_mutex_lock(&args->call->buflock);
	(&args->call->spybufferchar, &ogg);
	pthread_mutex_unlock(&args->call->buflock);
	*/

	unsigned long long begin_time_us = 0;
	unsigned long long end_time_us = 0;
	unsigned long long prev_process_time_us = 0;
        struct timeval tvwait;

	
	unsigned int period_msec = 50;
	unsigned int period_samples = 8000 * period_msec / 1000;
	u_char *spybufferchunk = new FILE_LINE(13004) u_char[period_samples * 2];
	u_int32_t len1, len2;
	short int r1, r2;
	char *s16char;
	
        while(listening_worker_run && !worker->stop) {

		/*
		while(max(call->audiobuffer1->size_get(), call->audiobuffer2->size_get()) < period_msec * 2) {
			USLEEP(period_msec * 1000);
		}
		*/
	 
		prev_process_time_us = end_time_us - begin_time_us;

		tvwait.tv_sec = 0;
		tvwait.tv_usec = 1000 * period_msec - prev_process_time_us;
		select(0, NULL, NULL, NULL, &tvwait);

		begin_time_us = getTimeUS();
		
		len1 = call->audioBufferData[0].audiobuffer->size_get();
		len2 = call->audioBufferData[1].audiobuffer->size_get();

		/*
		printf("codec_caller[%d] codec_called[%d] len1[%d] len2[%d]\n", 
		       worker->call->codec_caller, 
		       worker->call->codec_called,
		       len1, len2);
		*/
		
		if(len1 >= period_samples || len2 >= period_samples) {
			if(len1 >= period_samples && len2 >= period_samples) {
				len1 = period_samples;
				len2 = period_samples;
				unsigned char *read1 = call->audioBufferData[0].audiobuffer->pop(&len1);
				unsigned char *read2 = call->audioBufferData[1].audiobuffer->pop(&len2);
				for(unsigned int i = 0; i < len1; i++) {
					switch(call->codec_caller) {
					case 0:
						r1 = ULAW(read1[i]);
						break;
					case 8:
						r1 = ALAW(read1[i]);
						break;
					}
					switch(call->codec_caller) {
					case 0:
						r2 = ULAW(read2[i]);
						break;
					case 8:
						r2 = ALAW(read2[i]);
						break;
					}
					s16char = (char *)&r1;
					slinear_saturated_add((short int*)&r1, (short int*)&r2);
					if(sverb.call_listening) {
						fwrite(&r1, 1, 2, out);
					}
					spybufferchunk[i * 2] = s16char[0];
					spybufferchunk[i * 2 + 1] = s16char[1];
				}
				delete [] read1;
				delete [] read2;
			} else if(len2 >= period_samples) {
				len2 = period_samples;
				unsigned char *read2 = call->audioBufferData[1].audiobuffer->pop(&len2);
				for(unsigned int i = 0; i < len2; i++) {
					switch(call->codec_caller) {
					case 0:
						r2 = ULAW(read2[i]);
						break;
					case 8:
						r2 = ALAW(read2[i]);
						break;
					}
					if(sverb.call_listening) {
						fwrite(&r2, 1, 2, out);
					}
					s16char = (char *)&r2;
					spybufferchunk[i * 2] = s16char[0];
					spybufferchunk[i * 2 + 1] = s16char[1];
				}
				delete [] read2;
			} else if(len1 >= period_samples) {
				len1 = period_samples;
				unsigned char *read1 = call->audioBufferData[0].audiobuffer->pop(&len1);
				for(unsigned int i = 0; i < len1; i++) {
					switch(call->codec_caller) {
					case 0:
						r1 = ULAW(read1[i]);
						break;
					case 8:
						r1 = ALAW(read1[i]);
						break;
					}
					if(sverb.call_listening) {
						fwrite(&r1, 1, 2, out);
					}
					s16char = (char *)&r1;
					spybufferchunk[i * 2] = s16char[0];
					spybufferchunk[i * 2 + 1] = s16char[1];
				}
				delete [] read1;
			}
			worker->spybuffer->lock_master();
			worker->spybuffer->push(spybufferchunk, period_samples * 2);
			worker->spybuffer->unlock_master();
		}
		
		end_time_us = getTimeUS();
        }

	if(sverb.call_listening) {
		fclose(out);
	}
	
	/*
	//clean ogg
        ogg_stream_clear(&ogg.os);
        vorbis_block_clear(&ogg.vb);
        vorbis_dsp_clear(&ogg.vd);
        vorbis_comment_clear(&ogg.vc);
        vorbis_info_clear(&ogg.vi);
        */

	delete [] spybufferchunk;

	// reset pointer to NULL as we are leaving the stack here
	call->listening_worker_run = NULL;
	pthread_mutex_unlock(&call->listening_worker_run_lock);
	
	worker->running = false;
	
	return 0;
}

void listening_master_lock() {
	calltable->lock_calls_listMAP();
	listening_workers.lock();
	listening_clients.lock();
}

void listening_master_unlock() {
	listening_clients.unlock();
	listening_workers.unlock();
	calltable->unlock_calls_listMAP();
}

void listening_cleanup() {
	listening_master_lock();
	listening_clients.cleanup();
	listening_workers.cleanup();
	listening_master_unlock();
}

void listening_remove_worker(Call *call) {
	listening_master_lock();
	listening_workers.remove(call);
	listening_master_unlock();
}

void try_ip_mask(vmIP &addr, vmIP &mask, string &ipstr) {
        vector<string> ip_mask = split(ipstr.c_str(), "/", true);
	if(ip_mask.size() >= 2) {
		vmIP ip = str_2_vmIP(ip_mask[0].c_str());
		unsigned mask_length = atoi(ip_mask[1].c_str());
		addr = ip.network(mask_length);
		mask = ip.network_mask(mask_length);
	}
}

static volatile bool enable_parse_command = false;

void manager_parse_command_enable() {
	enable_parse_command = true;
}

void manager_parse_command_disable() {
	enable_parse_command = false;
}

static int _parse_command(char *buf, int size, sClientInfo client, cClient *c_client, cAesKey *aes_key, const char *aes_cipher, bool aes_missing, ManagerClientThread **managerClientThread);

int parse_command(string cmd, sClientInfo client, cClient *c_client, cAesKey *aes_key, const char *aes_cipher, bool aes_missing) {
	ManagerClientThread *managerClientThread = NULL;
	int rslt = _parse_command((char*)cmd.c_str(), cmd.length(), client, c_client, aes_key, aes_cipher, aes_missing, &managerClientThread);
	if(managerClientThread) {
		if(managerClientThread->parseCommand()) {
			ClientThreads.add(managerClientThread);
			managerClientThread->run();
		} else {
			delete managerClientThread;
			if(client.handler) {
				close(client.handler);
			}
		}
	} else {
		if(client.handler) {
			close(client.handler);
		}
	}
	return(rslt);
}

sMgmtCmdsReg *find_mgmt_cmd(char *cmd) {
	for(int i = 0; i < 2; i++) {
		char *pointerToEndSeparator = strstr(cmd, i == 0 ? "\r" : "\n");
		if(pointerToEndSeparator) {
			*pointerToEndSeparator = 0;
		}
	}
	sMgmtCmdsReg *mgmtCmd = NULL;
	char *pointerToSeparatorInCmd = strpbrk(cmd, " \r\n\t");
	std::map<string, sMgmtCmdsReg>::iterator MgmtItem = MgmtCmdsRegTable.find(pointerToSeparatorInCmd ? string(cmd, pointerToSeparatorInCmd) : cmd);
	if(MgmtItem != MgmtCmdsRegTable.end()) {
		mgmtCmd = &MgmtItem->second;
	}
	if(!mgmtCmd) {
		map<string, sMgmtCmdsReg>::iterator MgmtItem;
		for(MgmtItem = MgmtCmdsRegTable.begin(); MgmtItem != MgmtCmdsRegTable.end(); MgmtItem++) {
			if(strstr(cmd, MgmtItem->first.c_str())) {
				mgmtCmd = &MgmtItem->second;
				break;
			}
		}
	}
	return(mgmtCmd);
}

int _parse_command(char *buf, int size, sClientInfo client, cClient *c_client, cAesKey *aes_key, const char *aes_cipher, bool aes_missing, ManagerClientThread **managerClientThread) {
	if(!enable_parse_command) {
		return(0);
	}
	sMgmtCmdsReg *mgmtCmd = find_mgmt_cmd(buf);
	if(sverb.manager) {
		syslog(LOG_NOTICE, "manager command: %s|END", buf);
	}
	Mgmt_params* mparams = new FILE_LINE(0) Mgmt_params(buf, size, client, c_client, aes_key, aes_cipher, managerClientThread);
	if(mgmtCmd) {
		if(aes_missing && !cManagerAes::notNeedAesForCommand(buf, mgmtCmd)) {
			mparams->sendString("need aes!\n");
			delete mparams;
			return(-1);
		}
		mparams->command = mgmtCmd->command;
		int ret = mgmtCmd->mgmtFce(mparams);
		delete mparams;
		return(ret);
	} else {
		mparams->sendString("command not found\n");
		delete mparams;
		string error = string("Can't determine the command '") + buf + "'";
		syslog(LOG_ERR, "%s", error.c_str());
		return(-1);
	}
}

int Handle_pause_call(long long callref, int val ) {
	int retval = 1;

	if (calltable) {
		calltable->lock_calls_listMAP();
		Call *call = calltable->find_by_reference(callref, false);

		if (call)
			call->silencerecording = val;
		else
			retval = -1;

		calltable->unlock_calls_listMAP();
	}
	return(retval);
}


/*
struct svi {
	volatile char command_type[100];
	volatile int i;
};
volatile svi vi[500];
extern pthread_mutex_t commmand_type_counter_sync;

bool _strncmp_v(volatile char a[], const char *b, unsigned length) {
	for(unsigned i = 0; i < length; i++) {
		if(a[i] != b[i]) {
			return(true);
		}
		if(!a[i] || !b[i]) {
			break;
		}
	}
	return(false);
}

bool _strncpy_v(volatile char dst[], const char *src, unsigned length) {
	for(unsigned i = 0; i < length; i++) {
		dst[i] = src[i];
		if(!src[i]) {
			break;
		}
	}
}

static bool addCommandType(string command_type) {
	bool rslt = false;
	pthread_mutex_lock(&commmand_type_counter_sync);
	for(unsigned i = 0; i < sizeof(vi) / sizeof(svi); i++) {
		if(!_strncmp_v(vi[i].command_type, command_type.c_str(), sizeof(vi[i].command_type))) {
			if(vi[i].i < 20) {
				++vi[i].i;
				rslt = true;
			}
			break;
		} else if(!vi[i].command_type[0]) {
			_strncpy_v(vi[i].command_type, command_type.c_str(), sizeof(vi[i].command_type));
			vi[i].i = 1;
			rslt = true;
			break;
		}
	}
	
// 	map<string, vi*>::iterator iter = commmand_type_counter.find(command_type);
// 	if(iter == commmand_type_counter.end()) {
// 		vi *_i = new vi;
// 		_i->i = 1;
// 		commmand_type_counter[command_type] = _i;
// 		rslt = true;
// 	} else {
// 		if(commmand_type_counter[command_type]->i < 20) {
// 			__SYNC_INC(commmand_type_counter[command_type]->i);
// 			rslt = true;
// 		}
// 	}
	
	pthread_mutex_unlock(&commmand_type_counter_sync);
	return(rslt);
}

static void subCommandType(string command_type) {
	pthread_mutex_lock(&commmand_type_counter_sync);
	for(unsigned i = 0; i < sizeof(vi) / sizeof(svi); i++) {
		if(!_strncmp_v(vi[i].command_type, command_type.c_str(), sizeof(vi[i].command_type))) {
			if(vi[i].i > 0) {
				--vi[i].i;
			}
			break;
		}
	}
	
// 	if(commmand_type_counter[command_type]->i > 0) {
// 		__SYNC_DEC(commmand_type_counter[command_type]->i);
// 	}
	
	pthread_mutex_unlock(&commmand_type_counter_sync);
}
*/

void *manager_read_thread(void * arg) {

	char buf[BUFSIZE];
	int size;
	SimpleBuffer command_buffer;
	string command;
	cAesKey aes_key;
	string aes_cipher;
	bool aes_missing = false;
	sManagerClientInfo clientInfo = *(sManagerClientInfo*)arg;
	delete (sManagerClientInfo*)arg;
	bool debugRecv = verbosity >= 2;
	if(debugRecv) {
		cout << "START manager_read_thread" << endl;
	}
	if ((size = recv(clientInfo.handler, buf, BUFSIZE - 1, 0)) == -1) {
		cerr << "Error in receiving data" << endl;
		close(clientInfo.handler);
		return 0;
	} else {
		buf[size] = '\0';
		if(debugRecv) {
			cout << "DATA (" << size << ")" << endl;
			hexdump(buf, size);
		}
		if(!strncmp(buf, "aes", 3) ?
		    memmem(buf, size, ":sea", 4) :
		    strstr(buf, "\r\n\r\n")) {
			command_buffer.add(buf, size);
		} else {
			if(debugRecv) {
				cout << "NEXT_RECV start" << endl;
			}
			command_buffer.add(buf, size);
			while(true) {
				bool doRead = false;
				int timeout_ms = 500;
				if(opt_socket_use_poll) {
					pollfd fds[2];
					memset(fds, 0 , sizeof(fds));
					fds[0].fd = clientInfo.handler;
					fds[0].events = POLLIN;
					int rsltPool = poll(fds, 1, timeout_ms);
					if(rsltPool > 0 && fds[0].revents) {
						doRead = true;
					}
				} else {
					fd_set rfds;
					struct timeval tv;
					FD_ZERO(&rfds);
					FD_SET(clientInfo.handler, &rfds);
					tv.tv_sec = 0;
					tv.tv_usec = timeout_ms * 1000;
					int rsltSelect = select(clientInfo.handler + 1, &rfds, (fd_set *) 0, (fd_set *) 0, &tv);
					if(rsltSelect > 0 && FD_ISSET(clientInfo.handler, &rfds)) {
						doRead = true;
					}
				}
				if(doRead &&
				   (size = recv(clientInfo.handler, buf, BUFSIZE - 1, 0)) > 0) {
					buf[size] = '\0';
					command_buffer.add(buf, size);
					if(debugRecv) {
						cout << "NEXT DATA (" << size << ")" << endl;
						hexdump(buf, size);
					}
					if(cManagerAes::existsEnd(&command_buffer, NULL)) {
						break;
					}
				} else {
					break;
				}
			}
			if(debugRecv) {
				cout << "NEXT_RECV stop" << endl;
			}
		}
		if(cManagerAes::isAes(&command_buffer)) {
			if(!cManagerAes::decrypt(&command_buffer, &command, &aes_key, &aes_cipher)) {
				command = "failed_aes_decrypt";
			}
		} else {
			if(!clientInfo.file_socket && cManagerAes::getAesKey(NULL)) {
				aes_missing = true;
			}
			command = (char*)command_buffer;
		}
	}
	
	size_t posEnd;
	if((posEnd = command.find("\r\n\r\n")) != string::npos) {
		command.resize(posEnd);
	}
	if(!command.empty()) {
		parse_command(command, clientInfo, NULL, &aes_key, aes_cipher.c_str(), aes_missing);
	} else if(clientInfo.handler) {
		close(clientInfo.handler);
	}
	
	termTimeCacheForThread();

	return 0;
}


bool cManagerAes::getAesKey(cAesKey *aes_key, bool force) {
	if(!is_support_manager_aes()) {
		cManagerAes::aes_key.ckey.clear();
		cManagerAes::aes_key.ivec.clear();
		return(false);
	}
	extern string opt_manager_aes_key;
	extern string opt_manager_aes_iv;
	if(!opt_manager_aes_key.empty() && !opt_manager_aes_iv.empty()) {
		string ckey = base64_decode(opt_manager_aes_key.c_str());
		string ivec = base64_decode(opt_manager_aes_iv.c_str());
		if(aes_key) {
			aes_key->ckey = ckey;
			aes_key->ivec = ivec;
		}
		cManagerAes::aes_key.ckey = ckey;
		cManagerAes::aes_key.ivec = ivec;
		return(true);
	}
	__SYNC_LOCK(_sync);
	if(!force &&
	   cManagerAes::aes_key.isSetKeys()) {
		if(aes_key) {
			*aes_key = cManagerAes::aes_key;
		}
		__SYNC_UNLOCK(_sync);
		return(true);
	}
	bool rslt = false;
	SqlDb *sqlDb = createSqlObject();
	sqlDb->query("SELECT * from `system` where type = 'manager_key'");
	SqlDb_row row = sqlDb->fetchRow();
	if(row) {
		JsonItem jsonAesKey;
		jsonAesKey.parse(row["content"]);
		string key = jsonAesKey.getValue("key");
		string iv = jsonAesKey.getValue("iv");
		if(!key.empty() && !iv.empty()) {
			string ckey = base64_decode(key.c_str());
			string ivec = base64_decode(iv.c_str());
			if(aes_key) {
				aes_key->ckey = ckey;
				aes_key->ivec = ivec;
			}
			cManagerAes::aes_key.ckey = ckey;
			cManagerAes::aes_key.ivec = ivec;
			rslt = true;
		}
	}
	delete sqlDb;
	if(!rslt) {
		cManagerAes::aes_key.ckey.clear();
		cManagerAes::aes_key.ivec.clear();
	}
	__SYNC_UNLOCK(_sync);
	return(rslt);
}

bool cManagerAes::isAes(SimpleBuffer *buffer) {
	return(buffer->size() >= 3 &&
	       !strncasecmp((char*)buffer->data(), "aes", 3));
 
}

bool cManagerAes::existsEnd(SimpleBuffer *buffer, int *endPos) {
	bool is_aes = isAes(buffer);
	if(buffer->size() >= (is_aes ? 4 : 2)) {
		u_char *endPointer;
		if((endPointer = (u_char*)memmem(buffer->data(), buffer->size(), is_aes ? ":sea" : "\r\n", is_aes ? 4 : 2)) != NULL) {
			if(endPos) {
				*endPos = endPointer - buffer->data();
			}
			return(true);
		}
	}
	return(false);
}

bool cManagerAes::decrypt(SimpleBuffer *buffer, string *rslt, cAesKey *aes_key, string *aes_cipher) {
	if(!getAesKey(aes_key)) {
		return(false);
	}
	u_char *aes_cipher_end = (u_char*)strnchr((char*)buffer->data(), ':', buffer->size());
	if(aes_cipher_end && 
	   aes_cipher_end - buffer->data() < 20) {
		u_char *end_aes = (u_char*)memmem(buffer->data(), buffer->size(), ":sea", 4);
		if(end_aes) {
			*aes_cipher = string((char*)buffer->data(), aes_cipher_end - buffer->data());
			for(int pass = 0; pass < 2; pass++) {
				if(pass > 0) {
					getAesKey(aes_key, true);
				}
				cAes aes;
				aes.setKeys(aes_key);
				aes.setCipher(aes_cipher->c_str());
				u_char *decrypted;
				size_t decrypted_length;
				if(aes.decrypt(buffer->data() + (aes_cipher->length() + 1), end_aes - buffer->data() - (aes_cipher->length() + 1), &decrypted, &decrypted_length, true)) {
					*rslt = string((char*)decrypted, decrypted_length);
					delete [] decrypted;
					return(true);
				}
			}
		}
	}
	return(false);
}

bool cManagerAes::notNeedAesForCommand(char *command, sMgmtCmdsReg *mgmtCmd) {
	extern bool opt_manager_enable_unencrypted;
	if(opt_manager_enable_unencrypted) {
		return(true);
	}
	if(!mgmtCmd) {
		mgmtCmd = find_mgmt_cmd(command);
	}
	if(mgmtCmd) {
		if(mgmtCmd->notNeedAes == 1) {
			return(true);
		} else if(mgmtCmd->notNeedAes == 2) {
			Mgmt_params params(command, strlen(command), sClientInfo(), NULL, NULL, NULL, NULL);
			params.task = Mgmt_params::mgmt_task_CheckNeedAes;
			if(!mgmtCmd->mgmtFce(&params)) {
				return(true);
			}
		}
	}
	if(!getAesKey(NULL, true)) {
		return(true);
	}
	return(false);
}

cAesKey cManagerAes::aes_key;
volatile int cManagerAes::_sync = 0;


void *manager_server(void *arg) {
 	sManagerServerArgs managerServerArgs;
	if(arg) {
		managerServerArgs = *(sManagerServerArgs*)arg;
		delete (sManagerServerArgs*)arg;
	}
	if(!managerServerArgs.file_socket.empty()) {
		manager_file_terminating = false;
	}
	int *manager_socket = managerServerArgs.file_socket.empty() ? &manager_socket_server : &manager_socket_file_server;
	if((*manager_socket = (managerServerArgs.file_socket.empty() ?
				socket_create(str_2_vmIP(opt_manager_ip), SOCK_STREAM, IPPROTO_TCP) :
				socket(AF_UNIX, SOCK_STREAM, 0))) == -1) {
		syslog(LOG_ERR, "Cannot create manager %s socket", managerServerArgs.file_socket.empty() ? "tcp": "file");
		return 0;
	}
	int on = 1;
	setsockopt(*manager_socket, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
	if(opt_manager_nonblock_mode || managerServerArgs.non_block) {
		int flags = fcntl(*manager_socket, F_GETFL, 0);
		if(flags >= 0) {
			fcntl(*manager_socket, F_SETFL, flags | O_NONBLOCK);
		}
	}
	bool bindOk = false;
	while(!bindOk) {
		if(managerServerArgs.file_socket.empty()) {
			if(socket_bind(*manager_socket, str_2_vmIP(opt_manager_ip), opt_manager_port) != -1) {
				bindOk = true;
			} else {
				syslog(LOG_ERR, "Cannot bind to port [%d]; trying again after 5 seconds intervals", opt_manager_port);
				sleep(5);
			}
		} else {
			sockaddr_un file_socket_addr;
			memset(&file_socket_addr, 0, sizeof(file_socket_addr));
			file_socket_addr.sun_family = AF_UNIX;
			strncpy(file_socket_addr.sun_path, managerServerArgs.file_socket.c_str(), sizeof(file_socket_addr.sun_path));
			file_socket_addr.sun_path[sizeof(file_socket_addr.sun_path) - 1] = 0;
			unlink(file_socket_addr.sun_path);
			if(bind(*manager_socket, (struct sockaddr *)&file_socket_addr, sizeof(file_socket_addr)) != -1) {
				extern int opt_fork;
				if(opt_fork) {
					if(chown(file_socket_addr.sun_path, 0, 0) == -1) {
						syslog(LOG_ERR, "Cannot chown root [%s]", file_socket_addr.sun_path);
						return 0;
					}
				}
				if(chmod(file_socket_addr.sun_path, S_IRUSR | S_IWUSR) == -1) {
					syslog(LOG_ERR, "Cannot chmod S_IRUSR | S_IWUSR [%s]", file_socket_addr.sun_path);
					return 0;
				}
				bindOk = true;
			} else {
				syslog(LOG_ERR, "Cannot bind to [%s]; trying again after 5 seconds intervals", file_socket_addr.sun_path);
				sleep(5);
			}
		}
	}
	if(listen(*manager_socket, 512) == -1) {
		syslog(LOG_ERR, "Cannot create manager queue");
		return 0;
	}
	pthread_t threads;
	pthread_attr_t attr;
	fd_set rfds;
	struct timeval tv;
	while(!is_terminating_without_error() && 
	      (managerServerArgs.file_socket.empty() ? true : !manager_file_terminating)) {
		bool doAccept = false;
		int timeout = managerServerArgs.timeout > 0 ? managerServerArgs.timeout : 10;
		if(opt_manager_nonblock_mode || managerServerArgs.non_block) {
			if(opt_socket_use_poll) {
				pollfd fds[2];
				memset(fds, 0 , sizeof(fds));
				fds[0].fd = *manager_socket;
				fds[0].events = POLLIN;
				if(poll(fds, 1, timeout * 1000) > 0) {
					doAccept = true;
				}
			} else {
				FD_ZERO(&rfds);
				FD_SET(*manager_socket, &rfds);
				tv.tv_sec = timeout;
				tv.tv_usec = 0;
				if(select(*manager_socket + 1, &rfds, (fd_set *) 0, (fd_set *) 0, &tv) > 0) {
					doAccept = true;
				}
			}
		} else {
			doAccept = true;
		}
		if(doAccept) {
			vmIP clientIP;
			vmPort clientPort;
			int clientHandler = socket_accept(*manager_socket, &clientIP, &clientPort);
			if(sverb.manager) {
				syslog(LOG_NOTICE, "manager connect from: %s %u", clientIP.getString().c_str(), clientPort.getPort());
			}
			if(is_terminating_without_error()) {
				close(clientHandler);
				close(*manager_socket);
				return 0;
			}
			if(clientHandler == -1) {
				//cerr << "Problem with accept client" <<endl;
				close(clientHandler);
				continue;
			}
			pthread_attr_init(&attr);
			sManagerClientInfo *clientInfo = new FILE_LINE(0) sManagerClientInfo(clientHandler, clientIP);
			clientInfo->file_socket = !managerServerArgs.file_socket.empty();
			int rslt = pthread_create (		/* Create a child thread        */
					&threads,		/* Thread ID (system assigned)  */    
					&attr,			/* Default thread attributes    */
					manager_read_thread,	/* Thread routine               */
					clientInfo);		/* Arguments to be passed       */
			pthread_detach(threads);
			pthread_attr_destroy(&attr);
			if(rslt != 0) {
				syslog(LOG_ERR, "manager pthread_create failed with rslt code %i", rslt);
			}
		}
	}
	close(*manager_socket);
	if(!managerServerArgs.file_socket.empty()) {
		unlink(managerServerArgs.file_socket.c_str());
	}
	return 0;
}

bool manager_file_server_start(string *file, string *error) {
	if(manager_file_thread > 0) {
		*error = string("running (") + (!opt_manager_socket.empty() ? opt_manager_socket : opt_manager_socket_run_via_manager) + ")";
		return(false);
	}
	init_management_functions();
	string managersocket = file->empty() ? tmpnam() : *file;
	sManagerServerArgs *managerServerArgs = new FILE_LINE(0) sManagerServerArgs;
	managerServerArgs->file_socket = managersocket;
	managerServerArgs->non_block = true;
	managerServerArgs->timeout = 1;
	vm_pthread_create("manager socket server",
			  &manager_file_thread, NULL, manager_server, managerServerArgs, __FILE__, __LINE__);
	*file = managersocket;
	return(true);
}

bool manager_file_server_stop(string *error) {
	if(manager_file_thread == 0) {
		*error = "not running";
		return(false);
	} else if(!opt_manager_socket.empty()) {
		*error = "manager enabled via the managersocket option cannot be terminated";
		return(false);
	}
	manager_file_terminating = true;
	pthread_join(manager_file_thread, NULL);
	manager_file_thread = 0;
	return(true);
}

void livesnifferfilter_s::updateState() {
	state_s new_state; 
	new_state.all_saddr = true;
	new_state.all_daddr = true;
	new_state.all_bothaddr = true;
	new_state.all_bothport = true;
	new_state.all_srcnum = true;
	new_state.all_dstnum = true;
	new_state.all_bothnum = true;
	new_state.all_fromhstr = true;
	new_state.all_tohstr = true;
	new_state.all_bothhstr = true;
	new_state.all_vlan = true;
	new_state.all_siptypes = true;
	for(int i = 0; i < MAXLIVEFILTERS; i++) {
		if(this->lv_saddr[i].isSet()) {
			new_state.all_saddr = false;
		}
		if(this->lv_daddr[i].isSet()) {
			new_state.all_daddr = false;
		}
		if(this->lv_bothaddr[i].isSet()) {
			new_state.all_bothaddr = false;
		}
		if(this->lv_bothport[i].isSet()) {
			new_state.all_bothport = false;
		}
		if(this->lv_srcnum[i][0]) {
			new_state.all_srcnum = false;
		}
		if(this->lv_dstnum[i][0]) {
			new_state.all_dstnum = false;
		}
		if(this->lv_bothnum[i][0]) {
			new_state.all_bothnum = false;
		}
		if(this->lv_fromhstr[i][0]) {
			new_state.all_fromhstr = false;
		}
		if(this->lv_tohstr[i][0]) {
			new_state.all_tohstr = false;
		}
		if(this->lv_bothhstr[i][0]) {
			new_state.all_bothhstr = false;
		}
		if(this->lv_vlan_set[i]) {
			new_state.all_vlan = false;
		}
		if(this->lv_siptypes[i]) {
			new_state.all_siptypes = false;
		}
	}
	new_state.all_addr = new_state.all_saddr && new_state.all_daddr && new_state.all_bothaddr;
	new_state.all_num = new_state.all_srcnum && new_state.all_dstnum && new_state.all_bothnum;
	new_state.all_hstr = new_state.all_fromhstr && new_state.all_tohstr && new_state.all_bothhstr;
	new_state.all_all = new_state.all_addr && new_state.all_bothport && new_state.all_num && new_state.all_hstr && new_state.all_vlan && new_state.all_siptypes;
	this->state = new_state;
}

string livesnifferfilter_s::getStringState() {
	ostringstream outStr;
	outStr << "sip type: ";
	if(this->state.all_siptypes) {
		outStr << "all";
	} else {
		int counter = 0;
		for(int i = 0; i < MAXLIVEFILTERS; i++) {
			if(this->lv_siptypes[i]) {
				if(counter) {
					outStr << ",";
				}
				outStr << (this->lv_siptypes[i] == INVITE ? 'I' :
					   this->lv_siptypes[i] == REGISTER ? 'R' :
					   this->lv_siptypes[i] == OPTIONS ? 'O' :
					   this->lv_siptypes[i] == SUBSCRIBE ? 'S' :
					   this->lv_siptypes[i] == MESSAGE ? 'M' :
					   this->lv_siptypes[i] == NOTIFY ? 'N' : '-');
				++counter;
			}
		}
	}
	outStr << " ;   ";
	for(int pass = 1; pass <= 3; pass++) {
		if(!(pass == 1 ? this->state.all_saddr :
		     pass == 2 ? this->state.all_daddr :
				 this->state.all_bothaddr)) {
			vmIP *addr = pass == 1 ? this->lv_saddr :
				     pass == 2 ? this->lv_daddr :
						 this->lv_bothaddr;
			int counter = 0;
			for(int i = 0; i < MAXLIVEFILTERS; i++) {
				if(addr[i].isSet()) {
					if(counter) {
						outStr << ", ";
					} else {
						outStr << (pass == 1 ? "source address" :
							   pass == 2 ? "dest. address" :
							   pass == 3 ? "address" : 
								       "")
						       << ": ";
					}
					outStr << addr[i].getString();
					++counter;
				}
			}
			if(counter) {
				outStr << " ;   ";
			}
		}
	}
	for(int pass = 1; pass <= 3; pass++) {
		if(!(pass == 1 ? this->state.all_srcnum :
		     pass == 2 ? this->state.all_dstnum :
				 this->state.all_bothnum)) {
			char (*num)[MAXLIVEFILTERSCHARS] = pass == 1 ? this->lv_srcnum :
							   pass == 2 ? this->lv_dstnum :
								       this->lv_bothnum;
			int counter = 0;
			for(int i = 0; i < MAXLIVEFILTERS; i++) {
				if(num[i][0]) {
					if(counter) {
						outStr << ", ";
					} else {
						outStr << (pass == 1 ? "source number" :
							   pass == 2 ? "dest. number" :
							   pass == 3 ? "number" : 
								       "")
						       << ": ";
					}
					outStr << num[i];
					++counter;
				}
			}
			if(counter) {
				outStr << " ;   ";
			}
		}
	}
	for(int pass = 1; pass <= 3; pass++) {
		if(!(pass == 1 ? this->state.all_fromhstr :
		     pass == 2 ? this->state.all_tohstr :
				 this->state.all_bothhstr)) {
			char (*hstr)[MAXLIVEFILTERSCHARS] = pass == 1 ? this->lv_fromhstr :
							    pass == 2 ? this->lv_tohstr :
									this->lv_bothhstr;
			int counter = 0;
			for(int i = 0; i < MAXLIVEFILTERS; i++) {
				if(hstr[i][0]) {
					if(counter) {
						outStr << ", ";
					} else {
						outStr << (pass == 1 ? "from header" :
							   pass == 2 ? "to header" :
							   pass == 3 ? "from/to header" :
								       "")
						       << ": ";
					}
					outStr << hstr[i];
					++counter;
				}
			}
			if(counter) {
				outStr << " ;   ";
			}
		}
	}
	string result = outStr.str();
	while(result.length() && (result[result.length() - 1] == ' ' || result[result.length() - 1] == ';')) {
		result = result.substr(0, result.length() - 1);
	}
	return(result);
}

void updateLivesnifferfilters() {
	livesnifferfilter_use_siptypes_s new_livesnifferfilterUseSipTypes;
	memset(&new_livesnifferfilterUseSipTypes, 0, sizeof(new_livesnifferfilterUseSipTypes));
	if(usersniffer.size()) {
		global_livesniffer = 1;
		map<unsigned int, livesnifferfilter_s*>::iterator usersnifferIT;
		for(usersnifferIT = usersniffer.begin(); usersnifferIT != usersniffer.end(); ++usersnifferIT) {
			usersnifferIT->second->updateState();
			if(usersnifferIT->second->state.all_siptypes) {
				new_livesnifferfilterUseSipTypes.u_invite = true;
				new_livesnifferfilterUseSipTypes.u_register = true;
				new_livesnifferfilterUseSipTypes.u_options = true;
				new_livesnifferfilterUseSipTypes.u_subscribe = true;
				new_livesnifferfilterUseSipTypes.u_message = true;
				new_livesnifferfilterUseSipTypes.u_notify = true;
			} else {
				for(int i = 0; i < MAXLIVEFILTERS; i++) {
					if(usersnifferIT->second->lv_siptypes[i]) {
						switch(usersnifferIT->second->lv_siptypes[i]) {
						case INVITE:
							new_livesnifferfilterUseSipTypes.u_invite = true;
							break;
						case REGISTER:
							new_livesnifferfilterUseSipTypes.u_register = true;
							break;
						case OPTIONS:
							new_livesnifferfilterUseSipTypes.u_options = true;
							break;
						case SUBSCRIBE:
							new_livesnifferfilterUseSipTypes.u_subscribe = true;
							break;
						case MESSAGE:
							new_livesnifferfilterUseSipTypes.u_message = true;
							break;
						case NOTIFY:
							new_livesnifferfilterUseSipTypes.u_notify = true;
							break;
						}
					}
				}
			}
		}
	} else {
		global_livesniffer = 0;
	}
	livesnifferfilterUseSipTypes = new_livesnifferfilterUseSipTypes;
	/*
	cout << "livesnifferfilterUseSipTypes" << endl;
	if(livesnifferfilterUseSipTypes.u_invite) cout << "INVITE" << endl;
	if(livesnifferfilterUseSipTypes.u_register) cout << "REGISTER" << endl;
	if(livesnifferfilterUseSipTypes.u_options) cout << "OPTIONS" << endl;
	if(livesnifferfilterUseSipTypes.u_subscribe) cout << "SUBSCRIBE" << endl;
	if(livesnifferfilterUseSipTypes.u_message) cout << "MESSAGE" << endl;
	if(livesnifferfilterUseSipTypes.u_notify) cout << "NOTIFY" << endl;
	*/
}

bool cmpCallBy_destroy_call_at(Call* a, Call* b) {
	return(a->destroy_call_at < b->destroy_call_at);   
}
bool cmpCallBy_first_packet_time(Call* a, Call* b) {
	return(a->first_packet_time_us < b->first_packet_time_us);
}


ManagerClientThread::ManagerClientThread(sClientInfo client, const char *type, const char *command, int commandLength) {
	this->client = client;
	this->type = type;
	if(commandLength) {
		this->command = string(command, commandLength);
	} else {
		this->command = command;
	}
	this->finished = false;
	this->_sync_responses = 0;
}

void ManagerClientThread::run() {
	unsigned int counter = 0;
	bool disconnect = false;
	int flag = 0;
	setsockopt(client.handler, IPPROTO_TCP, TCP_NODELAY, (char *) &flag, sizeof(int));
	int flushBuffLength = 1000;
	char *flushBuff = new FILE_LINE(13019) char[flushBuffLength];
	memset(flushBuff, '_', flushBuffLength - 1);
	flushBuff[flushBuffLength - 1] = '\n';
	while(true && !is_terminating_without_error() && !disconnect) {
		string rsltString;
		this->lock_responses();
		if(this->responses.size()) {
			rsltString = this->responses.front();
			this->responses.pop();
		}
		this->unlock_responses();
		if(!rsltString.empty()) {
			if(send(client.handler, rsltString.c_str(), rsltString.length(), 0) == -1) {
				disconnect = true;
			} else {
				if(sverb.screen_popup_syslog) {
					syslog(LOG_INFO, "SCREEN_POPUP - ok send string: dest: %s data: %s", 
					       client.ip.getString().c_str(),
					       rsltString.c_str());
				}
				send(client.handler, flushBuff, flushBuffLength, 0);
			}
		}
		++counter;
		if((counter % 5) == 0 && !disconnect) {
			if(send(client.handler, "ping\n", 5, 0) == -1) {
				disconnect = true;
			}
		}
		USLEEP(100000);
	}
	close(client.handler);
	finished = true;
	delete [] flushBuff;
}

ManagerClientThread_screen_popup::ManagerClientThread_screen_popup(sClientInfo client, const char *command, int commandLength) 
 : ManagerClientThread(client, "screen_popup", command, commandLength) {
	auto_popup = false;
	non_numeric_caller_id = false;
}

bool ManagerClientThread_screen_popup::parseCommand() {
	ClientThreads.cleanup();
	return(this->parseUserPassword());
}

void ManagerClientThread_screen_popup::onCall(int sipResponseNum, const char *callerName, const char *callerNum, const char *calledNum,
					      vmIP sipSaddr, vmIP sipDaddr,
					      const char *screenPopupFieldsString) {
	if(sverb.screen_popup) {
		cout << "** - sip response : " << sipResponseNum << endl;
		cout << "** - called num : " << calledNum << endl;
		cout << "** - src ip : " << sipSaddr.getString() << endl;
		cout << "** - reg_match : " << reg_match(calledNum, this->dest_number.empty() ? this->username.c_str() : this->dest_number.c_str(), __FILE__, __LINE__) << endl;
		cout << "** - check ip : " << this->src_ip.checkIP(sipSaddr) << endl;
		cout << "** - screenPopupFieldsString : " << screenPopupFieldsString << endl;
	}
	if(!(reg_match(calledNum, this->dest_number.empty() ? this->username.c_str() : this->dest_number.c_str(), __FILE__, __LINE__) &&
	     (this->non_numeric_caller_id ||
	      this->isNumericId(calledNum)) &&
	     this->src_ip.checkIP(sipSaddr))) {
		return;
	}
	if(this->regex_check_calling_number.size()) {
		bool callerNumOk = false;
		for(size_t i = 0; i < this->regex_check_calling_number.size(); i++) {
			if(reg_match(callerNum, this->regex_check_calling_number[i].c_str(), __FILE__, __LINE__)) {
				callerNumOk = true;
				break;
			}
		}
		if(!callerNumOk) {
			return;
		}
	}
	char rsltString[4096];
	string callerNumStr = callerNum;
	for(size_t i = 0; i < this->regex_replace_calling_number.size(); i++) {
		string temp = reg_replace(callerNumStr.c_str(), 
					  this->regex_replace_calling_number[i].pattern.c_str(), 
					  this->regex_replace_calling_number[i].replace.c_str(),
					  __FILE__, __LINE__);
		if(!temp.empty()) {
			callerNumStr = temp;
		}
	}
	snprintf(rsltString, sizeof(rsltString),
		"call_data: "
		"sipresponse:[[%i]] "
		"callername:[[%s]] "
		"caller:[[%s]] "
		"called:[[%s]] "
		"sipcallerip:[[%s]] "
		"sipcalledip:[[%s]] "
		"fields:[[%s]]\n",
		sipResponseNum,
		callerName,
		callerNumStr.c_str(),
		calledNum,
		sipSaddr.getString().c_str(),
		sipDaddr.getString().c_str(),
		screenPopupFieldsString);
	if(sverb.screen_popup_syslog) {
		syslog(LOG_INFO, "SCREEN_POPUP - send string: username: %s dest: %s data: %s", 
		       username.c_str(),
		       client.ip.getString().c_str(),
		       rsltString);
	}
	this->lock_responses();
	this->responses.push(rsltString);
	this->unlock_responses();
}

bool ManagerClientThread_screen_popup::parseUserPassword() {
	char user[128];
	char password[128];
	char key[128];
	sscanf(command.c_str(), "login_screen_popup %s %s %s", user, password, key);
	string password_md5 = GetStringMD5(password);
	SqlDb *sqlDb = createSqlObject();
	sqlDb->query(
		string(
		"select u.username,\
			u.name,\
			u.dest_number,\
			u.allow_change_settings,\
			p.name as profile_name,\
			p.auto_popup,\
			p.show_ip,\
			p.popup_on,\
			p.non_numeric_caller_id,\
			p.regex_calling_number,\
			p.src_ip_whitelist,\
			p.src_ip_blacklist,\
			p.app_launch,\
			p.app_launch_args_or_url,\
			p.status_line,\
			p.popup_title\
		 from screen_popup_users u\
		 join screen_popup_profile p on (p.id=u.profile_id)\
		 where username=") +
		sqlEscapeStringBorder(user) +
		" and password=" + 
		sqlEscapeStringBorder(password_md5));
	SqlDb_row row = sqlDb->fetchRow();
	char rsltString[4096];
	bool rslt;
	if(row) {
		rslt = true;
		username = row["username"];
		name = row["name"];
		dest_number = row["dest_number"];
		allow_change_settings = atoi(row["allow_change_settings"].c_str());
		profile_name = row["profile_name"];
		auto_popup = atoi(row["auto_popup"].c_str());
		show_ip = atoi(row["show_ip"].c_str());
		popup_on = row["popup_on"];
		non_numeric_caller_id = atoi(row["non_numeric_caller_id"].c_str());
		if(!row["regex_calling_number"].empty()) {
			vector<string> items = split(row["regex_calling_number"].c_str(), split("\r|\n", "|"), true);
			for(size_t i = 0; i < items.size(); i++) {
				vector<string> itemItems = split(items[i].c_str(), "|", true);
				if(itemItems.size() == 2) {
					this->regex_replace_calling_number.push_back(RegexReplace(itemItems[0].c_str(), itemItems[1].c_str()));
				} else {
					this->regex_check_calling_number.push_back(itemItems[0]);
				}
			}
		}
		src_ip.addWhite(row["src_ip_whitelist"].c_str());
		src_ip.addBlack(row["src_ip_blacklist"].c_str());
		app_launch = row["app_launch"];
		app_launch_args_or_url = row["app_launch_args_or_url"];
		status_line = row["status_line"];
		popup_title = row["popup_title"];
		if(!opt_php_path[0]) {
			rslt = false;
			strcpy(rsltString, "login_failed error:[[Please set php_path parameter in voipmonitor.conf.]]\n");
		} else {
			string cmd = string("php ") + opt_php_path + "/php/run.php checkScreenPopupLicense -k " + key;
			FILE *fp = popen(cmd.c_str(), "r"); // TODO: create an alternative using vm_pexec
			if(fp == NULL) {
				rslt = false;
				strcpy(rsltString, "login_failed error:[[Failed to run php checkScreenPopupLicense.]]\n");
			} else {
				char rsltFromPhp[1024];
				if(!fgets(rsltFromPhp, sizeof(rsltFromPhp) - 1, fp)) {
					rslt = false;
					strcpy(rsltString, "login_failed error:[[License check failed please contact support.]]\n");
				} else if(!strncmp(rsltFromPhp, "error: ", 7)) {
					rslt = false;
					strcpy(rsltString, (string("login_failed error:[[") + (rsltFromPhp + 7) + "]]\n").c_str());
				} else {
					char key[1024];
					int maxClients;
					if(sscanf(rsltFromPhp, "key: %s max_clients: %i", key, &maxClients) == 2) {
						if(maxClients && ClientThreads.getCount() >= maxClients) {
							rslt = false;
							strcpy(rsltString, "login_failed error:[[Maximum connection limit reached.]]\n");
						} else {
							snprintf(rsltString, sizeof(rsltString),
								"login_ok "
								"auto_popup:[[%i]] "
								"popup_on_200:[[%i]] "
								"popup_on_18:[[%i]] "
								"show_ip:[[%i]] "
								"app_launch:[[%s]] "
								"args_or_url:[[%s]] "
								"status_line:[[%s]] "
								"key:[[%s]] "
								"allow_change_settings:[[%i]] "
								"popup_title:[[%s]]\n", 
								auto_popup, 
								popup_on == "200" || popup_on == "183/180_200",
								popup_on == "183/180" || popup_on == "183/180_200",
								show_ip, 
								app_launch.c_str(), 
								app_launch_args_or_url.c_str(), 
								status_line.c_str(),
								key, 
								allow_change_settings,
								popup_title.c_str());
						}
					} else {
						rslt = false;
							strcpy(rsltString, "login_failed error:[[License is invalid.]]\n");
					}
				}
				pclose(fp);
			}
		}
	} else {
		rslt = false;
		strcpy(rsltString, "login_failed error:[[Invalid user or password.]]\n");
	}
	delete sqlDb;
	send(client.handler, rsltString, strlen(rsltString), 0);
	return(rslt);
}

bool ManagerClientThread_screen_popup::isNumericId(const char *id) {
	while(*id) {
		if(!isdigit(*id) &&
		   *id != ' ' &&
		   *id != '+') {
			return(false);
		}
		++id;
	}
	return(true);
}

ManagerClientThreads::ManagerClientThreads() {
	_sync_client_threads = 0;
}
	
void ManagerClientThreads::add(ManagerClientThread *clientThread) {
	this->lock_client_threads();
	clientThreads.push_back(clientThread);
	this->unlock_client_threads();
	this->cleanup();
}

void ManagerClientThreads::onCall(const char *call_id,
				  int sipResponseNum, const char *callerName, const char *callerNum, const char *calledNum,
				  vmIP sipSaddr, vmIP sipDaddr,
				  const char *screenPopupFieldsString) {
	if(sverb.screen_popup_syslog) {
		syslog(LOG_INFO, "SCREEN_POPUP - call: call_id: %s response: %i caller: %s called: %s callername: %s from: %s to: %s fields: %s", 
		       call_id,
		       sipResponseNum,
		       callerNum,
		       calledNum,
		       callerName,
		       sipSaddr.getString().c_str(),
		       sipDaddr.getString().c_str(),
		       screenPopupFieldsString);
	}
	this->lock_client_threads();
	vector<ManagerClientThread*>::iterator iter;
	for(iter = this->clientThreads.begin(); iter != this->clientThreads.end(); ++iter) {
		(*iter)->onCall(sipResponseNum, callerName, callerNum, calledNum, 
				sipSaddr, sipDaddr, 
				screenPopupFieldsString);
	}
	this->unlock_client_threads();
}

void ManagerClientThreads::cleanup() {
	this->lock_client_threads();
	for(int i = this->clientThreads.size() - 1; i >=0; i--) {
		ManagerClientThread *ct = this->clientThreads[i];
		if(ct->isFinished()) {
			delete ct;
			this->clientThreads.erase(this->clientThreads.begin() + i);
			
		}
	}
	this->unlock_client_threads();
}

int ManagerClientThreads::getCount() {
	this->lock_client_threads();
	int count = this->clientThreads.size();
	this->unlock_client_threads();
	return(count);
}

int Mgmt_getversion(Mgmt_params* params) {
	if (params->task == params->mgmt_task_DoInit) {
		commandAndHelp ch[] = {
			{"getversion", "return the version of the sniffer", true},
			{"getbuild", "return the build number of the sniffer", true},
			{"getverbuild", "return the version and build number of the sniffer", true},
			{NULL, NULL}
		};
		params->registerCommand(ch);
		return(0);
	}
	if(strstr(params->buf, "getversion") != NULL) {
		return(params->sendString(RTPSENSOR_VERSION));
	} else if(strstr(params->buf, "getbuild") != NULL) {
		return(params->sendString(RTPSENSOR_BUILD_NUMBER));
	} else if(strstr(params->buf, "getverbuild") != NULL) {
		return(params->sendString(RTPSENSOR_VERSION + string(" (") + RTPSENSOR_BUILD_NUMBER + ")"));
	}
	return(0);
}

int Mgmt_cleanup_calls(Mgmt_params* params) {
	if (params->task == params->mgmt_task_DoInit) {
		params->registerCommand("cleanup_calls", "clean calls");
		return(0);
	}
	calltable->cleanup_calls(true);
	return(params->sendString("ok"));
}

int Mgmt_dtls_queue_cleanup(Mgmt_params* params) {
	if (params->task == params->mgmt_task_DoInit) {
		params->registerCommand("dtls_queue_cleanup", "clean dtls queue");
		return(0);
	}
	extern void dtls_queue_cleanup();
	dtls_queue_cleanup();
	return(params->sendString("ok"));
}

int Mgmt_cleanup_registers(Mgmt_params* params) {
	if (params->task == params->mgmt_task_DoInit) {
		params->registerCommand("cleanup_registers", "clean registers");
		return(0);
	}
	char parameter[100] = "";
	sscanf(params->buf + params->command.length() + 1, "%s", parameter);
	bool force = parameter[0] && 
		     (!strncasecmp(parameter, "closeall", 8) ||
		      !strncasecmp(parameter, "force", 5));
	calltable->cleanup_registers(force);
	return(params->sendString("ok"));
}

int Mgmt_expire_registers(Mgmt_params* params) {
	if (params->task == params->mgmt_task_DoInit) {
		params->registerCommand("expire_registers", "expire registers");
		return(0);
	}
	extern int opt_sip_register;
	if(enable_register_engine) {
		char parameter[100] = "";
		sscanf(params->buf + params->command.length() + 1, "%s", parameter);
		bool force = parameter[0] && !strncasecmp(parameter, "force", 5);
		extern Registers registers;
		registers.cleanup(force);
		registers.registers_failed.cleanup(force);
	}
	return(params->sendString("ok"));
}

int Mgmt_cleanup_tcpreassembly(Mgmt_params* params) {
	if (params->task == params->mgmt_task_DoInit) {
		params->registerCommand("cleanup_tcpreassembly", "clean tcpreassembly");
		return(0);
	}
	extern TcpReassemblySip tcpReassemblySip;
	tcpReassemblySip.clean();
	return(params->sendString("ok"));
}

int Mgmt_destroy_close_calls(Mgmt_params* params) {
	if (params->task == params->mgmt_task_DoInit) {
		params->registerCommand("destroy_close_calls", "destroy close calls");
		return(0);
	}
	calltable->destroyCallsIfPcapsClosed();
	return(params->sendString("ok"));
}

int Mgmt_is_register_new(Mgmt_params* params) {
	if (params->task == params->mgmt_task_DoInit) {
		params->registerCommand("is_register_new", "return status of the sip registration");
		return(0);
	}
	extern int opt_sip_register;
	return(params->sendString(opt_sip_register == 2 ? "no" : "ok"));
}

int Mgmt_totalcalls(Mgmt_params* params) {
	if (params->task == params->mgmt_task_DoInit) {
		params->registerCommand("totalcalls", "return the number of total calls", true);
		return(0);
	}
	return(params->sendString(calls_counter));
}

int Mgmt_totalregisters(Mgmt_params* params) {
	if (params->task == params->mgmt_task_DoInit) {
		params->registerCommand("totalregisters", "return the number of total registers");
		return(0);
	}
	extern Registers registers;
	return(params->sendString(registers.getCount()));
}

int Mgmt_listregisters(Mgmt_params* params) {
	if (params->task == params->mgmt_task_DoInit) {
		params->registerCommand("listregisters", "return the list of registers. Possible params:");
		return(0);
	}
	string rslt_data;
	char *pointer;
	if((pointer = strchr(params->buf, '\n')) != NULL) {
		*pointer = 0;
	}
	extern Registers registers;
	rslt_data = registers.getDataTableJson(params->buf + params->command.length() + 1, &params->zip);
	return(params->sendString(&rslt_data));
}

int Mgmt_list_sip_msg(Mgmt_params* params) {
	if (params->task == params->mgmt_task_DoInit) {
		params->registerCommand("list_sip_msg", "return the list of options. Possible params:");
		return(0);
	}
	string rslt_data;
	char *pointer;
	if((pointer = strchr(params->buf, '\n')) != NULL) {
		*pointer = 0;
	}
	extern cSipMsgRelations *sipMsgRelations;
	if(sipMsgRelations) {
		rslt_data = sipMsgRelations->getDataTableJson(params->buf + params->command.length() + 1, &params->zip);
		return(params->sendString(&rslt_data));
	}
	return(0);
}

int Mgmt_list_history_sip_msg(Mgmt_params* params) {
	if (params->task == params->mgmt_task_DoInit) {
		params->registerCommand("list_history_sip_msg", "return the list of history options. Possible params:");
		return(0);
	}
	string rslt_data;
	char *pointer;
	if((pointer = strchr(params->buf, '\n')) != NULL) {
		*pointer = 0;
	}
	extern cSipMsgRelations *sipMsgRelations;
	if(sipMsgRelations) {
		rslt_data = sipMsgRelations->getHistoryDataJson(params->buf + params->command.length() + 1, &params->zip);
		return(params->sendString(&rslt_data));
	}
	return(0);
}

int Mgmt_cleanupregisters(Mgmt_params* params) {
	if (params->task == params->mgmt_task_DoInit) {
		params->registerCommand("cleanupregisters", "clean registers. Possible params:");
		return(0);
	}
	char *pointer;
	if((pointer = strchr(params->buf, '\n')) != NULL) {
		*pointer = 0;
	}
	extern Registers registers;
	registers.cleanupByJson(params->buf + strlen("cleanupregisters") + 1);
	return(params->sendString("ok"));
}

int Mgmt_help(Mgmt_params* params) {
	if (params->task == params->mgmt_task_DoInit) {
		params->registerCommand("help", "print command's help");
		return(0);
	}
	std::map<string, sMgmtCmdsReg>::iterator MgmtItem;
	char *startOfParam = strpbrk(params->buf, " ");
	stringstream sendBuff;
	if (startOfParam) {
		startOfParam++;
		char *endOfParam = strpbrk(startOfParam, " \r\n\t");
		if (!endOfParam) {
			syslog(LOG_ERR, "Can't determine the param's end.");
			cerr << "Can't determine the param's end." << endl;
			return(-1);
		}
		string cmdStr (startOfParam, endOfParam);
		MgmtItem = MgmtCmdsRegTable.find(cmdStr);
		if (MgmtItem != MgmtCmdsRegTable.end()) {
			if (MgmtItem->second.help.length()) {
				sendBuff << MgmtItem->second.command << " ... " << MgmtItem->second.help << "." << endl << endl;
			}
		} else {
			sendBuff << "Command " << cmdStr << " not found." << endl << endl;
		}
	} else {
		sendBuff << "List of commands:" << endl << endl;
		for (MgmtItem = MgmtCmdsRegTable.begin(); MgmtItem != MgmtCmdsRegTable.end(); MgmtItem++) {
			if (MgmtItem->second.help.length()) {
				sendBuff << MgmtItem->second.command << " ... " << MgmtItem->second.help << "." << endl << endl;
			}
		}
	}
	string sendbuff = sendBuff.str();
	return(params->sendString(&sendbuff));
}

int Mgmt_check_filesindex(Mgmt_params *params) {
	if (params->task == params->mgmt_task_DoInit) {
		params->registerCommand("check_filesindex", "check files indexing");
		return(0);
	}
	char sendbuf[BUFSIZE];
	if(is_enable_cleanspool()) {
		snprintf(sendbuf, BUFSIZE, "starting checking indexing please wait...");
		params->sendString(sendbuf);
		CleanSpool::run_check_filesindex();
		snprintf(sendbuf, BUFSIZE, "done\r\n");
	} else {
		strcpy(sendbuf, "cleanspool is disable\r\n");
	}
	return(params->sendString(sendbuf));
}

int Mgmt_reindexspool(Mgmt_params *params) {
	if (params->task == params->mgmt_task_DoInit) {
		params->registerCommand("reindexspool", "reindex spool directory");
		return(0);
	}
	string rslt;
	if(is_enable_cleanspool()) {
		CleanSpool::run_reindex_spool();
		rslt = "done\r\n";
	} else {
		rslt = "cleanspool is disable\r\n";
	}
	return(params->sendString(&rslt));
}

int Mgmt_printspool(Mgmt_params *params) {
	if (params->task == params->mgmt_task_DoInit) {
		params->registerCommand("printspool", "print info about spool directory", true);
		return(0);
	}
	string rslt;
	if(is_enable_cleanspool()) {
		rslt = CleanSpool::run_print_spool(-1, strstr(params->buf, "refresh") != NULL);
	} else {
		rslt = "cleanspool is disable\r\n";
	}
	return(params->sendString(&rslt));
}

int Mgmt_listopentars(Mgmt_params *params) {
	if (params->task == params->mgmt_task_DoInit) {
		commandAndHelp ch[] = {
			{"listopentars", "print lists of open tars"},
			{"listtartimemap", "print lists of tartimemap"},
			{NULL, NULL}
		};
		params->registerCommand(ch);
		return(0);
	}
	string rslt;
	extern TarQueue *tarQueue[2];
	for(int i = 0; i < 2; i++) {
		if(tarQueue[i]) {
			list<string> rslt_list;
			if(strstr(params->buf, "listopentars")) {
				rslt_list = tarQueue[i]->listOpenTars();
			} else if(strstr(params->buf, "listtartimemap")) {
				rslt_list = tarQueue[i]->listTartimemap(true);
			}
			for(list<string>::iterator iter = rslt_list.begin(); iter != rslt_list.end(); iter++) {
				rslt += *iter + "\n";
			}
		}
	}
	return(params->sendString(&rslt));
}

int Mgmt_reindexfiles(Mgmt_params *params) {
	if (params->task == params->mgmt_task_DoInit) {
		commandAndHelp ch[] = {
			{"reindexfiles", "starts the reindexing of the spool's files. 'reindexfiles' runs standard reindex"},
			{"reindexfiles_date", "runs reindex for entered DATE"},
			{"reindexfiles_datehour", "runs reindex for entered DATE HOUR"},
			{NULL, NULL}
		};
		params->registerCommand(ch);
		return(0);
	}
	char sendbuf[BUFSIZE];
	if(is_enable_cleanspool()) {
		char date[21];
		int hour;
		bool badParams = false;
		if(strstr(params->buf, "reindexfiles_datehour")) {
			if(sscanf(params->buf + strlen("reindexfiles_datehour") + 1, "%20s %i", date, &hour) != 2) {
				badParams = true;
			}
		} else if(strstr(params->buf, "reindexfiles_date")) {
			if(sscanf(params->buf + strlen("reindexfiles_date") + 1, "%20s", date) != 1) {
				badParams = true;
			}
		}
		if(badParams) {
			snprintf(sendbuf, BUFSIZE, "bad parameters");
			params->sendString(sendbuf);
			return -1;
		}
		snprintf(sendbuf, BUFSIZE, "starting reindexing please wait...");
		params->sendString(sendbuf);
		if(strstr(params->buf, "reindexfiles_datehour")) {
			CleanSpool::run_reindex_date_hour(date, hour);
		} else if(strstr(params->buf, "reindexfiles_date")) {
			CleanSpool::run_reindex_date(date);
		} else {
			CleanSpool::run_reindex_all("call from manager");
		}
		snprintf(sendbuf, BUFSIZE, "done\r\n");
	} else {
		strcpy(sendbuf, "cleanspool is disable\r\n");
	}
	return(params->sendString(sendbuf));
}

int Mgmt_listcalls(Mgmt_params *params) {
	if (params->task == params->mgmt_task_DoInit) {
		params->registerCommand("listcalls", "lists active calls");
		return(0);
	}
	if(calltable) {
		string rslt_data;
		char *pointer;
		if((pointer = strchr(params->buf, '\n')) != NULL) {
			*pointer = 0;
		}
		params->zip = false;
		char *jsonParams = params->buf + strlen("listcalls");
		while(*jsonParams == ' ') {
			++jsonParams;
		}
		rslt_data = calltable->getCallTableJson(jsonParams, &params->zip);
		return(params->sendString(&rslt_data));
	}
	return 0;
}

typedef struct {
	const char *cmd;
	volatile int *setVar;
	int setValue;
	const char *helpText;
} cmdData;

int Mgmt_offon(Mgmt_params *params) {
	static std::map<string, cmdData> cmdsDataTable;
	if (params->task == params->mgmt_task_DoInit) {
		extern volatile int partitionsServiceIsInProgress;
		cmdData cmdData_src[] = {
			{ "unblocktar", &opt_blocktarwrite, 0, "unblock tar files"},
			{ "blocktar", &opt_blocktarwrite, 1, "block tar files"},
			{ "unblockasync", &opt_blockasyncprocess, 0, "unblock async processing"},
			{ "blockasync", &opt_blockasyncprocess, 1, "block async processing"},
			{ "unblockprocesspacket", &opt_blockprocesspacket, 0, "unblock packet processing"},
			{ "blockprocesspacket", &opt_blockprocesspacket, 1, "block packet processing"},
			{ "unblockcleanupcalls", &opt_blockcleanupcalls, 0, "unblock cleanup calls"},
			{ "blockcleanupcalls", &opt_blockcleanupcalls, 1, "block cleanup calls"},
			{ "unsleepprocesspacket", &opt_sleepprocesspacket, 0, "unsleep packet processing"},
			{ "sleepprocesspacket", &opt_sleepprocesspacket, 1, "sleep packet processing"},
			{ "unblockqfile", &opt_blockqfile, 0, "unblock qfiles"},
			{ "blockqfile", &opt_blockqfile, 1, "block qfiles"},
			{ "unblock_alloc_stack", &opt_block_alloc_stack, 0, "unblock stack allocation"},
			{ "block_alloc_stack", &opt_block_alloc_stack, 1, "block stack allocation"},
			{ "disable_wait_for_ssl_key", &opt_disable_wait_for_ssl_key, 1, "disable wait for ssl key"},
			{ "enable_wait_for_ssl_key", &opt_disable_wait_for_ssl_key, 0, "enable wait for ssl key"},
			{ "disablecdr", &opt_nocdr, 1, "disable cdr creation"},
			{ "enablecdr", &opt_nocdr, 0, "enable cdr creation"},
			{ "unset_partitions_service", &partitionsServiceIsInProgress, 0, "unset flag partitions service is in progress"},
			{ "set_partitions_service", &partitionsServiceIsInProgress, 1, "set flag partitions service is in progress"}
		};
		for(unsigned i = 0; i < sizeof(cmdData_src) / sizeof(cmdData_src[0]); i++) {
			cmdsDataTable[cmdData_src[i].cmd] = cmdData_src[i];
		}
		std::map<string, cmdData>::iterator cmdItem;
		for (cmdItem = cmdsDataTable.begin(); cmdItem != cmdsDataTable.end(); cmdItem++) {
			params->registerCommand(cmdItem->first.c_str(), cmdItem->second.helpText);
		}
		return(0);
	}
	char *endOfCmd = strpbrk(params->buf, " \r\n\t");
	string cmdStr = endOfCmd ? string(params->buf, endOfCmd) : params->buf;
	std::map<string, cmdData>::iterator cmdItem = cmdsDataTable.find(cmdStr);
	if (cmdItem != cmdsDataTable.end()) {
		* cmdItem->second.setVar = cmdItem->second.setValue;
	}
	return(0);
}

int Mgmt_creategraph(Mgmt_params *params) {
	if (params->task == params->mgmt_task_DoInit) {
		params->registerCommand("creategraph", "creates graphs");
		return(0);
	}
	checkRrdVersion(true);
	extern int vm_rrd_version;
	if(!vm_rrd_version) {
		return(params->sendString("missing rrdtool"));
	}
	int rslt = 0;
	string error;
	vector<string> args;
	string params_buf = params->buf;
	while(params_buf.size() && params_buf[params_buf.size() - 1] == '\n') {
		params_buf.resize(params_buf.size() - 1);
	}
	parse_cmd_str(params_buf.c_str(), &args);
	if(args.size() < 6) {
		if(verbosity > 0) {
			syslog(LOG_NOTICE, "parse_command creategraph too few arguments, passed%zu need at least 6!\n", args.size());
		}
		error = "Syntax: creategraph graph_type linuxTS_from linuxTS_to size_x_pixels size_y_pixels  [ slope-mode  [ icon-mode  [ color  [ dstfile ]]]]\n";
	}
	if(error.empty()) {
		// Arguments:
		//   0-creategraphs
		//   1-graph type
		//   2-at-style time from
		//   3-at-style time to
		//   4-total size x
		//   5-total size y
		//   [6-zaobleni hran(slope-mode)]
		//   [7-discard graphs legend (for sizes bellow 600x240)]
		//   [8-color]
		string fromTime = args[2];
		string toTime = args[3];
		int resx = atoi(args[4].c_str());
		int resy = atoi(args[5].c_str());
		bool slope = args.size() > 6 && args[6][0] == '1';
		bool icon = args.size() > 7 && args[7][0] == '1';
		string color = args.size() > 8 ? args[8] : "";
		string chart_type;
		string series_type;
		if(args[1] == "PSA") {
			chart_type = RRD_CHART_PS;
			series_type= RRD_CHART_SERIES_PSA;
		} else if(args[1] == "PSR") {
			chart_type = RRD_CHART_PS;
			series_type= RRD_CHART_SERIES_PSR;
		} else if(args[1] == "PSSR") {
			chart_type = RRD_CHART_PS;
			series_type= RRD_CHART_SERIES_PSSR;
		} else if(args[1] == "PSSM") {
			chart_type = RRD_CHART_PS;
			series_type= RRD_CHART_SERIES_PSSM;
		} else if(args[1] == "PSS") {
			chart_type = RRD_CHART_PS;
			series_type= RRD_CHART_SERIES_PSS;
		} else if(args[1] == "PSC") {
			chart_type = RRD_CHART_PS;
			series_type= RRD_CHART_SERIES_PSC;
		} else if(args[1] == "PS") {
			chart_type = RRD_CHART_PS;
		} else if(args[1] == "SQLq") {
			chart_type = RRD_CHART_SQL;
			series_type= RRD_CHART_SERIES_SQLq;
		} else if(args[1] == "SQLf") {
			chart_type = RRD_CHART_SQL;
			series_type= RRD_CHART_SERIES_SQLf;
		} else if(args[1] == "tCPU") {
			chart_type = RRD_CHART_tCPU;
		} else if(args[1] == "drop") {
			chart_type = RRD_CHART_drop;
		} else if(args[1] == "speed") {
			chart_type = RRD_CHART_speedmbs;
		} else if(args[1] == "heap") {
			chart_type = RRD_CHART_heap;
		} else if(args[1] == "calls") {
			chart_type = RRD_CHART_callscounter;
		} else if(args[1] == "tacCPU") {
			chart_type = RRD_CHART_tacCPU;
		} else if(args[1] == "memusage") {
			chart_type = RRD_CHART_memusage;
		} else if(args[1] == "loadavg") {
			chart_type = RRD_CHART_LA;
		} else {
			error =  "Error: Graph type " + args[1] + " isn't known\n\t"
				 "Graph types: PS PSC PSS PSSM PSSR PSR PSA SQLq SQLf tCPU drop speed heap calls tacCPU memusage\n";
			if(verbosity > 0) {
				syslog(LOG_NOTICE, "creategraph Error: Unrecognized graph type %s", args[1].c_str());
				syslog(LOG_NOTICE, "    Graph types: PS PSC PSS PSSM PSSR PSR PSA SQLq SQLf tCPU drop speed heap calls tacCPU memusage loadavg");
			}
		}
		if(!chart_type.empty()) {
			string createGraphCmd = rrd_chart_graphString(chart_type.c_str(),
								      series_type.c_str(),
								      NULL, fromTime.c_str(), toTime.c_str(),
								      color.c_str(), resx, resy,
								      slope, icon);
			if(!createGraphCmd.empty()) {
				createGraphCmd = string(RRDTOOL_CMD) + " " + createGraphCmd;
				RrdChartQueueItem *queueItem = new FILE_LINE(0) RrdChartQueueItem;
				queueItem->request_type = "graph";
				queueItem->rrd_cmd = createGraphCmd;
				rrd_add_to_queue(queueItem);
				while(!queueItem->completed) {
					USLEEP(10000);
				}
				if(!queueItem->error.empty()) {
					error = queueItem->error;
				} else {
					if(params->sendString((const char*)queueItem->result.data(), queueItem->result.size()) == -1) {
						if(verbosity > 0) {
							syslog(LOG_NOTICE, "sendvm: sending data problem");
						}
						rslt = -1;
					}
				}
				delete queueItem;
			}
		}
	}
	if(!error.empty()) {
		params->sendString(error);
		rslt = -1;
	}
	return(rslt);
}

int Mgmt_d_lc_for_destroy(Mgmt_params *params) {
	if (params->task == params->mgmt_task_DoInit) {
		params->registerCommand("d_lc_for_destroy", "d_lc_for_destroy");
		return(0);
	}

	ostringstream outStr;
	if(!calltable && !terminating) {
		outStr << "sniffer not initialized yet" << endl;
		return(params->sendString(&outStr));
	}
	if(calltable->calls_queue.size()) {
		Call *call;
		vector<Call*> vectCall;
		calltable->lock_calls_queue();
		for(size_t i = 0; i < calltable->calls_queue.size(); ++i) {
			call = calltable->calls_queue[i];
			if(call->typeIsNot(REGISTER) && call->destroy_call_at) {
				vectCall.push_back(call);
			}
		}
		if(vectCall.size()) {
			std::sort(vectCall.begin(), vectCall.end(), cmpCallBy_destroy_call_at);
			for(size_t i = 0; i < vectCall.size(); i++) {
				call = vectCall[i];
				CallBranch *c_branch = call->branch_main();
				outStr.width(15);
				outStr << c_branch->caller << " -> ";
				outStr.width(15);
				outStr << call->get_called(c_branch) << "  "
					<< sqlDateTimeString(call->calltime_s()) << "  ";
				outStr.width(6);
				outStr << call->duration_s() << "s  "
					<< sqlDateTimeString(call->destroy_call_at) << "  "
					<< call->fbasename;
				outStr << endl;
			}
		}
		calltable->unlock_calls_queue();
	}
	outStr << "-----------" << endl;
	return(params->sendString(&outStr));
}
int Mgmt_d_lc_bye(Mgmt_params *params) {
	if (params->task == params->mgmt_task_DoInit) {
		params->registerCommand("d_lc_bye", "d_lc_bye");
		return(0);
	}

	ostringstream outStr;
	if(!calltable && !terminating) {
		outStr << "sniffer not initialized yet" << endl;
		return(params->sendString(&outStr));
	}
	Call *call;
	vector<Call*> vectCall;
	calltable->lock_calls_listMAP();
	if(opt_call_id_alternative[0]) {
		for(list<Call*>::iterator callIT = calltable->calls_list.begin(); callIT != calltable->calls_list.end(); ++callIT) {
			call = *callIT;
			if(call->typeIsNot(REGISTER) && call->branch_main()->seenbye) {
				vectCall.push_back(call);
			}
		}
	} else {
		for(map<string, Call*>::iterator callMAPIT = calltable->calls_listMAP.begin(); callMAPIT != calltable->calls_listMAP.end(); ++callMAPIT) {
			call = callMAPIT->second;
			if(call->typeIsNot(REGISTER) && call->branch_main()->seenbye) {
				vectCall.push_back(call);
			}
		}
	}
	if(vectCall.size()) {
		std::sort(vectCall.begin(), vectCall.end(), cmpCallBy_destroy_call_at);
		for(size_t i = 0; i < vectCall.size(); i++) {
			call = vectCall[i];
			CallBranch *c_branch = call->branch_main();
			outStr.width(15);
			outStr << c_branch->caller << " -> ";
			outStr.width(15);
			outStr << call->get_called(c_branch) << "  "
				<< sqlDateTimeString(call->calltime_s()) << "  ";
			outStr.width(6);
			outStr << call->duration_s() << "s  "
				<< (call->destroy_call_at ? sqlDateTimeString(call->destroy_call_at) : "    -  -     :  :  ")  << "  "
				<< call->fbasename;
			outStr << endl;
		}
	}
	calltable->unlock_calls_listMAP();
	outStr << "-----------" << endl;
	return(params->sendString(&outStr));
}

int Mgmt_d_lc_all(Mgmt_params *params) {
	if (params->task == params->mgmt_task_DoInit) {
		params->registerCommand("d_lc_all", "d_lc_all");
		return(0);
	}

	ostringstream outStr;
	if(!calltable && !terminating) {
		outStr << "sniffer not initialized yet" << endl;
		return(params->sendString(&outStr));
	}
	Call *call;
	vector<Call*> vectCall;
	calltable->lock_calls_listMAP();
	if(opt_call_id_alternative[0]) {
		for(list<Call*>::iterator callIT = calltable->calls_list.begin(); callIT != calltable->calls_list.end(); ++callIT) {
			vectCall.push_back(*callIT);
		}
	} else {
		for(map<string, Call*>::iterator callMAPIT = calltable->calls_listMAP.begin(); callMAPIT != calltable->calls_listMAP.end(); ++callMAPIT) {
			vectCall.push_back(callMAPIT->second);
		}
	}
	if(vectCall.size()) {
		std::sort(vectCall.begin(), vectCall.end(), cmpCallBy_first_packet_time);
		for(size_t i = 0; i < vectCall.size(); i++) {
			call = vectCall[i];
			CallBranch *c_branch = call->branch_main();
			outStr.width(15);
			outStr << c_branch->caller << " -> ";
			outStr.width(15);
			outStr << call->get_called(c_branch) << "  "
				<< sqlDateTimeString(call->calltime_s()) << "  ";
			outStr.width(6);
			outStr << call->duration_s() << "s  "
				<< (call->destroy_call_at ? sqlDateTimeString(call->destroy_call_at) : "    -  -     :  :  ")  << "  ";
			outStr.width(3);
			outStr << c_branch->lastSIPresponseNum << "  "
				<< call->fbasename;
			outStr << endl;
		}
	}
	calltable->unlock_calls_listMAP();
	outStr << "-----------" << endl;
	return(params->sendString(&outStr));
}

int Mgmt_d_pointer_to_call(Mgmt_params *params) {
	if (params->task == params->mgmt_task_DoInit) {
		params->registerCommand("d_pointer_to_call", "d_pointer_to_call");
		return(0);
	}
	char fbasename[256];
	sscanf(params->buf, "d_pointer_to_call %s", fbasename);
	ostringstream outStr;
	calltable->lock_calls_listMAP();
	if(opt_call_id_alternative[0]) {
		for(list<Call*>::iterator callIT = calltable->calls_list.begin(); callIT != calltable->calls_list.end(); ++callIT) {
			if(!strcmp((*callIT)->fbasename, fbasename)) {
				outStr << "find in calltable->calls_list " << hex << (*callIT) << endl;
			}
		}
	} else {
		for(map<string, Call*>::iterator callMAPIT = calltable->calls_listMAP.begin(); callMAPIT != calltable->calls_listMAP.end(); ++callMAPIT) {
			if(!strcmp((callMAPIT->second)->fbasename, fbasename)) {
				outStr << "find in calltable->calls_list " << hex << (callMAPIT->second) << endl;
			}
		}
	}
	calltable->unlock_calls_listMAP();
	calltable->lock_calls_queue();
	for(deque<Call*>::iterator callIT = calltable->calls_queue.begin(); callIT != calltable->calls_queue.end(); ++callIT) {
		if(!strcmp((*callIT)->fbasename, fbasename)) {
			outStr << "find in calltable->calls_queue " << hex << (*callIT) << endl;
		}
	}
	calltable->unlock_calls_queue();
	return(params->sendString(&outStr));
}

int Mgmt_d_close_call(Mgmt_params *params) {
	if (params->task == params->mgmt_task_DoInit) {
		params->registerCommand("d_close_call", "d_close_call");
		return(0);
	}
	char fbasename[100];
	sscanf(params->buf, "d_close_call %s", fbasename);
	string rslt = fbasename + string(" missing");
	calltable->lock_calls_listMAP();
	if(opt_call_id_alternative[0]) {
		for(list<Call*>::iterator callIT = calltable->calls_list.begin(); callIT != calltable->calls_list.end(); ++callIT) {
			if(!strcmp((*callIT)->fbasename, fbasename)) {
				(*callIT)->force_close = true;
				rslt = fbasename + string(" close");
				break;
			}
		}
	} else {
		for(map<string, Call*>::iterator callMAPIT = calltable->calls_listMAP.begin(); callMAPIT != calltable->calls_listMAP.end(); ++callMAPIT) {
			if(!strcmp((callMAPIT->second)->fbasename, fbasename)) {
				(callMAPIT->second)->force_close = true;
				rslt = fbasename + string(" close");
				break;
			}
		}
	}
	calltable->unlock_calls_listMAP();
	rslt += "\n";
	return(params->sendString(&rslt));
}

int Mgmt_getipaccount(Mgmt_params *params) {
	if (params->task == params->mgmt_task_DoInit) {
		params->registerCommand("getipaccount", "getipaccount");
		return(0);
	}
	char sendbuf[BUFSIZE];
	u_int32_t uid = 0;
	sscanf(params->buf, "getipaccount %u", &uid);
	map<unsigned int, octects_live_t*>::iterator it = ipacc_live.find(uid);
	if(it != ipacc_live.end()) {
		snprintf(sendbuf, BUFSIZE, "%d", 1);
	} else {
		snprintf(sendbuf, BUFSIZE, "%d", 0);
	}
	return(params->sendString(sendbuf));
}

int Mgmt_ipaccountfilter(Mgmt_params *params) {
	if (params->task == params->mgmt_task_DoInit) {
		params->registerCommand("ipaccountfilter", "ipaccountfilter set");
		return(0);
	}

	string ipfilter;
	u_int32_t id = atol(params->buf + strlen("ipaccountfilter set "));
	char *pointToSeparatorBefereIpfilter = strchr(params->buf + strlen("ipaccountfilter set "), ' ');
	if(pointToSeparatorBefereIpfilter) {
		ipfilter = pointToSeparatorBefereIpfilter + 1;
	}
	if(!ipfilter.length() || ipfilter.find("ALL") != string::npos) {
		map<unsigned int, octects_live_t*>::iterator it = ipacc_live.find(id);
		octects_live_t* filter;
		if(it != ipacc_live.end()) {
			filter = it->second;
		} else {
			filter = new FILE_LINE(13007) octects_live_t;
			memset(CAST_OBJ_TO_VOID(filter), 0, sizeof(octects_live_t));
			filter->all = 1;
			filter->fetch_timestamp = time(NULL);
			ipacc_live[id] = filter;
			if(verbosity > 0) {
				cout << "START LIVE IPACC " << "id: " << id << " ipfilter: " << "ALL" << endl;
			}
		}
		return 0;
	} else {
		octects_live_t* filter;
		filter = new FILE_LINE(13008) octects_live_t;
		memset(CAST_OBJ_TO_VOID(filter), 0, sizeof(octects_live_t));
		filter->setFilter(ipfilter.c_str());
		filter->fetch_timestamp = time(NULL);
		ipacc_live[id] = filter;
		if(verbosity > 0) {
			cout << "START LIVE IPACC " << "id: " << id << " ipfilter: " << ipfilter << endl;
		}
	}
	return(0);
}

int Mgmt_stopipaccount(Mgmt_params *params) {
	if (params->task == params->mgmt_task_DoInit) {
		params->registerCommand("stopipaccount", "stopipaccount");
		return(0);
	}
	u_int32_t id = 0;
	sscanf(params->buf, "stopipaccount %u", &id);
	map<unsigned int, octects_live_t*>::iterator it = ipacc_live.find(id);
	if(it != ipacc_live.end()) {
		delete it->second;
		ipacc_live.erase(it);
		if(verbosity > 0) {
			cout << "STOP LIVE IPACC " << "id:" << id << endl;
		}
	}
	return 0;
}

int Mgmt_fetchipaccount(Mgmt_params *params) {
	if (params->task == params->mgmt_task_DoInit) {
		params->registerCommand("fetchipaccount", "fetchipaccount");
		return(0);
	}
	u_int32_t id = 0;
	sscanf(params->buf, "fetchipaccount %u", &id);
	map<unsigned int, octects_live_t*>::iterator it = ipacc_live.find(id);
	char sendbuf[1024];
	if(it == ipacc_live.end()) {
		strcpy(sendbuf, "stopped");
	} else {
		octects_live_t *data = it->second;
		snprintf(sendbuf, 1024, "%u;%llu;%u;%llu;%u;%llu;%u;%llu;%u;%llu;%u;%llu;%u",
				(unsigned int)time(NULL),
				data->dst_octects, data->dst_numpackets,
				data->src_octects, data->src_numpackets,
				data->voipdst_octects, data->voipdst_numpackets,
				data->voipsrc_octects, data->voipsrc_numpackets,
				data->all_octects, data->all_numpackets,
				data->voipall_octects, data->voipall_numpackets);
		data->fetch_timestamp = time(NULL);
	}
	return(params->sendString(sendbuf));
}

int Mgmt_getactivesniffers(Mgmt_params *params) {
	if (params->task == params->mgmt_task_DoInit) {
		params->registerCommand("getactivesniffers", "returns active sniffers");
		return(0);
	}
	__SYNC_LOCK(usersniffer_sync);
	string jsonResult = "[";
	map<unsigned int, livesnifferfilter_s*>::iterator usersnifferIT;
	int counter = 0;
	for(usersnifferIT = usersniffer.begin(); usersnifferIT != usersniffer.end(); usersnifferIT++) {
		if(counter) {
			jsonResult += ",";
		}
		char uid_str[10];
		snprintf(uid_str, sizeof(uid_str), "%i", usersnifferIT->first);
		jsonResult += "{\"uid\": \"" + string(uid_str) + "\"," +
			"\"state\":\"" + usersnifferIT->second->getStringState() + "\"}";
		++counter;
	}
	jsonResult += "]";
	__SYNC_UNLOCK(usersniffer_sync);
	return(params->sendString(&jsonResult));
}

int Mgmt_stoplivesniffer(Mgmt_params *params) {
	if (params->task == params->mgmt_task_DoInit) {
		params->registerCommand("stoplivesniffer", "stop live sniffer");
		return(0);
	}
	u_int32_t uid = 0;
	sscanf(params->buf, "stoplivesniffer %u", &uid);
	__SYNC_LOCK(usersniffer_sync);
	map<unsigned int, livesnifferfilter_s*>::iterator usersnifferIT = usersniffer.find(uid);
	if(usersnifferIT != usersniffer.end()) {
		delete usersnifferIT->second;
		usersniffer.erase(usersnifferIT);
		if(!usersniffer.size()) {
			global_livesniffer = 0;
		}
		updateLivesnifferfilters();
		if(verbosity > 0) {
			syslog(LOG_NOTICE, "stop livesniffer - uid: %u", uid);
		}
	}
	__SYNC_UNLOCK(usersniffer_sync);
	return 0;
}

int Mgmt_getlivesniffer(Mgmt_params *params) {
	if (params->task == params->mgmt_task_DoInit) {
		params->registerCommand("getlivesniffer", "returns running live sniffers");
		return(0);
	}
	char sendbuf[BUFSIZE];
	u_int32_t uid = 0;
	sscanf(params->buf, "getlivesniffer %u", &uid);
	__SYNC_LOCK(usersniffer_sync);
	map<unsigned int, livesnifferfilter_s*>::iterator usersnifferIT = usersniffer.find(uid);
	if(usersnifferIT != usersniffer.end()) {
		string parameters = trim_str((char*)usersnifferIT->second->parameters);
		if(usersnifferIT->second->timeout_s > 0 &&
		   parameters.length() && parameters[parameters.length() - 1] == '}') {
			parameters = parameters.substr(0, parameters.length() - 1) + 
				     ",\"timeout\":" + intToString(usersnifferIT->second->timeout_s) + 
				     ",\"time\":" + intToString(time(NULL) - usersnifferIT->second->created_at) +
				     "}";
		}
		snprintf(sendbuf, BUFSIZE, "%d %s", 1, parameters.c_str());
	} else {
		if(usersniffer_kill_reason[uid].empty()) {
			snprintf(sendbuf, BUFSIZE, "%d", 0);
		} else {
			JsonExport parameters;
			parameters.add("kill_reason", usersniffer_kill_reason[uid]);
			snprintf(sendbuf, BUFSIZE, "%d %s", 0, parameters.getJson().c_str());
			usersniffer_kill_reason.erase(uid);
		}
	}
	__SYNC_UNLOCK(usersniffer_sync);
	return(params->sendString(sendbuf));
}

int Mgmt_startlivesniffer(Mgmt_params *params) {
	if (params->task == params->mgmt_task_DoInit) {
		params->registerCommand("startlivesniffer", "starts live sniffing");
		return(0);
	}

	char parameters[10000] = "";
	sscanf(params->buf, "startlivesniffer %[^\n\r]", parameters);
	if(verbosity > 0) {
		syslog(LOG_NOTICE, "start livesniffer - parameters: %s", parameters);
	}
	JsonItem jsonParameters;
	jsonParameters.parse(parameters);
	__SYNC_LOCK(usersniffer_sync);
	unsigned int uid = atol(jsonParameters.getValue("uid").c_str());
	map<unsigned int, livesnifferfilter_s*>::iterator usersnifferIT = usersniffer.find(uid);
	livesnifferfilter_s* filter;
	if(usersnifferIT != usersniffer.end()) {
		filter = usersnifferIT->second;
	} else {
		filter = new FILE_LINE(0) livesnifferfilter_s;
		filter->parameters.add(parameters);
		usersniffer[uid] = filter;
		filter->uid = uid;
	}
	string filter_sensor_id = jsonParameters.getValue("filter_sensor_id");
	if(filter_sensor_id.length()) {
		vector<string> sensors_id = split(filter_sensor_id.c_str(), split(",|;| ", "|"), true);
		for(unsigned i = 0; i < sensors_id.size(); i++) {
			int sensor_id = atoi(sensors_id[i].c_str());
			filter->sensor_id.insert(sensor_id > 0 ? sensor_id : 0);
			filter->sensor_id_set = true;
		}
	}
	string filter_ip = jsonParameters.getValue("filter_ip");
	if(filter_ip.length()) {
		vector<string> ip = split(filter_ip.c_str(), split(",|;| ", "|"), true);
		for(unsigned i = 0; i < ip.size() && i < MAXLIVEFILTERS; i++) {
			if(!filter->lv_bothaddr[i].setFromString(ip[i].c_str()) && strchr(ip[i].c_str(), '/')) {
				try_ip_mask(filter->lv_bothaddr[i], filter->lv_bothmask[i], ip[i]);
			} else {
				filter->lv_bothmask[i].clear(~0);
			}
		}
	}
	string filter_port = jsonParameters.getValue("filter_port");
	if(filter_port.length()) {
		vector<string> port = split(filter_port.c_str(), split(",|;| ", "|"), true);
		for(unsigned i = 0; i < port.size() && i < MAXLIVEFILTERS; i++) {
			filter->lv_bothport[i].setFromString(port[i].c_str());
		}
	}
	string filter_number = jsonParameters.getValue("filter_number");
	if(filter_number.length()) {
		vector<string> number = split(filter_number.c_str(), split(",|;| ", "|"), true);
		for(unsigned i = 0; i < number.size() && i < MAXLIVEFILTERS; i++) {
			strcpy_null_term(filter->lv_bothnum[i], number[i].c_str());
		}
	}
	string filter_vlan = jsonParameters.getValue("filter_vlan");
	if(filter_vlan.length()) {
		vector<string> vlan = split(filter_vlan.c_str(), split(",|;| ", "|"), true);
		for(unsigned i = 0; i < vlan.size() && i < MAXLIVEFILTERS; i++) {
			filter->lv_vlan[i] = atoi(vlan[i].c_str());
			filter->lv_vlan_set[i] = true;
		}
	}
	string filter_header_type = jsonParameters.getValue("filter_header_type");
	string filter_header = jsonParameters.getValue("filter_header");
	if(filter_header_type.length() && filter_header.length()) {
		vector<string> header_type = split(filter_header_type.c_str(), split(",|;| ", "|"), true);
		bool from = false;
		bool to = false;
		for(unsigned i = 0; i < header_type.size(); i++) {
			if(header_type[i] == "F") {
				from = true;
			} else if(header_type[i] == "T") {
				to = true;
			}
		}
		if(from || to) {
			vector<string> header = split(filter_header.c_str(), split(",|;| ", "|"), true);
			for(unsigned i = 0; i < header.size() && i < MAXLIVEFILTERS; i++) {
				if(from && to) {
					strcpy_null_term(filter->lv_bothhstr[i], header[i].c_str());
				} else if(from) {
					strcpy_null_term(filter->lv_fromhstr[i], header[i].c_str());
				} else if(to) {
					strcpy_null_term(filter->lv_tohstr[i], header[i].c_str());
				}
			}
		}
	}
	string filter_sip_type = jsonParameters.getValue("filter_sip_type");
	if(filter_sip_type.length()) {
		vector<string> sip_type = split(filter_sip_type.c_str(), split(",|;| ", "|"), true);
		for(unsigned i = 0, j = 0; i < sip_type.size(); i++) {
			int sip_type_i = sip_type[i] == "I" ? INVITE :
				sip_type[i] == "R" ? REGISTER :
				sip_type[i] == "O" ? OPTIONS :
				sip_type[i] == "S" ? SUBSCRIBE :
				sip_type[i] == "M" ? MESSAGE :
				sip_type[i] == "N" ? NOTIFY :
				0;
			if(sip_type_i) {
				filter->lv_siptypes[j++] = sip_type_i;
			}
		}
	}
	int timeout = atoi(jsonParameters.getValue("timeout").c_str());
	if(timeout > 0) {
		filter->timeout_s = timeout;
	}
	string disable_timeout_warn_msg = jsonParameters.getValue("disable_live_sniffer_timeout_warning");
	filter->disable_timeout_warn_msg = disable_timeout_warn_msg == "true" ? true : false;
	updateLivesnifferfilters();
	SqlDb *sqlDb = createSqlObject();
	sqlDb->getTypeColumn(("livepacket_" + intToString(uid)).c_str(), NULL, true, true);
	delete sqlDb;
	global_livesniffer = 1;
	__SYNC_UNLOCK(usersniffer_sync);
	return(0);
}

int Mgmt_livefilter(Mgmt_params *params) {
	if (params->task == params->mgmt_task_DoInit) {
		params->registerCommand("livefilter", "set live filter. Syntax livefilter set PARAMS");
		return(0);
	}

	char search[1024] = "";
	char value[1024] = "";
	u_int32_t uid = 0;
	sscanf(params->buf, "livefilter set %u %s %[^\n\r]", &uid, search, value);
	if(verbosity > 0) {
		syslog(LOG_NOTICE, "set livesniffer - uid: %u search: %s value: %s", uid, search, value);
	}

	__SYNC_LOCK(usersniffer_sync);

	if(memmem(search, sizeof(search), "all", 3)) {
		global_livesniffer = 1;
		map<unsigned int, livesnifferfilter_s*>::iterator usersnifferIT = usersniffer.find(uid);
		livesnifferfilter_s* filter;
		if(usersnifferIT != usersniffer.end()) {
			filter = usersnifferIT->second;
		} else {
			filter = new FILE_LINE(13009) livesnifferfilter_s;
			usersniffer[uid] = filter;
		}
		updateLivesnifferfilters();
		__SYNC_UNLOCK(usersniffer_sync);
		return 0;
	}

	map<unsigned int, livesnifferfilter_s*>::iterator usersnifferIT = usersniffer.find(uid);
	livesnifferfilter_s* filter;
	if(usersnifferIT != usersniffer.end()) {
		filter = usersnifferIT->second;
	} else {
		filter = new FILE_LINE(13010) livesnifferfilter_s;
		usersniffer[uid] = filter;
	}

	if(strstr(search, "srcaddr")) {
		int i = 0;
		//reset filters
		for(i = 0; i < MAXLIVEFILTERS; i++) {
			filter->lv_saddr[i].clear();
			filter->lv_smask[i].clear(~0);
		}
		stringstream  data(value);
		string val;
		// read all argumens lkivefilter set saddr 123 345 244
		i = 0;
		while(i < MAXLIVEFILTERS and getline(data, val,' ')){
			global_livesniffer = 1;
			if (!filter->lv_saddr[i].setFromString(val.c_str()) && strchr(val.c_str(), '/'))
				try_ip_mask(filter->lv_saddr[i], filter->lv_smask[i], val);
			i++;
		}
		updateLivesnifferfilters();
	} else if(strstr(search, "dstaddr")) {
		int i = 0;
		//reset filters
		for(i = 0; i < MAXLIVEFILTERS; i++) {
			filter->lv_daddr[i].clear();
			filter->lv_dmask[i].clear(~0);
		}
		stringstream  data(value);
		string val;
		i = 0;
		// read all argumens livefilter set daddr 123 345 244
		while(i < MAXLIVEFILTERS and getline(data, val,' ')){
			global_livesniffer = 1;
			if (!filter->lv_daddr[i].setFromString(val.c_str()) && strchr(val.c_str(), '/'))
				try_ip_mask(filter->lv_daddr[i], filter->lv_dmask[i], val);
			i++;
		}
		updateLivesnifferfilters();
	} else if(strstr(search, "bothaddr")) {
		int i = 0;
		//reset filters
		for(i = 0; i < MAXLIVEFILTERS; i++) {
			filter->lv_bothaddr[i].clear();
			filter->lv_bothmask[i].clear(~0);
		}
		stringstream  data(value);
		string val;
		i = 0;
		// read all argumens livefilter set bothaddr 123 345 244
		while(i < MAXLIVEFILTERS and getline(data, val,' ')){
			global_livesniffer = 1;
			if (!filter->lv_bothaddr[i].setFromString(val.c_str()) && strchr(val.c_str(), '/'))
				try_ip_mask(filter->lv_bothaddr[i], filter->lv_bothmask[i], val);
			i++;
		}
		updateLivesnifferfilters();
	} else if(strstr(search, "bothport")) {
		int i;
		//reset filters
		for(i = 0; i < MAXLIVEFILTERS; i++) {
			filter->lv_bothport[i].clear();
		}
		stringstream  data(value);
		string val;
		i = 0;

		while(i < MAXLIVEFILTERS and getline(data, val,' ')){
			global_livesniffer = 1;
			filter->lv_bothport[i].setFromString(val.c_str());
			i++;
		}
		updateLivesnifferfilters();
	} else if(strstr(search, "srcnum")) {
		int i = 0;
		//reset filters
		for(i = 0; i < MAXLIVEFILTERS; i++) {
			filter->lv_srcnum[i][0] = '\0';
		}
		stringstream  data(value);
		string val;
		i = 0;
		// read all argumens livefilter set srcaddr 123 345 244
		while(i < MAXLIVEFILTERS and getline(data, val,' ')){
			global_livesniffer = 1;
			stringstream tmp;
			tmp << val;
			tmp >> filter->lv_srcnum[i];
			//cout << filter->lv_srcnum[i] << "\n";
			i++;
		}
		updateLivesnifferfilters();
	} else if(strstr(search, "dstnum")) {
		int i = 0;
		//reset filters
		for(i = 0; i < MAXLIVEFILTERS; i++) {
			filter->lv_dstnum[i][0] = '\0';
		}
		stringstream  data(value);
		string val;
		i = 0;
		// read all argumens livefilter set dstaddr 123 345 244
		while(i < MAXLIVEFILTERS and getline(data, val,' ')){
			global_livesniffer = 1;
			stringstream tmp;
			tmp << val;
			tmp >> filter->lv_dstnum[i];
			//cout << filter->lv_dstnum[i] << "\n";
			i++;
		}
		updateLivesnifferfilters();
	} else if(strstr(search, "bothnum")) {
		int i = 0;
		//reset filters
		for(i = 0; i < MAXLIVEFILTERS; i++) {
			filter->lv_bothnum[i][0] = '\0';
		}
		stringstream  data(value);
		string val;
		i = 0;
		// read all argumens livefilter set bothaddr 123 345 244
		while(i < MAXLIVEFILTERS and getline(data, val,' ')){
			global_livesniffer = 1;
			stringstream tmp;
			tmp << val;
			tmp >> filter->lv_bothnum[i];
			//cout << filter->lv_bothnum[i] << "\n";
			i++;
		}
		updateLivesnifferfilters();
	} else if(strstr(search, "fromhstr")) {
		int i = 0;
		//reset filters
		for(i = 0; i < MAXLIVEFILTERS; i++) {
			filter->lv_fromhstr[i][0] = '\0';
		}
		stringstream  data(value);
		string val;
		i = 0;
		// read all argumens livefilter set fromhstr 123 345 244
		while(i < MAXLIVEFILTERS and getline(data, val,' ')){
			global_livesniffer = 1;
			stringstream tmp;
			tmp << val;
			tmp >> filter->lv_fromhstr[i];
			//cout << filter->lv_fromhstr[i] << "\n";
			i++;
		}
		updateLivesnifferfilters();
	} else if(strstr(search, "tohstr")) {
		int i = 0;
		//reset filters
		for(i = 0; i < MAXLIVEFILTERS; i++) {
			filter->lv_tohstr[i][0] = '\0';
		}
		stringstream  data(value);
		string val;
		i = 0;
		// read all argumens livefilter set tohstr 123 345 244
		while(i < MAXLIVEFILTERS and getline(data, val,' ')){
			global_livesniffer = 1;
			stringstream tmp;
			tmp << val;
			tmp >> filter->lv_tohstr[i];
			//cout << filter->lv_tohstr[i] << "\n";
			i++;
		}
		updateLivesnifferfilters();
	} else if(strstr(search, "bothhstr")) {
		int i = 0;
		//reset filters
		for(i = 0; i < MAXLIVEFILTERS; i++) {
			filter->lv_bothhstr[i][0] = '\0';
		}
		stringstream  data(value);
		string val;
		i = 0;
		// read all argumens livefilter set bothhstr 123 345 244
		while(i < MAXLIVEFILTERS and getline(data, val,' ')){
			global_livesniffer = 1;
			stringstream tmp;
			tmp << val;
			tmp >> filter->lv_bothhstr[i];
			//cout << filter->lv_bothhstr[i] << "\n";
			i++;
		}
		updateLivesnifferfilters();
	} else if(strstr(search, "vlan")) {
		int i = 0;
		//reset filters
		for(i = 0; i < MAXLIVEFILTERS; i++) {
			filter->lv_vlan[i] = 0;
			filter->lv_vlan_set[i] = false;
		}
		stringstream  data(value);
		string val;
		i = 0;
		// read all argumens livefilter set bothhstr 123 345 244
		while(i < MAXLIVEFILTERS and getline(data, val,' ')){
			global_livesniffer = 1;
			filter->lv_vlan[i] = atoi(val.c_str());
			filter->lv_vlan_set[i] = true;
			i++;
		}
		updateLivesnifferfilters();
	} else if(strstr(search, "siptypes")) {
		//cout << "siptypes: " << value << "\n";
		for(size_t i = 0; i < strlen(value) && i < MAXLIVEFILTERS; i++) {
			filter->lv_siptypes[i] = value[i] == 'I' ? INVITE :
				value[i] == 'R' ? REGISTER :
				value[i] == 'O' ? OPTIONS :
				value[i] == 'S' ? SUBSCRIBE :
				value[i] == 'M' ? MESSAGE :
				value[i] == 'N' ? NOTIFY :
				0;
		}
		updateLivesnifferfilters();
	}
	__SYNC_UNLOCK(usersniffer_sync);
	return(params->sendString("ok"));
}

int Mgmt_listen_stop(Mgmt_params *params) {
	if (params->task == params->mgmt_task_DoInit) {
		params->registerCommand("listen_stop", "stop listen");
		return(0);
	}

	if(!calltable) {
		return(-1);
	}
	long long callreference = 0;
	char listen_id[20] = "";
	string error;
	sscanf(params->buf, "listen_stop %llu %s", &callreference, listen_id);
	if(!callreference) {
		listen_id[0] = 0;
		sscanf(params->buf, "listen_stop %llx %s", &callreference, listen_id);
	}
	listening_master_lock();
	c_listening_clients::s_client *l_client = listening_clients.get(listen_id, (Call*)callreference);
	if(l_client) {
		listening_clients.remove(l_client);
	}
	c_listening_workers::s_worker *l_worker = listening_workers.get((Call*)callreference);
	if(l_worker && !listening_clients.exists(l_worker->call)) {
		listening_workers.stop(l_worker);
		while(l_worker->running) {
			USLEEP(100);
		}
		listening_workers.remove(l_worker);
	}
	listening_master_unlock();
	return(0);
}

int Mgmt_listen(Mgmt_params *params) {
	if (params->task == params->mgmt_task_DoInit) {
		params->registerCommand("listen", "start listen");
		return(0);
	}

	if(!calltable) {
		return(-1);
	}
	int rslt = 0;
	string error;
	extern int opt_liveaudio;
	if(opt_liveaudio) {
		long long callreference = 0;
		char listen_id[20] = "";
		sscanf(params->buf, "listen %llu %s", &callreference, listen_id);
		if(!callreference) {
			listen_id[0] = 0;
			sscanf(params->buf, "listen %llx %s", &callreference, listen_id);
		}
		listening_master_lock();
		Call *call = calltable->find_by_reference(callreference, false);
		if(call) {
			bool newWorker = false;
			string rslt_str = "success";
			c_listening_workers::s_worker *l_worker = listening_workers.get(call);
			if(l_worker) {
				rslt_str = "call already listening";
			} else {
				l_worker = listening_workers.add(call);
				listening_workers.run(l_worker);
				newWorker = true;
			}
			c_listening_clients::s_client *l_client = listening_clients.add(listen_id, call);
			if(!newWorker) {
				l_client->spybuffer_start_pos = l_worker->spybuffer->size_all_with_freed_pos();
			}
			if(params->sendString(&rslt_str) == -1) {
				rslt = -1;
			}
		} else {
			error = "call not found";
		}
		listening_master_unlock();
	} else {
		error = "liveaudio is disabled";
	}
	if(!error.empty()) {
		if(params->sendString(&error) == -1) {
			rslt = -1;
		}
	}
	return(rslt);
}

int Mgmt_readaudio(Mgmt_params *params) {
	if (params->task == params->mgmt_task_DoInit) {
		params->registerCommand("readaudio", "start read audio");
		return(0);
	}

	if(!calltable) {
		return(-1);
	}
	long long callreference = 0;
	char listen_id[20] = "";
	string error;
	string information;
	int rslt = 0;
	sscanf(params->buf, "readaudio %llu %s", &callreference, listen_id);
	if(!callreference) {
		listen_id[0] = 0;
		sscanf(params->buf, "readaudio %llx %s", &callreference, listen_id);
	}
	listening_master_lock();
	Call *call = calltable->find_by_reference(callreference, false);
	if(call) {
		c_listening_workers::s_worker *l_worker = listening_workers.get(call);
		if(l_worker) {
			c_listening_clients::s_client *l_client = listening_clients.get(listen_id, call);
			if(l_client) {
				u_int32_t bsize = 0;
				u_int32_t from_pos = max(l_client->spybuffer_start_pos, l_client->spybuffer_last_send_pos);
				//cout << "pos: " << from_pos << " / " << l_worker->spybuffer->size_all_with_freed_pos() << endl;
				l_worker->spybuffer->lock_master();
				u_char *buff = l_worker->spybuffer->get_from_pos(&bsize, from_pos);
				if(buff) {
					//cout << "bsize: " << bsize << endl;
					l_client->spybuffer_last_send_pos = from_pos + bsize;
					u_int64_t min_use_spybuffer_sample = listening_clients.get_min_use_spybuffer_pos(l_client->call);
					if(min_use_spybuffer_sample) {
						l_worker->spybuffer->free_pos(min_use_spybuffer_sample);
					}
					l_worker->spybuffer->unlock_master();
					if(params->sendString((char*)buff, bsize) == -1) {
						rslt = -1;
					}
					delete [] buff;
				} else {
					l_worker->spybuffer->unlock_master();
					information = "wait for data";
				}
				l_client->last_activity_time = getTimeS();
			} else {
				error = "client of worker not found";
			}
		} else {
			error = "worker not found";
		}
	} else {
		error = "call not found";
	}
	listening_master_unlock();
	if(!error.empty() || !information.empty()) {
		string data = !error.empty() ?
			"error: " + error :
			"information: " + information;
		if(params->sendString(&data) == -1) {
			rslt = -1;
		}
	}
	return(rslt);
}

int Mgmt_active_call_info(Mgmt_params *params) {
	if (params->task == params->mgmt_task_DoInit) {
		params->registerCommand("active_call_info", "get ative call packet info");
		return(0);
	}
	if(!calltable) {
		return(-1);
	}
	int rslt = 0;
	string error;
	long long callreference = 0;
	string callreference_str;
	bool zip = false;
	char params_str[1000];
	sscanf(params->buf, "active_call_info %[^\n\r]", params_str);
	if(isJsonObject(params_str)) {
		JsonItem jsonParams;
		jsonParams.parse(params_str);
		callreference_str = jsonParams.getValue("callreference");
		string zip_str = jsonParams.getValue("zip");
		zip = yesno(zip_str.c_str()) || is_true(zip_str.c_str());
	} else {
		callreference_str = params_str;
	}
	sscanf(callreference_str.c_str(), "%llu", &callreference);
	if(!callreference) {
		sscanf(callreference_str.c_str(), "%llx", &callreference);
	}
	params->zip = zip;
	calltable->lock_calls_listMAP();
	Call *call = calltable->find_by_reference(callreference, false);
	if(call) {
		JsonExport json_export;
		json_export.addJson("call", call->getJsonData());
		extern bool opt_active_call_info;
		if(opt_active_call_info) {
			string sip_packets = call->branch_main()->get_sip_packets_info_json();
			json_export.addJson("sip_packets", sip_packets);
		} else {
			json_export.add("sip_packets", "active call info is disabled (option active_call_info)");
		}
		string rtp_streams = call->get_rtp_streams_info_json();
		json_export.addJson("rtp_streams", rtp_streams);
		string json_rslt = json_export.getJson();
		if(params->sendString(&json_rslt) == -1) {
			rslt = -1;
		}
	} else {
		error = "call not found";
	}
	calltable->unlock_calls_listMAP();
	if(!error.empty()) {
		if(params->sendString(&error) == -1) {
			rslt = -1;
		}
	}
	return(rslt);
}

int Mgmt_reload(Mgmt_params *params) {
	if (params->task == params->mgmt_task_DoInit) {
		params->registerCommand("reload", "voipmonitor's reload. The reloaded items: the capture rules for now");
		return(0);
	}
	reload_capture_rules();
	return(params->sendString("reload ok"));
}

int Mgmt_crules_print(Mgmt_params *params) {
	if (params->task == params->mgmt_task_DoInit) {
		params->registerCommand("crules_print", "debug print of the capture rules");
		return(0);
	}
	ostringstream oss;
	oss << "IPfilter" << endl;
	IPfilter::dump2man(oss);
	oss << "TELNUMfilter" << endl;
	TELNUMfilter::dump2man(oss, NULL);
	oss << "DOMAINfilter" << endl;
	DOMAINfilter::dump2man(oss);
	oss << "SIP_HEADERfilter" << endl;
	SIP_HEADERfilter::dump2man(oss);
	string txt = oss.str();
	return(params->sendString(&txt));
}

int Mgmt_hot_restart(Mgmt_params *params) {
	if (params->task == params->mgmt_task_DoInit) {
		params->registerCommand("hot_restart", "do hot restart");
		return(0);
	}
	hot_restart();
	return(params->sendString("hot restart ok"));
}

int Mgmt_get_json_config(Mgmt_params *params) {
	string cmd = "get_json_config";
	if (params->task == params->mgmt_task_DoInit) {
		params->registerCommand(cmd.c_str(), "export JSON config", 2);
		return(0);
	}
	string rslt;
	vector<string> filter;
	if(strlen(params->buf) > cmd.length() + 1) {
		split(params->buf + cmd.length() + 1, ',', filter);
	}
	if (params->task == params->mgmt_task_CheckNeedAes) {
		if(filter.size() == 0) {
			return(true);
		}
		const char *notNeedAesValues[] = {
			"server_bind",
			"server_bind_port",
			"server_destination",
			"packetbuffer_sender"
		};
		for(vector<string>::iterator iter = filter.begin(); iter != filter.end(); iter++) {
			bool notNeedAesValues_ok = false;
			for(unsigned i = 0; i < sizeof(notNeedAesValues) / sizeof(notNeedAesValues[0]); i++) {
				if(!strcmp(iter->c_str(), notNeedAesValues[i])) {
					notNeedAesValues_ok = true;
				}
			}
			if(!notNeedAesValues_ok) {
				return(true);
			}
		}
		return(false);
	}
	if(CONFIG.isSet()) {
		rslt = CONFIG.getJson(false, &filter);
	} else {
		cConfig config;
		config.addConfigItems();
		rslt = config.getJson(false, &filter);
	}
	return(params->sendString(&rslt));
}

int Mgmt_set_json_config(Mgmt_params *params) {
	if (params->task == params->mgmt_task_DoInit) {
		params->registerCommand("set_json_config", "set JSON config");
		return(0);
	}
	hot_restart_with_json_config(params->buf + 16);
	return(params->sendString("ok"));
}

int Mgmt_fraud_refresh(Mgmt_params *params) {
	if (params->task == params->mgmt_task_DoInit) {
		params->registerCommand("fraud_refresh", "refresh fraud");
		return(0);
	}
	refreshFraud();
	return(params->sendString("reload ok"));
}

int Mgmt_fraud_rcc_dump(Mgmt_params *params) {
	if (params->task == params->mgmt_task_DoInit) {
		params->registerCommand("fraud_rcc_dump", "fraud rcc dump");
		return(0);
	}
	string rslt;
	fraudRccDump(&rslt);
	if(rslt.empty()) {
		rslt = "fraud rcc dump is empty";
	}
	return(params->sendString(rslt));
}

int Mgmt_send_call_info_refresh(Mgmt_params *params) {
	if (params->task == params->mgmt_task_DoInit) {
		params->registerCommand("send_call_info_refresh", "send call info refresh");
		return(0);
	}
	refreshSendCallInfo();
	return(params->sendString("reload ok"));
}

int Mgmt_options_qualify_refresh(Mgmt_params *params) {
	if (params->task == params->mgmt_task_DoInit) {
		params->registerCommand("options_qualify_refresh", "refresh options qualify");
		return(0);
	}
	extern cSipMsgRelations *sipMsgRelations;
	if(sipMsgRelations) {
		sipMsgRelations->loadParamsInBackground();
		return(params->sendString("reload ok"));
	} else {
		return(params->sendString("subsystem SIP Opt.,Subsc.,Notify is not running"));
	}
}

int Mgmt_custom_headers_refresh(Mgmt_params *params) {
	if (params->task == params->mgmt_task_DoInit) {
		params->registerCommand("custom_headers_refresh", "refresh custom headers");
		return(0);
	}
	extern CustomHeaders *custom_headers_cdr;
	extern CustomHeaders *custom_headers_message;
	extern CustomHeaders *custom_headers_sip_msg;
	extern NoHashMessageRules *no_hash_message_rules;
	if(custom_headers_cdr) {
		custom_headers_cdr->refresh();
	}
	if(custom_headers_message) {
		custom_headers_message->refresh();
	}
	if(custom_headers_sip_msg) {
		custom_headers_sip_msg->refresh();
	}
	if(no_hash_message_rules) {
		no_hash_message_rules->refresh();
	}
	return(params->sendString("reload ok"));
}

int Mgmt_custom_headers_dump(Mgmt_params *params) {
	if (params->task == params->mgmt_task_DoInit) {
		params->registerCommand("custom_headers_dump", "custom headers dump");
		return(0);
	}
	char ch_type[100] = "";
	if(strlen(params->buf) > params->command.length() + 1) {
		sscanf(params->buf + params->command.length() + 1, "%s", ch_type);
	}
	extern CustomHeaders *custom_headers_cdr;
	extern CustomHeaders *custom_headers_message;
	CustomHeaders *ch = NULL;
	if(!strcasecmp(ch_type, "cdr")) {
		ch = custom_headers_cdr;
	} else if(!strcasecmp(ch_type, "message")) {
		ch = custom_headers_message;
	}
	if(ch) {
		string ch_dump = ch->dump();
		return(params->sendString(ch_dump));
	}
	return(0);
}

int Mgmt_no_hash_message_rules_refresh(Mgmt_params *params) {
	if (params->task == params->mgmt_task_DoInit) {
		params->registerCommand("no_hash_message_rules_refresh", "refresh no hash message rules");
		return(0);
	}
	extern NoHashMessageRules *no_hash_message_rules;
	if(no_hash_message_rules) {
		no_hash_message_rules->refresh();
	}
	return(params->sendString("reload ok"));
}

int Mgmt_billing_refresh(Mgmt_params *params) {
	if (params->task == params->mgmt_task_DoInit) {
		params->registerCommand("billing_refresh", "refresh billing");
		return(0);
	}
	refreshBilling();
	return(params->sendString("reload ok"));
}

int Mgmt_country_detect_refresh(Mgmt_params *params) {
	if (params->task == params->mgmt_task_DoInit) {
		params->registerCommand("country_detect_refresh", "refresh country detect");
		return(0);
	}
	refreshBilling();
	CountryDetectPrepareReload();
	return(params->sendString("reload ok"));
}

int Mgmt_getfile_is_zip_support(Mgmt_params *params) {
	if (params->task == params->mgmt_task_DoInit) {
		params->registerCommand("getfile_is_zip_support", "check getfile zip support");
		return(0);
	}
	return(params->sendString("OK"));
}

int Mgmt_getfile_in_tar_check_complete(Mgmt_params *params) {
	if (params->task == params->mgmt_task_DoInit) {
		params->registerCommand("getfile_in_tar_check_complete", "getfile in tar check complete");
		return(0);
	}
	char tar_filename[2048];
	char filename[2048];
	char dateTimeKey[2048];

	sscanf(params->buf, "getfile_in_tar_check_complete %s %s %s", tar_filename, filename, dateTimeKey);

	const char *rslt = getfile_in_tar_completed.check(tar_filename, filename, dateTimeKey) ? "OK" : "uncomplete";

	return(params->sendString(rslt));
}

int Mgmt_getfile_in_tar(Mgmt_params *params) {
	if (params->task == params->mgmt_task_DoInit) {
		commandAndHelp ch[] = {
			{"getfile_in_tar", "get file(s) in tar"},
			{"getfile_in_tar_zip", "get file(s) in zipped tar"},
			{NULL, NULL}
		};
		params->registerCommand(ch);
		return(0);
	}

	bool zip = strstr(params->buf, "getfile_in_tar_zip");
	params->zip = zip;

	char tar_filename[2048];
	char filename[2048];
	char dateTimeKey[2048];
	u_int32_t recordId = 0;
	char tableType[100] = "";
	char *tarPosI = new FILE_LINE(13011) char[1000000];
	unsigned spool_index = 0;
	int type_spool_file = (int)tsf_na;
	char end_string[100] = "";
	*tarPosI = 0;

	sscanf(params->buf, (string(zip ? "getfile_in_tar_zip" : "getfile_in_tar") + " %s %s %s %u %s %s %u %i %s").c_str(), 
	       tar_filename, filename, dateTimeKey, &recordId, tableType, tarPosI, &spool_index, &type_spool_file, end_string);
	if(type_spool_file == tsf_na) {
		type_spool_file = findTypeSpoolFile(spool_index, tar_filename);
	}
	if(strstr(tar_filename, "../")) {
		return(params->sendString("access denied"));
	}
	
	string tar_filepathname;
	for(int spool_tar_move = (opt_tar_move && !opt_tar_move_destination_path.empty() ? 1 : 0); spool_tar_move >= 0; spool_tar_move--) {
		string spooldir = spool_tar_move ?
				   opt_tar_move_destination_path :
				   getSpoolDir((eTypeSpoolFile)type_spool_file, spool_index);
		if(file_exists(spooldir + '/' + tar_filename)) {
			tar_filepathname = spooldir + '/' + tar_filename;
			break;
		}
	}
	if(tar_filepathname.empty()) {
		char buf_output[2048 + 100];
		snprintf(buf_output, sizeof(buf_output), "error: missing file [%s]", tar_filename);
		params->sendString(buf_output);
		delete [] tarPosI;
		return -1;
	}
	
	Tar tar;
	if(!tar.tar_open(tar_filepathname, O_RDONLY)) {
		string filename_conv = filename;
		prepare_string_to_filename((char*)filename_conv.c_str());
		tar.tar_read_send_parameters(params);
		tar.tar_read(filename_conv.c_str(), recordId, tableType, tarPosI);
		if(tar.isReadEnd()) {
			if(end_string[0]) {
				params->_send(end_string, strlen(end_string));
			}
			getfile_in_tar_completed.add(tar_filename, filename, dateTimeKey);
		}
	} else {
		char buf_output[2048 + 100];
		snprintf(buf_output, sizeof(buf_output), "error: cannot open file [%s]", tar_filename);
		params->sendString(buf_output);
		delete [] tarPosI;
		return -1;
	}
	
	delete [] tarPosI;
	return 0;
}

int Mgmt_getfile(Mgmt_params *params) {
	if (params->task == params->mgmt_task_DoInit) {
		commandAndHelp ch[] = {
			{"getfile", "get file"},
			{"getfile_zip", "get zipped file"},
			{NULL, NULL}
		};
		params->registerCommand(ch);
		return(0);
	}
	params->zip = strstr(params->buf, "getfile_zip");

	char filename[2048];
	unsigned spool_index = 0;
	int type_spool_file = (int)tsf_na;

	sscanf(params->buf, params->zip ? "getfile_zip %s %u %i" : "getfile %s %u %i", filename, &spool_index, &type_spool_file);
	if(strstr(filename, "../")) {
		return(params->sendString("access denied"));
	}
	
	if(type_spool_file == tsf_na) {
		type_spool_file = findTypeSpoolFile(spool_index, filename);
	}
	return(params->sendFile((string(getSpoolDir((eTypeSpoolFile)type_spool_file, spool_index)) + '/' + filename).c_str()));
}

int Mgmt_file_exists(Mgmt_params *params) {
	if (params->task == params->mgmt_task_DoInit) {
		params->registerCommand("file_exists", "file exists");
		return(0);
	}

	if(is_sender()) {
		return(params->sendString("mirror"));
	}

	char *filename = new FILE_LINE(0) char[1024 * 100];
	unsigned spool_index = 0;
	int type_spool_file = (int)tsf_na;
	u_int64_t size;
	string rslt;

	sscanf(params->buf, "file_exists %s %u %i", filename, &spool_index, &type_spool_file);
	int type_spool_file_orig = type_spool_file;

	if(strchr(filename, '|')) {
		vector<string> filenames = explode(filename, '|');
		int error_code_rslt = 0;
		vector<string> rslts;
		for(unsigned i = 0; i < filenames.size(); i++) {
			for(int spool_tar_move = (opt_tar_move && !opt_tar_move_destination_path.empty() ? 1 : 0); spool_tar_move >= 0; spool_tar_move--) {
				int error_code;
				string filename = filenames[i];
				string spooldir;
				if(spool_tar_move) {
					spooldir = opt_tar_move_destination_path;
				} else {
					if(type_spool_file_orig == tsf_na) {
						type_spool_file = findTypeSpoolFile(spool_index, filename.c_str());
					}
					spooldir = getSpoolDir((eTypeSpoolFile)type_spool_file, spool_index);
				}
				if(file_exists(spooldir + '/' + filename, &error_code)) {
					size = file_size(spooldir + '/' + filename);
					string rslt_i = filename + ":" + intToString(size);
					if(size > 0 && filename.find("tar") != string::npos) {
						for(int i = 1; i <= 5; i++) {
							string nextfilename = filename;
							nextfilename += "." + intToString(i);
							u_int64_t nextsize = file_size(spooldir + '/' + nextfilename);
							if(nextsize > 0) {
								rslt_i += ";" + nextfilename + ":" + intToString(nextsize);
							} else {
								break;
							}
						}
					}
					rslts.push_back(rslt_i);
					if(spool_tar_move) {
						break;
					}
				} else {
					if(error_code) {
						error_code_rslt = error_code;
					}
				}
			}
		}
		if(rslts.size()) {
			rslt = "ok\n" + implode(rslts, "\n");
		} else {
			rslt = error_code_rslt == EACCES ? "permission_denied" : "not_exists";
		}
	} else {
		if(type_spool_file == tsf_na) {
			type_spool_file = findTypeSpoolFile(spool_index, filename);
		}
		int error_code;
		if(file_exists(string(getSpoolDir((eTypeSpoolFile)type_spool_file, spool_index)) + '/' + filename, &error_code)) {
			size = file_size(string(getSpoolDir((eTypeSpoolFile)type_spool_file, spool_index)) + '/' + filename);
			rslt = intToString(size);
			if(size > 0 && strstr(filename, "tar")) {
				for(int i = 1; i <= 5; i++) {
					string nextfilename = filename;
					nextfilename += "." + intToString(i);
					u_int64_t nextsize = file_size(string(getSpoolDir((eTypeSpoolFile)type_spool_file, spool_index)) + '/' + nextfilename);
					if(nextsize > 0) {
						rslt += ";" + nextfilename + ":" + intToString(nextsize);
					} else {
						break;
					}
				}
			}
		} else {
			rslt = error_code == EACCES ? "permission_denied" : "not_exists";
		}
	}
	
	delete [] filename;
	
	return(params->sendString(&rslt));
}

int Mgmt_fileexists(Mgmt_params *params) {
	if (params->task == params->mgmt_task_DoInit) {
		params->registerCommand("fileexists", "file exists 2");
		return(0);
	}
	char filename[2048];
	unsigned int size;
	char buf_output[1024];

	sscanf(params->buf, "fileexists %s", filename);
	size = file_size(filename);
	snprintf(buf_output, sizeof(buf_output), "%d", size);
	return(params->sendString(buf_output));
}

int Mgmt_flush_tar(Mgmt_params *params) {
	if (params->task == params->mgmt_task_DoInit) {
		params->registerCommand("flush_tar", "flush_tar");
		return(0);
	}
	char filename[2048];
	sscanf(params->buf, "flush_tar %s", filename);
	flushTar(filename);
	return(params->sendString("OK"));
}

int Mgmt_genhttppcap(Mgmt_params *params) {
	if (params->task == params->mgmt_task_DoInit) {
		params->registerCommand("genhttppcap", "get http pcap");
		return(0);
	}

	char timestamp_from[100];
	char timestamp_to[100];
	char *ids = new FILE_LINE(13012) char [1000000];
	sscanf(params->buf, "genhttppcap %19[T0-9--: ] %19[T0-9--: ] %s", timestamp_from, timestamp_to, ids);
	/*
	   cout << timestamp_from << endl
	   << timestamp_to << endl
	   << ids << endl;
	*/
	HttpPacketsDumper dumper;
	dumper.setTemplatePcapName();
	dumper.setUnlinkPcap();
	dumper.dumpData(timestamp_from, timestamp_to, ids);
	dumper.closePcapDumper();

	delete [] ids;

	if(!dumper.getPcapName().empty() && file_exists(dumper.getPcapName()) > 0) {
		return(params->sendFile(dumper.getPcapName().c_str()));
	} else {
		params->sendString("null");
		return(0);
	}
}

int Mgmt_terminating(Mgmt_params *params) {
	if (params->task == params->mgmt_task_DoInit) {
		params->registerCommand("terminating", "terminates sensor");
		return(0);
	}
	vm_terminate();
	return(0);
}

int Mgmt_coutstr(Mgmt_params *params) {
	if (params->task == params->mgmt_task_DoInit) {
		params->registerCommand("coutstr", "echo string to the standart output");
		return(0);
	}
	char *pointToSpaceSeparator = strchr(params->buf, ' ');
	if(pointToSpaceSeparator) {
		cout << (pointToSpaceSeparator + 1) << flush;
	}
	return(0);
}

int Mgmt_syslogstr(Mgmt_params *params) {
	if (params->task == params->mgmt_task_DoInit) {
		params->registerCommand("syslogstr", "sends string to the syslog");
		return(0);
	}
	char *pointToSpaceSeparator = strchr(params->buf, ' ');
	if(pointToSpaceSeparator) {
		syslog(LOG_NOTICE, "%s", pointToSpaceSeparator + 1);
	}
	return(0);
}

int Mgmt_custipcache_get_cust_id(Mgmt_params *params) {
	if (params->task == params->mgmt_task_DoInit) {
		params->registerCommand("custipcache_get_cust_id", "custipcache_get_cust_id");
		return(0);
	}
	char ip[INET6_ADDRSTRLEN];
	sscanf(params->buf, "custipcache_get_cust_id %s", ip);
	CustIpCache *custIpCache = getCustIpCache();
	if(custIpCache) {
		unsigned int cust_id = custIpCache->getCustByIp(str_2_vmIP(ip));
		char sendbuf[BUFSIZE];
		snprintf(sendbuf, BUFSIZE, "cust_id: %u\n", cust_id);
		return(params->sendString(sendbuf));
	}
	return(0);
}

int Mgmt_custipcache_refresh(Mgmt_params *params) {
	if (params->task == params->mgmt_task_DoInit) {
		params->registerCommand("custipcache_refresh", "custipcache_refresh");
		return(0);
	}
	char sendbuf[BUFSIZE];
	int rslt = refreshCustIpCache();
	snprintf(sendbuf, BUFSIZE, "rslt: %i\n", rslt);
	return(params->sendString(sendbuf));
}

int Mgmt_custipcache_vect_print(Mgmt_params *params) {
	if (params->task == params->mgmt_task_DoInit) {
		params->registerCommand("custipcache_vect_print", "custipcache_vect_print");
		return(0);
	}
	CustIpCache *custIpCache = getCustIpCache();
	if(custIpCache) {
		string rslt = custIpCache->printVect();
		return(params->sendString(&rslt));
	}
	return(0);
}

int Mgmt_upgrade_restart(Mgmt_params *params) {
	if (params->task == params->mgmt_task_DoInit) {
		commandAndHelp ch[] = {
			{"upgrade", "upgrades sensor"},
			{"restart", "restarts sensor"},
			{NULL, NULL}
		};
		params->registerCommand(ch);
		return(0);
	}

	bool upgrade = false;
	string build;
	string version;
	string url;
	string md5_32;
	string md5_64;
	string md5_arm;
	string md5_64_ws;
	string rsltForSend;

	if(strstr(params->buf, "upgrade") != NULL) {
		extern void dns_lookup_common_hostnames();
		dns_lookup_common_hostnames();

		extern bool opt_upgrade_by_git;
		if(opt_upgrade_by_git) {
			rsltForSend = "upgrade from official binary source disabled - upgrade by git!";
		} else {
			upgrade = true;
			string command = params->buf;
			size_t pos = command.find("to: [");
			if(pos != string::npos) {
				size_t posEnd = command.find("]", pos);
				if(posEnd != string::npos) {
					version = command.substr(pos + 5, posEnd - pos - 5);
				}
			} else {
				pos = command.find("build: [");
				if(pos != string::npos) {
					size_t posEnd = command.find("]", pos);
					if(posEnd != string::npos) {
						build = command.substr(pos + 8, posEnd - pos - 8);
					}
				}
			}
			if(build.empty()) {
				if(pos != string::npos) {
					pos = command.find("url: [", pos);
					if(pos != string::npos) {
						size_t posEnd = command.find("]", pos);
						if(posEnd != string::npos) {
							url = command.substr(pos + 6, posEnd - pos - 6);
						}
					}
				}
				if(pos != string::npos) {
					pos = command.find("md5: [", pos);
					if(pos != string::npos) {
						size_t posEnd = command.find("]", pos);
						if(posEnd != string::npos) {
							md5_32 = command.substr(pos + 6, posEnd - pos - 6);
						}
						for(int i = 0; i < 3; i++) {
							pos = command.find(" / [", pos);
							if(pos != string::npos) {
								size_t posEnd = command.find("]", pos);
								if(posEnd != string::npos) {
									string md5 = command.substr(pos + 4, posEnd - pos - 4);
									switch(i) {
										case 0: md5_64 = md5; break;
										case 1: md5_arm = md5; break;
										case 2: md5_64_ws = md5; break;
									}
									pos = posEnd;
								} else {
									break;
								}
							} else {
								break;
							}
						}
					}
				}
				if(!version.length()) {
					rsltForSend = "missing version in command line";
				} else if(!url.length()) {
					rsltForSend = "missing url in command line";
				} else if(!md5_32.length() || !md5_64.length()) {
					rsltForSend = "missing md5 in command line";
				}
			} else {
				pos = command.find("ver: [", pos);
				if(pos != string::npos) {
					size_t posEnd = command.find("]", pos);
					if(posEnd != string::npos) {
						version = command.substr(pos + 6, posEnd - pos - 6);
					}
				}
				if(pos != string::npos) {
					pos = command.find("url: [", pos);
					if(pos != string::npos) {
						size_t posEnd = command.find("]", pos);
						if(posEnd != string::npos) {
							url = command.substr(pos + 6, posEnd - pos - 6);
						}
					}
				}
				if(!version.length()) {
					rsltForSend = "missing version in command line";
				} else if(!url.length()) {
					rsltForSend = "missing url in command line";
				}
			}
		}
	}
	bool ok = false;
	RestartUpgrade restart(upgrade, version.c_str(), build.c_str(), url.c_str(), md5_32.c_str(), md5_64.c_str(), md5_arm.c_str(), md5_64_ws.c_str());
	if(!rsltForSend.length()) {
		if(restart.createRestartScript() && restart.createSafeRunScript()) {
			if((!upgrade || restart.runUpgrade()) &&
					restart.checkReadyRestart() &&
					restart.isOk()) {
				ok = true;
			}
		}
		rsltForSend = restart.getRsltString();
	}
	if (params->sendString(&rsltForSend) == -1){
		return -1;
	}
	if(ok) {
		restart.runRestart(params->client.handler, manager_socket_server, params->c_client);
	}
	return 0;
}

int Mgmt_gitUpgrade(Mgmt_params *params) {
	if (params->task == params->mgmt_task_DoInit) {
		params->registerCommand("gitUpgrade", "do upgrade from git");
		return(0);
	}
	char cmd[100];
	sscanf(params->buf, "gitUpgrade %s", cmd);
	RestartUpgrade upgrade;
	bool rslt = upgrade.runGitUpgrade(cmd);
	string rsltString;
	if(rslt) {
		rsltString = "OK";
	} else {
		rsltString = upgrade.getErrorString();
	}
	rsltString.append("\n");
	return(params->sendString(&rsltString));
}

int Mgmt_sniffer_stat(Mgmt_params *params) {
	if (params->task == params->mgmt_task_DoInit) {
		params->registerCommand("sniffer_stat", "return sniffer's statistics", true);
		return(0);
	}

	extern vm_atomic<string> storingCdrLastWriteAt;
	extern vm_atomic<string> pbStatString;
	extern vm_atomic<u_long> pbCountPacketDrop;
	extern bool opt_upgrade_by_git;
	extern bool packetbuffer_memory_is_full;
	extern vm_atomic<string> terminating_error;
	ostringstream outStrStat;
	extern int vm_rrd_version;
	checkRrdVersion(true);
	__SYNC_LOCK(usersniffer_sync);
	size_t countLiveSniffers = usersniffer.size();
	__SYNC_UNLOCK(usersniffer_sync);
	outStrStat << "{";
	outStrStat << "\"version\": \"" << RTPSENSOR_VERSION << "\",";
	outStrStat << "\"build\": \"" << RTPSENSOR_BUILD_NUMBER << "\",";
	outStrStat << "\"rrd_version\": \"" << vm_rrd_version << "\",";
	outStrStat << "\"storingCdrLastWriteAt\": \"" << storingCdrLastWriteAt << "\",";
	outStrStat << "\"pbStatString\": \"" << pbStatString << "\",";
	outStrStat << "\"pbCountPacketDrop\": \"" << pbCountPacketDrop << "\",";
	outStrStat << "\"uptime\": \"" << getUptime() << "\",";
	outStrStat << "\"memory_is_full\": \"" << packetbuffer_memory_is_full << "\",";
	outStrStat << "\"count_live_sniffers\": \"" << countLiveSniffers << "\",";
	outStrStat << "\"upgrade_by_git\": \"" << opt_upgrade_by_git << "\",";
	outStrStat << "\"use_new_config\": \"" << true << "\",";
	outStrStat << "\"terminating_error\": \"" << terminating_error << "\"";
	outStrStat << "}";
	outStrStat << endl;
	string outStrStatStr = outStrStat.str();
	return(params->sendString(&outStrStatStr));
}

int Mgmt_datadir_stat(Mgmt_params *params) {
	if (params->task == params->mgmt_task_DoInit) {
		params->registerCommand("datadir_stat", "return sniffer's mysql datadir statistics", true);
		return(0);
	}
	cPartitions p;
	string rslt = p.dump(false);
	return(params->sendString(rslt));
}

int Mgmt_sniffer_threads(Mgmt_params *params) {
	if (params->task == params->mgmt_task_DoInit) {
		params->registerCommand("sniffer_threads", "return sniffer's thread statistics", true);
		return(0);
	}
	extern cThreadMonitor threadMonitor;
	int outputFlags = 0;
	if(strstr(params->buf, "no_sort") != NULL) {
		outputFlags |= cThreadMonitor::_of_no_sort;
	}
	if(strstr(params->buf, "only_traffic") != NULL) {
		outputFlags |= cThreadMonitor::_of_only_traffic;
	}
	if(strstr(params->buf, "all") != NULL) {
		outputFlags |= cThreadMonitor::_of_all;
	}
	if(strstr(params->buf, "line") != NULL) {
		outputFlags |= cThreadMonitor::_of_line;
	}
	string threads = threadMonitor.output(1, outputFlags);
	return(params->sendString(&threads));
}

int Mgmt_pcapstat(Mgmt_params *params) {
	if (params->task == params->mgmt_task_DoInit) {
		params->registerCommand("pcapstat", "return pcap's statistics");
		return(0);
	}
	extern PcapQueue *pcapQueueStatInterface;
	string rslt;
	if(pcapQueueStatInterface) {
		rslt = pcapQueueStatInterface->pcapDropCountStat();
		if(!rslt.length()) {
			rslt = "ok";
		}
	} else {
		rslt = "no PcapQueue mode";
	}
	return(params->sendString(&rslt));
}

int Mgmt_login_screen_popup(Mgmt_params *params) {
	if (params->task == params->mgmt_task_DoInit) {
		params->registerCommand("login_screen_popup", "login_screen_popup");
		return(0);
	}
	*params->managerClientThread =  new FILE_LINE(13013) ManagerClientThread_screen_popup(params->client, params->buf);
	return(0);
}

int Mgmt_processing_limitations(Mgmt_params *params) {
	extern cProcessingLimitations processing_limitations;
	if (params->task == params->mgmt_task_DoInit) {
		commandAndHelp ch[] = {
			{"processing_limitations_inc", ""},
			{"processing_limitations_dec", ""},
			{NULL, NULL}
		};
		params->registerCommand(ch);
		return(0);
	}
	if(strstr(params->buf, "processing_limitations_inc") != NULL) {
		processing_limitations.incLimitations(cProcessingLimitations::_pl_all, true);
	} else if(strstr(params->buf, "processing_limitations_dec") != NULL) {
		processing_limitations.decLimitations(cProcessingLimitations::_pl_all, true);
	}
	return(0);
}

int Mgmt_enable_bad_packet_order_warning(Mgmt_params *params) {
	if (params->task == params->mgmt_task_DoInit) {
		params->registerCommand("enable_bad_packet_order_warning", "enable_bad_packet_order_warning");
		return(0);
	}
	enable_bad_packet_order_warning = 1;
	return(params->sendString("ok\n"));
}

int Mgmt_sipports(Mgmt_params *params) {
	if (params->task == params->mgmt_task_DoInit) {
		params->registerCommand("sipports", "return list of used sip ports");
		return(0);
	}
	ostringstream outStrSipPorts;
	extern char *sipportmatrix;
	outStrSipPorts << cConfigItem_ports::getPortString(sipportmatrix) << ',';
	ssl_ipport_lock();
	extern map<vmIPport, string> ssl_ipport;
	for(map<vmIPport, string>::iterator it = ssl_ipport.begin(); it != ssl_ipport.end(); it++) {
		outStrSipPorts << it->first.port << ',';
	}
	extern map<vmIPmask_port, string> ssl_netport;
	for(map<vmIPmask_port, string>::iterator it = ssl_netport.begin(); it != ssl_netport.end(); it++) {
		outStrSipPorts << it->first.port << ',';
	}
	ssl_ipport_unlock();
	outStrSipPorts << endl;
	string strSipPorts = outStrSipPorts.str();
	return(params->sendString(&strSipPorts));
}

int Mgmt_skinnyports(Mgmt_params *params) {
	if (params->task == params->mgmt_task_DoInit) {
		params->registerCommand("skinnyports", "return list of used skinny ports");
		return(0);
	}

	ostringstream outStrSkinnyPorts;
	extern char *skinnyportmatrix;
	extern int opt_skinny;
	if (opt_skinny) {
		outStrSkinnyPorts << cConfigItem_ports::getPortString(skinnyportmatrix);
	}
	outStrSkinnyPorts << endl;
	string strSkinnyPorts = outStrSkinnyPorts.str();
	return(params->sendString(&strSkinnyPorts));
}

int Mgmt_ignore_rtcp_jitter(Mgmt_params *params) {
	if (params->task == params->mgmt_task_DoInit) {
		params->registerCommand("ignore_rtcp_jitter", "return ignore rtcp jitter value");
		return(0);
	}
	extern unsigned int opt_ignoreRTCPjitter;
	ostringstream outStrIgnoreJitter;
	outStrIgnoreJitter << opt_ignoreRTCPjitter << endl;
	string ignoreJitterVal = outStrIgnoreJitter.str();
	return(params->sendString(&ignoreJitterVal));
}

int Mgmt_convertchars(Mgmt_params *params) {
	if (params->task == params->mgmt_task_DoInit) {
		params->registerCommand("convertchars", "convertchars");
		return(0);
	}
	ostringstream outStrConvertchar;
	extern char opt_convert_char[64];
	for(unsigned int i = 0; i < sizeof(opt_convert_char) && opt_convert_char[i]; i++) {
		outStrConvertchar << opt_convert_char[i] << ',';
	}
	outStrConvertchar << endl;
	string strConvertchar = outStrConvertchar.str();
	return(params->sendString(&strConvertchar));
}

int Mgmt_natalias(Mgmt_params *params) {
	if (params->task == params->mgmt_task_DoInit) {
		params->registerCommand("natalias", "natalias");
		return(0);
	}
	extern nat_aliases_t nat_aliases;
	string strNatAliases;
	if(nat_aliases.size()) {
		ostringstream outStrNatAliases;
		for(nat_aliases_t::iterator iter = nat_aliases.begin(); iter != nat_aliases.end(); iter++) {
			outStrNatAliases << iter->first.getString() << ':' << iter->second.getString() << ',';
		}
		strNatAliases = outStrNatAliases.str();
	} else {
		strNatAliases = "none";
	}
	return(params->sendString(&strNatAliases));
}

int Mgmt_sql_time_information(Mgmt_params *params) {
	if (params->task == params->mgmt_task_DoInit) {
		params->registerCommand("sql_time_information", "sql_time_information");
		return(0);
	}

	string timezone_name = "UTC";
	long timezone_offset = 0;
	extern bool opt_sql_time_utc;
	char sendbuf[BUFSIZE];
	if(!opt_sql_time_utc && !isCloud()) {
		time_t t = time(NULL);
		struct tm lt;
		::localtime_r(&t, &lt);
		timezone_name = getSystemTimezone();
		if(timezone_name.empty()) {
			timezone_name = lt.tm_zone;
		}
		timezone_offset = lt.tm_gmtoff;
	}
	snprintf(sendbuf, BUFSIZE, "%s,%li,%s",
			timezone_name.c_str(),
			timezone_offset,
			sqlDateTimeString(time(NULL)).c_str());
	return(params->sendString(sendbuf));
}

int Mgmt_sqlexport(Mgmt_params *params) {
	if (params->task == params->mgmt_task_DoInit) {
		commandAndHelp ch[] = {
			{"sqlexport", "sqlexport"},
			{"sqlvmexport", "sqlvmexport"},
			{NULL, NULL}
		};
		params->registerCommand(ch);
		return(0);
	}
	bool sqlFormat = strstr(params->buf, "sqlexport") != NULL;
	extern MySqlStore *sqlStore;
	string rslt = sqlStore->exportToFile(NULL, "auto", sqlFormat, strstr(params->buf, "clean") != NULL);
	return(params->sendString(&rslt));
}

int Mgmt_memory_stat(Mgmt_params *params) {
	if (params->task == params->mgmt_task_DoInit) {
		params->registerCommand("memory_stat", "return a memory statistics", true);
		return(0);
	}
	string rsltMemoryStat = getMemoryStat();
	return(params->sendString(&rsltMemoryStat));
}

int Mgmt_list_active_clients(Mgmt_params *params) {
	if (params->task == params->mgmt_task_DoInit) {
		params->registerCommand("list_active_clients", "list of active clients");
		return(0);
	}
	extern sSnifferServerServices *snifferServerServices;
	string rslt = snifferServerServices->listJsonServices();
	return(params->sendString(&rslt));
}

int Mgmt_jemalloc_stat(Mgmt_params *params) {
	if (params->task == params->mgmt_task_DoInit) {
		params->registerCommand("jemalloc_stat", "return jemalloc statistics", true);
		return(0);
	}
	string rsltMemoryStat = jeMallocStat(strstr(params->buf, "full"));
	return(params->sendString(&rsltMemoryStat));
}

int Mgmt_heapprof(Mgmt_params *params) {
	const char *startCmd = "heapprof_start";
	if (params->task == params->mgmt_task_DoInit) {
		commandAndHelp ch[] = {
			{startCmd, "heapprof_start", true},
			{"heapprof_stop", "heapprof_stop", true},
			{"heapprof_dump", "heapprof_dump", true},
			{NULL, NULL}
		};
		params->registerCommand(ch);
		return(0);
	}
	#if HAVE_LIBTCMALLOC_HEAPPROF
	extern bool heap_profiler_is_running;
	if(strstr(params->buf, startCmd)) {
		if(!heap_profiler_is_running) {
			HeapProfilerStart(strlen(params->buf) > strlen(startCmd) + 1 ? 
					   params->buf + strlen(startCmd) + 1 : 
					   "voipmonitor.hprof");
			heap_profiler_is_running = true;
		}
	} else if(strstr(params->buf, "heapprof_stop")) {
		if(heap_profiler_is_running) {
			HeapProfilerStop();
			heap_profiler_is_running = false;
		}
	} else if(strstr(params->buf, "heapprof_dump")) {
		if(heap_profiler_is_running) {
			HeapProfilerDump("force dump via manager");
		} else {
			return(params->sendString("heap profiler is not running"));
		}
	}
	#else
	return(params->sendString("heap profiler need build with tcmalloc (with heap profiler)"));
	#endif
	return(0);
}

/* obsolete
int Mgmt_cloud_activecheck(Mgmt_params *params) {
	if (params->task == params->mgmt_task_DoInit) {
		params->registerCommand("cloud_activecheck", "cloud_activecheck");
		return(0);
	}
	cloud_activecheck_success();
	return(0);
}
*/

int Mgmt_alloc_trim(Mgmt_params *params) {
	if (params->task == params->mgmt_task_DoInit) {
		params->registerCommand("alloc_trim", "alloc_trim");
		return(0);
	}
	rss_purge(true);
	return(0);
}

int Mgmt_alloc_test(Mgmt_params *params) {
	if (params->task == params->mgmt_task_DoInit) {
		params->registerCommand("alloc_test", "alloc_test");
		return(0);
	}
	unsigned gb = 0;
	unsigned s = 0;
	sscanf(params->buf, "alloc_test %u %u", &gb, &s);
	if(gb && s < 1e4) {
		s = 1e4;
	}
	static char **p;
	static unsigned c;
	if(p) {
		for(unsigned i = 0; i < c; i++) {
		unsigned ii = rand() % c;
		if(p[ii]) {
			delete [] p[ii];
			p[ii] = NULL;
		}
		}
		for(unsigned i = 0; i < c; i++) {
			if(p[i]) {
				delete [] p[i];
			}
		}
		delete [] p;
		p = NULL;
	}
	if(gb) {
		c = (unsigned)(gb * 1024ull * 1024 * 1024 / s);
		p = new char*[c];
		long unsigned sss = 0;
		for(unsigned i = 0; i < c; i++) {
			if(sss < gb * 1024ull * 1024 * 1024) {
				unsigned ss = s + rand() % s;
				p[i] = new char[ss];
				memset(p[i], 0, ss);
				sss += ss;
			} else {
				p[i] = NULL;
			}
		}
	}
	return(0);
}

int Mgmt_tcmalloc_stats(Mgmt_params *params) {
	if (params->task == params->mgmt_task_DoInit) {
		params->registerCommand("tcmalloc_stats", "tcmalloc_stats", true);
		return(0);
	}
	#if HAVE_LIBTCMALLOC
	unsigned stats_buffer_length = 1000000;
	char *stats_buffer = new char[stats_buffer_length];
	MallocExtension::instance()->GetStats(stats_buffer, stats_buffer_length);
	int rslt = params->sendString(stats_buffer);
	delete [] stats_buffer;
	return(rslt);
	#else
	return(0);
	#endif
}

int Mgmt_hashtable_stats(Mgmt_params *params) {
	if (params->task == params->mgmt_task_DoInit) {
		params->registerCommand("hashtable_stats", "hashtable_stats");
		return(0);
	}
	#if NEW_RTP_FIND__NODES
	return(0);
	#else
	return(params->sendString(calltable->getHashStats()));
	#endif
}

int Mgmt_thread(Mgmt_params *params) {
	if (params->task == params->mgmt_task_DoInit) {
		params->registerCommand("thread", "threads management", true);
		return(0);
	}
	char thread_params[5][100];
	for(unsigned i = 0; i < sizeof(thread_params) / sizeof(thread_params[0]); i++) {
		thread_params[i][0] = 0;
	}
	if(strlen(params->buf) > params->command.length() + 1) {
		sscanf(params->buf + params->command.length() + 1, "%s %s %s %s %s",
		       thread_params[0], thread_params[1], thread_params[2], thread_params[3], thread_params[4]);
	}
	if(!strcasecmp(thread_params[0], "rtp")) {
		if(!strcasecmp(thread_params[1], "add")) {
			add_rtp_read_thread();
			return(params->sendString("ok\n"));
		} else if(!strcasecmp(thread_params[1], "remove")) {
			set_remove_rtp_read_thread();
			return(params->sendString("ok\n"));
		}
	} else if(!strcasecmp(thread_params[0], "rtp_rh")) {
		extern ProcessRtpPacket *processRtpPacketHash;
		if(processRtpPacketHash) {
			if(!strcasecmp(thread_params[1], "add")) {
				processRtpPacketHash->addRtpRhThread();
				return(params->sendString("ok\n"));
			} else if(!strcasecmp(thread_params[1], "remove")) {
				processRtpPacketHash->removeRtpRhThread();
				return(params->sendString("ok\n"));
			}
		}
	} else if(!strcasecmp(thread_params[0], "sip_t2")) {
		if(!strcasecmp(thread_params[1], "add")) {
			if(opt_t2_boost) {
				#if not CALLX_MOD_OLDVER
				return(params->sendString("not supported in t2_boost mode\n"));
				#else
				PreProcessPacket::autoStartCallX_PreProcessPacket();
				#endif
			} else {
				PreProcessPacket::autoStartNextLevelPreProcessPacket();
			}
			return(params->sendString("ok\n"));
		} else if(!strcasecmp(thread_params[1], "remove")) {
			if(opt_t2_boost) {
				return(params->sendString("not supported in t2_boost mode\n"));
			} else {
				PreProcessPacket::autoStopLastLevelPreProcessPacket(true);
			}
			return(params->sendString("ok\n"));
		}
	} else if(!strcasecmp(thread_params[0], "sip_detach_x")) {
		extern PreProcessPacket *preProcessPacket[PreProcessPacket::ppt_end_base];
		if(preProcessPacket[PreProcessPacket::ppt_detach_x]) {
			if(!strcasecmp(thread_params[1], "add")) {
				preProcessPacket[PreProcessPacket::ppt_detach_x]->addNextThread();
				return(params->sendString("ok\n"));
			} else if(!strcasecmp(thread_params[1], "remove")) {
				preProcessPacket[PreProcessPacket::ppt_detach_x]->removeNextThread();
				return(params->sendString("ok\n"));
			}
		}
	} else if(!strcasecmp(thread_params[0], "sip_detach")) {
		extern PreProcessPacket *preProcessPacket[PreProcessPacket::ppt_end_base];
		if(preProcessPacket[PreProcessPacket::ppt_detach]) {
			if(!strcasecmp(thread_params[1], "add")) {
				preProcessPacket[PreProcessPacket::ppt_detach]->addNextThread();
				return(params->sendString("ok\n"));
			} else if(!strcasecmp(thread_params[1], "remove")) {
				preProcessPacket[PreProcessPacket::ppt_detach]->removeNextThread();
				return(params->sendString("ok\n"));
			}
		}
	} else if(!strcasecmp(thread_params[0], "sip_sip")) {
		extern PreProcessPacket *preProcessPacket[PreProcessPacket::ppt_end_base];
		if(preProcessPacket[PreProcessPacket::ppt_sip]) {
			if(!strcasecmp(thread_params[1], "add")) {
				preProcessPacket[PreProcessPacket::ppt_sip]->addNextThread();
				return(params->sendString("ok\n"));
			} else if(!strcasecmp(thread_params[1], "remove")) {
				preProcessPacket[PreProcessPacket::ppt_sip]->removeNextThread();
				return(params->sendString("ok\n"));
			}
		}
	#if not CALLX_MOD_OLDVER
	} else if(!strcasecmp(thread_params[0], "sip_find_callc")) {
		extern PreProcessPacket *preProcessPacket[PreProcessPacket::ppt_end_base];
		if(preProcessPacket[PreProcessPacket::ppt_pp_find_call]) {
			if(!strcasecmp(thread_params[1], "add")) {
				preProcessPacket[PreProcessPacket::ppt_pp_find_call]->addNextThread();
				return(params->sendString("ok\n"));
			} else if(!strcasecmp(thread_params[1], "remove")) {
				preProcessPacket[PreProcessPacket::ppt_pp_find_call]->removeNextThread();
				return(params->sendString("ok\n"));
			}
		}
	} else if(!strcasecmp(thread_params[0], "sip_process_callc")) {
		extern PreProcessPacket *preProcessPacket[PreProcessPacket::ppt_end_base];
		if(preProcessPacket[PreProcessPacket::ppt_pp_process_call]) {
			if(!strcasecmp(thread_params[1], "add")) {
				preProcessPacket[PreProcessPacket::ppt_pp_process_call]->addNextThread();
				return(params->sendString("ok\n"));
			} else if(!strcasecmp(thread_params[1], "remove")) {
				preProcessPacket[PreProcessPacket::ppt_pp_process_call]->removeNextThread();
				return(params->sendString("ok\n"));
			}
		}
	#endif
	} else if(!strcasecmp(thread_params[0], "ac")) {
		extern AsyncClose *asyncClose;
		if(asyncClose) {
			if(!strcasecmp(thread_params[1], "add")) {
				asyncClose->addThread();
				return(params->sendString("ok\n"));
			} else if(!strcasecmp(thread_params[1], "remove")) {
				asyncClose->removeThread();
				return(params->sendString("ok\n"));
			}
		}
	} else if(!strcasecmp(thread_params[0], "store_cdr")) {
		if(!strcasecmp(thread_params[1], "add")) {
			extern void storing_cdr_next_thread_add();
			storing_cdr_next_thread_add();
			return(params->sendString("ok\n"));
		} else if(!strcasecmp(thread_params[1], "remove")) {
			extern void storing_cdr_next_thread_remove();
			storing_cdr_next_thread_remove();
			return(params->sendString("ok\n"));
		}
	}
	return(0);
}

int Mgmt_usleep(Mgmt_params *params) {
	if (params->task == params->mgmt_task_DoInit) {
		params->registerCommand("usleep", "usleep management", true);
		return(0);
	}
	char us_params[5][100];
	for(unsigned i = 0; i < sizeof(us_params) / sizeof(us_params[0]); i++) {
		us_params[i][0] = 0;
	}
	if(strlen(params->buf) > params->command.length() + 1) {
		sscanf(params->buf + params->command.length() + 1, "%s %s %s %s %s",
		       us_params[0], us_params[1], us_params[2], us_params[3], us_params[4]);
	}
	extern string usleep_stats(unsigned int useconds_lt);
	extern void usleep_stats_clear();
	if(!strcasecmp(us_params[0], "stats_start")) {
		extern bool opt_usleep_stats;
		opt_usleep_stats = true;
		return(params->sendString("ok\n"));
	} else if(!strcasecmp(us_params[0], "stats_stop")) {
		extern bool opt_usleep_stats;
		opt_usleep_stats = false;
		usleep_stats_clear();
		return(params->sendString("ok\n"));
	} else if(!strcasecmp(us_params[0], "stats")) {
		unsigned useconds_lt = atoi(us_params[1]);
		string usleepStats = usleep_stats(useconds_lt);
		usleep_stats_clear();
		return(params->sendString(usleepStats));
	} else if(!strcasecmp(us_params[0], "stats_and_stop")) {
		extern bool opt_usleep_stats;
		unsigned useconds_lt = atoi(us_params[1]);
		string usleepStats = usleep_stats(useconds_lt);
		opt_usleep_stats = false;
		usleep_stats_clear();
		return(params->sendString(usleepStats));
	} else if(!strcasecmp(us_params[0], "set_preprocess")) {
		extern unsigned int opt_preprocess_packets_qring_usleep;
		extern unsigned int opt_preprocess_packets_qring_push_usleep;
		extern unsigned int opt_process_rtp_packets_qring_usleep;
		extern unsigned int opt_process_rtp_packets_qring_push_usleep;
		opt_preprocess_packets_qring_usleep = 
		opt_preprocess_packets_qring_push_usleep =
		opt_process_rtp_packets_qring_usleep = 
		opt_process_rtp_packets_qring_push_usleep = atoi(us_params[1]);
		return(params->sendString("ok\n"));
	} else if(!strcasecmp(us_params[0], "set_lock_calls_hash")) {
		extern unsigned int opt_lock_calls_hash_usleep;
		opt_lock_calls_hash_usleep = atoi(us_params[1]);
		return(params->sendString("ok\n"));
	} else if(!strcasecmp(us_params[0], "set_rtp")) {
		extern unsigned int rtp_qring_usleep;
		rtp_qring_usleep = atoi(us_params[1]);
		return(params->sendString("ok\n"));
	} else if(!strcasecmp(us_params[0], "set_sip_batch")) {
		extern unsigned int opt_sip_batch_usleep;
		opt_sip_batch_usleep = atoi(us_params[1]);
		return(params->sendString("ok\n"));
	} else if(!strcasecmp(us_params[0], "set_rtp_batch")) {
		extern unsigned int opt_rtp_batch_usleep;
		opt_rtp_batch_usleep = atoi(us_params[1]);
		return(params->sendString("ok\n"));
	} else if(!strcasecmp(us_params[0], "set_lock_calls")) {
		extern unsigned int opt_lock_calls_usleep;
		opt_lock_calls_usleep = atoi(us_params[1]);
		return(params->sendString("ok\n"));
	} else if(!strcasecmp(us_params[0], "set_progressive")) {
		extern bool opt_usleep_progressive;
		opt_usleep_progressive = yesno(us_params[1]);
		return(params->sendString("ok\n"));
	} else if(!strcasecmp(us_params[0], "set_all")) {
		extern unsigned int opt_usleep_force;
		opt_usleep_force = atoi(us_params[1]);
		return(params->sendString("ok\n"));
	} else if(!strcasecmp(us_params[0], "set_min")) {
		extern unsigned int opt_usleep_minimal;
		opt_usleep_minimal = atoi(us_params[1]);
		return(params->sendString("ok\n"));
	}
	return(0);
}

int Mgmt_charts_cache(Mgmt_params *params) {
	if (params->task == params->mgmt_task_DoInit) {
		commandAndHelp ch[] = {
			{"charts_cache_store_all", "charts_cache_store_all"},
			{"charts_cache_cleanup_all", "charts_cache_cleanup_all"},
			{NULL, NULL}
		};
		params->registerCommand(ch);
		return(0);
	}
	if(strstr(params->buf, "store_all") != NULL) {
		chartsCacheStore(true);
	} else if(strstr(params->buf, "cleanup_all") != NULL) {
		chartsCacheCleanup(true);
	}
	return(0);
}

int Mgmt_packetbuffer_log(Mgmt_params *params) {
	if (params->task == params->mgmt_task_DoInit) {
		commandAndHelp ch[] = {
			{"packetbuffer_log", "packetbuffer_log", true},
			{"packetbuffer_save", "packetbuffer_save", true},
			{NULL, NULL}
		};
		params->registerCommand(ch);
		return(0);
	}
	if(strstr(params->buf, "packetbuffer_log") != NULL) {
		extern PcapQueue_readFromFifo *pcapQueueQ;
		extern cBuffersControl buffersControl;
		string log = pcapQueueQ->debugBlockStoreTrash() + "\n" + buffersControl.debug();
		return(params->sendString(log));
	} else if(strstr(params->buf, "packetbuffer_save") != NULL) {
		char *nextParams = params->buf + strlen("packetbuffer_save");
		while(*nextParams == ' ') {
			++nextParams;
		}
		vector<string> nextParamsV = explode(nextParams, ' ');
		if(nextParamsV.size() >= 2) {
			extern PcapQueue_readFromFifo *pcapQueueQ;
			string rslt = pcapQueueQ->saveBlockStoreTrash(nextParamsV[0].c_str(), nextParamsV[1].c_str());
			return(params->sendString(rslt + "\n"));
		} else {
			return(params->sendString("missing parameters: filter dest_file\n"));
		}
	}
	return(0);
}

int Mgmt_diameter_packets_stack(Mgmt_params *params) {
	if (params->task == params->mgmt_task_DoInit) {
		commandAndHelp ch[] = {
			{"diameter_packets_stack", "diameter_packets_stack"},
			{"diameter_cleanup", "diameter_cleanup"},
			{NULL, NULL}
		};
		params->registerCommand(ch);
		return(0);
	}
	if(strstr(params->buf, "diameter_packets_stack") != NULL) {
		extern cDiameterPacketStack diameter_packet_stack;
		return(params->sendString(diameter_packet_stack.print_packets_stack()));
	} else if(strstr(params->buf, "diameter_cleanup") != NULL) {
		extern cDiameterPacketStack diameter_packet_stack;
		diameter_packet_stack.cleanup(getTimeUS());
		return(0);
	}
	return(0);
}

int Mgmt_memcrash_test(Mgmt_params *params) {
	if (params->task == params->mgmt_task_DoInit) {
		commandAndHelp ch[] = {
			{"memcrash_test_1", ""},
			{"memcrash_test_2", ""},
			{"memcrash_test_3", ""},
			{"memcrash_test_4", ""},
			{"memcrash_test_5", ""},
			{"memcrash_test_6", ""},
			{NULL, NULL}
		};
		params->registerCommand(ch);
		return(0);
	}

	if(strstr(params->buf, "memcrash_test_1") != NULL) {
		char *test = new FILE_LINE(13014) char[10];
		test[10] = 1;
	} else if(strstr(params->buf, "memcrash_test_2") != NULL) {
		char *test = new FILE_LINE(13015) char[10];
		delete [] test;
		delete [] test;
	} else if(strstr(params->buf, "memcrash_test_3") != NULL) {
		char *test = new FILE_LINE(13016) char[10];
		delete [] test;
		test[0] = 1;
	} else if(strstr(params->buf, "memcrash_test_4") != NULL) {
		char *test[10];
		for(int i = 0; i < 10; i++) {
			test[i] = new FILE_LINE(13017) char[10];
		}
		memset(test[4] + 10, 0, 40);
	} else if(strstr(params->buf, "memcrash_test_5") != NULL) {
		char *test = NULL;
		*test = 0;
	} else if(strstr(params->buf, "memcrash_test_6") != NULL) {
		new FILE_LINE(0) char[1000000001];
	}
	return(0);
}

int Mgmt_memalloc_test(Mgmt_params *params) {
	if (params->task == params->mgmt_task_DoInit) {
		commandAndHelp ch[] = {
			{"memalloc_alloc", ""},
			{"memalloc_free", ""},
			{NULL, NULL}
		};
		params->registerCommand(ch);
		return(0);
	}
	static queue<char*> alloc_queue;
	if(!strncmp(params->buf, "memalloc_alloc", 14)) {
		unsigned size = atoi(params->buf + 15);
		if(size > 0) {
			char *block = new FILE_LINE(0) char[size];
			if(block) {
				memset(block, 0, size);
				alloc_queue.push(block);
			}
		}
	} else if(strstr(params->buf, "memalloc_free") != NULL) {
		if(alloc_queue.size()) {
			char *block = alloc_queue.front();
			if(block) {
				delete [] block;
				alloc_queue.pop();
			}
		}
	}
	return(0);
}

int Mgmt_get_oldest_spooldir_date(Mgmt_params *params) {
	if (params->task == params->mgmt_task_DoInit) {
		params->registerCommand("get_oldest_spooldir_date", "return oldest date from spool");
		return(0);
	}
	string rslt = CleanSpool::get_oldest_date();
	if(rslt.empty()) {
		rslt = "empty";
	}
	return(params->sendString(rslt));
}

int Mgmt_get_sensor_information(Mgmt_params *params) {
	if (params->task == params->mgmt_task_DoInit) {
		params->registerCommand("get_sensor_information", "return sensor information");
		return(0);
	}
	const char *_hidePasswordForOptions[] = {
		"mysqlpassword",
		"mysqlpassword_2",
		"database_backup_from_mysqlpassword",
		"get_customer_by_ip_odbc_password",
		"get_customer_by_pn_odbc_password",
		"get_radius_ip_password",
		"odbcpass",
		"manager_sshpassword",
		"server_password",
		"cloud_token"
	};
	list<string> hidePasswordForOptions;
	for(unsigned i = 0; i < sizeof(_hidePasswordForOptions) / sizeof(_hidePasswordForOptions[0]); i++) {
		hidePasswordForOptions.push_back(_hidePasswordForOptions[i]);
	}
	char type_information[256] = "";
	char next_params[5][256];
	for(unsigned i = 0; i < sizeof(next_params) / sizeof(next_params[0]); i++) {
		next_params[i][0] = 0;
	}
	sscanf(params->buf + params->command.length() + 1, "%s %s %s %s", type_information, next_params[0], next_params[1], next_params[2]);
	if(string(next_params[0]) == "zip") {
		params->zip = true;
	}
	if(string(type_information) == "configuration_files_list" ||
	   string(type_information) == "configuration_file") {
		string config_dir = "/etc/voipmonitor/conf.d/";
		list<string> configurations;
		extern string rundir;
		extern char configfile[1024];
		string configfilepath = configfile[0] == '/' ? string(configfile) : (rundir + "/" + configfile);
		if(file_exists(configfilepath)) {
			configurations.push_back(configfilepath);
		}
		DIR* dp = opendir(config_dir.c_str());
		if(dp) {
			dirent *de;
			while((de = readdir(dp)) != NULL) {
				if(string(de->d_name) != ".." && string(de->d_name) != ".") {
					configurations.push_back(config_dir + "/" + de->d_name);
				}
			}
			closedir(dp);
		}
		if(configurations.size()) {
			if(string(type_information) == "configuration_file") {
				for(list<string>::iterator iter = configurations.begin(); iter != configurations.end(); iter++) {
					if(next_params[1] == *iter) {
						return(params->sendConfigurationFile(next_params[1], &hidePasswordForOptions));
					}
				}
				return(params->sendString("failed - access denied"));
			} else {
				string configurations_str;
				for(list<string>::iterator iter = configurations.begin(); iter != configurations.end(); iter++) {
					if(!configurations_str.empty()) {
						configurations_str += "\n";
					}
					configurations_str += *iter + ":" + intToString(GetFileSize(*iter));
				}
				return(params->sendString(configurations_str));
			}
		} else {
			return(params->sendString("failed search configurations"));
		}
	} else if(string(type_information) == "configuration_db") {
		extern int opt_mysqlloadconfig;
		if(opt_mysqlloadconfig) {
			SqlDb *sqlDb = createSqlObject();
			sqlDb->setMaxQueryPass(1);
			sqlDb->setDisableLogError();
			extern int opt_id_sensor;
			if(sqlDb->query("SELECT * FROM sensor_config WHERE id_sensor " + 
					(opt_id_sensor > 0 ? "= " + intToString(opt_id_sensor) : "IS NULL"))) {
				SqlDb_row row = sqlDb->fetchRow();
				delete sqlDb;
				if(row) {
					string result;
					for(size_t i = 0; i < row.getCountFields(); i++) {
						string column = row.getNameField(i);
						if(column != "id" && column != "id_sensor" && !row.isNull(column)) {
							if(!result.empty()) {
								result += "\n";
							}
							result += column + " = ";
							bool hidePassword = false;
							for(list<string>::iterator iter = hidePasswordForOptions.begin(); iter != hidePasswordForOptions.end(); iter++) {
								if(column == *iter) {
									hidePassword = true;
									break;
								}
							}
							if(hidePassword) {
								result += "****";
							} else {
								result += row[column];
							}
						}
					}
					return(params->sendString(result));
				} else {
					return(params->sendString("failed - not exists data in table sensor_config for sensor"));
				}
			} else {
				delete sqlDb;
				return(params->sendString("failed load data from table sensor_config"));
			}
		} else {
			return(params->sendString("failed - need active new config and enable mysqlloadconfig"));
		}
	} else if(string(type_information) == "configuration_active") {
		extern cConfig CONFIG;
		string contentConfig = CONFIG.getContentConfig(true, false);
		vector<string> contentConfigSplit = split(contentConfig, '\n');
		for(unsigned i = 0; i < contentConfigSplit.size(); i++) {
			size_t optionSeparatorPos = contentConfigSplit[i].find('=');
			if(optionSeparatorPos != string::npos) {
				string option = trim_str(contentConfigSplit[i].substr(0, optionSeparatorPos));
				string value = trim_str(contentConfigSplit[i].substr(optionSeparatorPos + 1));
				for(list<string>::iterator iter = hidePasswordForOptions.begin(); iter != hidePasswordForOptions.end(); iter++) {
					if(option == *iter) {
						contentConfigSplit[i] = option + " = ****";
						break;
					}
				}
			}
		}
		contentConfig = "";
		for(unsigned i = 0; i < contentConfigSplit.size(); i++) {
			if(i) {
				contentConfig += '\n';
			}
			contentConfig += contentConfigSplit[i];
		}
		return(params->sendString(contentConfig));
	} else if(string(type_information) == "syslog_files_list" ||
		  string(type_information) == "syslog_file") {
		string syslog_dir = "/var/log";
		list<string> syslogs;
		DIR* dp = opendir(syslog_dir.c_str());
		if(dp) {
			dirent *de;
			while((de = readdir(dp)) != NULL) {
				if(string(de->d_name) != ".." && string(de->d_name) != "." &&
				   (strstr(de->d_name, "messages") ||
				    strstr(de->d_name, "syslog"))) {
					syslogs.push_back(syslog_dir + "/" + de->d_name);
				}
			}
			closedir(dp);
		}
		if(syslogs.size()) {
			if(string(type_information) == "syslog_file") {
				for(list<string>::iterator iter = syslogs.begin(); iter != syslogs.end(); iter++) {
					if(next_params[1] == *iter) {
						return(params->sendFile(next_params[1], next_params[2][0] ? atoll(next_params[2]) : 0));
					}
				}
				return(params->sendString("failed - access denied"));
			} else {
				string syslogs_str;
				for(list<string>::iterator iter = syslogs.begin(); iter != syslogs.end(); iter++) {
					if(!syslogs_str.empty()) {
						syslogs_str += "\n";
					}
					syslogs_str += *iter + ":" + intToString(GetFileSize(*iter));
				}
				return(params->sendString(syslogs_str));
			}
		} else {
			return(params->sendString("failed search syslogs"));
		}
	} else if(string(type_information) == "interfaces") {
		extern char ifname[1024];
		vector<string> interfaces = split(ifname, split(",|;| |\t|\r|\n", "|"), true);
		if(interfaces.size()) {
			string interfaces_str;
			for(unsigned i = 0; i < interfaces.size(); i ++) {
				if(!interfaces_str.empty()) {
					interfaces_str += "\n";
				}
				for(unsigned j = 0; j < interfaces[i].length() + 2; j++) {
					interfaces_str += '-';
				}
				interfaces_str += "\n|" + interfaces[i] + "|\n";
				for(unsigned j = 0; j < interfaces[i].length() + 2; j++) {
					interfaces_str += '-';
				}
				interfaces_str += "\n\n";
				string cmd = "ip addr show " + interfaces[i];
				int exitCode;
				SimpleBuffer out;
				SimpleBuffer err;
				vm_pexec(cmd.c_str(), &out, &err, &exitCode);
				if(exitCode == 0 && out.size()) {
					interfaces_str += (char*)out;
					const char *nextCommands[] = {
						"ethtool -i",
						"ethtool -g",
						"ethtool -c",
						"ethtool -S",
						"ip -s -s l l"
					};
					for(unsigned j = 0; j < sizeof(nextCommands) / sizeof(nextCommands[0]); j++) {
						string cmd = nextCommands[j] + string(" ") + interfaces[i];
						int exitCode;
						SimpleBuffer out;
						SimpleBuffer err;
						vm_pexec(cmd.c_str(), &out, &err, &exitCode);
						if(exitCode == 0 && out.size()) {
							interfaces_str += "\n" + string((char*)out);
						} else if(err.size()) {
							interfaces_str += "\n" + string((char*)err);
						}
					}
				} else {
					interfaces_str += "failed " + cmd + "\n";
					if(err.size()) {
						interfaces_str += (char*)err;
						if(interfaces_str[interfaces_str.length() - 1] != '\n') {
							interfaces_str += "\n";
						}
					}
				}
			}
			return(params->sendString(interfaces_str));
		} else {
			return(params->sendString("no interfaces"));
		}
	} else if(string(type_information) == "proc_cpuinfo") {
		return(params->sendFile("/proc/cpuinfo"));
	} else if(string(type_information) == "proc_meminfo") {
		return(params->sendFile("/proc/meminfo"));
	} else if(string(type_information) == "cmd_df") {
		return(params->sendPexecOutput("df -h"));
	} else if(string(type_information) == "cmd_mount") {
		return(params->sendPexecOutput("mount"));
	} else if(string(type_information) == "cmd_dmesg") {
		return(params->sendPexecOutput("dmesg -t"));
	} else if(string(type_information) == "cmd_file") {
		extern string rundir;
		extern string appname;
		return(params->sendPexecOutput(("file " + binaryNameWithPath).c_str()));
	} else if(string(type_information) == "cmd_ldd") {
		return(params->sendPexecOutput(("ldd " + binaryNameWithPath).c_str()));
	}
	return(0);
}

int Mgmt_set_pcap_stat_period(Mgmt_params *params) {
	if (params->task == params->mgmt_task_DoInit) {
		params->registerCommand("set_pcap_stat_period", "set_pcap_stat_period");
		return(0);
	}
	int new_pcap_stat_period = atoi(params->buf + 21);
	if(new_pcap_stat_period > 0 && new_pcap_stat_period < 600) {
		sverb.pcap_stat_period = new_pcap_stat_period;
	}
	return(0);
}

int Mgmt_setverbparam(Mgmt_params *params) {
	if (params->task == params->mgmt_task_DoInit) {
		params->registerCommand("setverbparam", "setverbparam");
		return(0);
	}

	extern void parse_verb_param(string verbParam);
	string verbparam = params->buf + 13;
	size_t posEndLine = verbparam.find("\n");
	if(posEndLine != string::npos) {
		verbparam.resize(posEndLine);
	}
	parse_verb_param(verbparam);
	return(0);
}

int Mgmt_cleanverbparams(Mgmt_params *params) {
	if (params->task == params->mgmt_task_DoInit) {
		params->registerCommand("cleanverbparams", "cleanverbparams");
		return(0);
	}

	sverb.disable_process_packet_in_packetbuffer = 0;
	sverb.disable_push_to_t2_in_packetbuffer = 0;
	
	return(0);
}

int Mgmt_unpausecall(Mgmt_params *params) {
	if (params->task == params->mgmt_task_DoInit) {
		params->registerCommand("unpausecall", "unpause call's processing");
		return(0);
	}

	long long callref = 0;
	sscanf(params->buf, "unpausecall 0x%llx", &callref);
	if (!callref) {
		return(params->sendString("Bad/missing Call id\n"));
	} else if (Handle_pause_call(callref, 0) == -1) {
		return(params->sendString("Call id not found\n"));
	}
	return(0);
}

int Mgmt_pausecall(Mgmt_params *params) {
	if (params->task == params->mgmt_task_DoInit) {
		params->registerCommand("pausecall", "pause call's processing");
		return(0);
	}

	long long callref = 0;
	sscanf(params->buf, "pausecall 0x%llx", &callref);
	if (!callref) {
		return(params->sendString("Bad/missing Call id\n"));
	} else if (Handle_pause_call(callref, 1) == -1) {
		return(params->sendString("Call id not found\n"));
	}
	return(0);
}

int Mgmt_aes(Mgmt_params *params) {
	if (params->task == params->mgmt_task_DoInit) {
		commandAndHelp ch[] = {
			{"need_aes", NULL, true},
			{"failed_aes_decrypt", NULL, true},
			{"support_aes", NULL, true},
			{"exists_aes_key", NULL, true},
			{NULL, NULL}
		};
		params->registerCommand(ch);
		return(0);
	}
	if(strstr(params->buf, "need_aes") != NULL) {
		params->sendString("need aes!\n");
	} else if(strstr(params->buf, "failed_aes_decrypt") != NULL) {
		params->sendString("failed aes decrypt!\n");
	} else if(strstr(params->buf, "support_aes") != NULL) {
		params->sendString(is_support_manager_aes() ? "yes" : "no");
	} else if(strstr(params->buf, "exists_aes_key") != NULL) {
		params->sendString(cManagerAes::getAesKey(NULL, true) ? "yes" : "no");
	}
	return(0);
}

int Mgmt_manager_file(Mgmt_params *params) {
	if (params->task == params->mgmt_task_DoInit) {
		params->registerCommand("manager_file", NULL, true);
		return(0);
	}
	char *p;
	if((p = strstr(params->buf, "start")) != NULL) {
		string file, error;
		if(strlen(p) > 6 && p[5] == ' ') {
			file = p + 6;
		}
		if(manager_file_server_start(&file, &error)) {
			opt_manager_socket_run_via_manager = file;
			params->sendString("OK (" + file + ")\n");
		} else {
			params->sendString(error + "\n");
		}
	} else if(strstr(params->buf, "stop") != NULL) {
		string error;
		if(manager_file_server_stop(&error)) {
			params->sendString("OK\n");
		} else {
			params->sendString(error + "\n");
		}
	}
	return(0);
}

int Mgmt_ssl_ipport(Mgmt_params *params) {
	if (params->task == params->mgmt_task_DoInit) {
		params->registerCommand("ssl_ipport", "modification of the ssl_ipport parameter (list, set, add, del)", false);
		return(0);
	}
	unsigned command_length = 10;
	if(strlen(params->buf + command_length) > 1) {
		if(!strcmp(params->buf + command_length + 1, "list")) {
			params->sendString(ssl_ipport_list());
		} else if(!strncmp(params->buf + command_length + 1, "set", 3)) {
			command_length += 1 + 3;
			if(strlen(params->buf + command_length) > 1) {
				bool rslt = ssl_ipport_set(params->buf + command_length + 1);
				params->sendString(rslt ? "OK\n" : "FAILED\n");
			}
		} else if(!strncmp(params->buf + command_length + 1, "add", 3)) {
			command_length += 1 + 3;
			if(strlen(params->buf + command_length) > 1) {
				bool rslt = ssl_ipport_add(params->buf + command_length + 1);
				params->sendString(rslt ? "OK\n" : "FAILED\n");
			}
		} else if(!strncmp(params->buf + command_length + 1, "del", 3)) {
			command_length += 1 + 3;
			if(strlen(params->buf + command_length) > 1) {
				bool rslt = ssl_ipport_del(params->buf + command_length + 1);
				params->sendString(rslt ? "OK\n" : "FAILED\n");
			}
		} else {
			params->sendString("bad command - commands: list, set, add, del\n");
		}
	} else {
		params->sendString("missing command - commands: list, set, add, del\n");
	}
	return(0);
}

int Mgmt_sql_errors_skip(Mgmt_params *params) {
	if (params->task == params->mgmt_task_DoInit) {
		params->registerCommand("sql_errors_skip", "set parameter sql_errors_skip", true);
		return(0);
	}
	extern char opt_sql_errors_skip[1024];
	unsigned command_length = 15;
	if(strlen(params->buf + command_length) > 1) {
		strcpy_null_term(opt_sql_errors_skip, params->buf + command_length + 1);
	} else {
		opt_sql_errors_skip[0] = 0;
	}
	params->sendString(string("sql_errors_skip: ") + opt_sql_errors_skip + "\n");
	return(0);
}

void init_management_functions(void) {
	if(!MgmtCmdsRegTable.empty()) {
		return;
	}
	Mgmt_params params(NULL, 0, 0, NULL, NULL, NULL, NULL);
	params.task = params.mgmt_task_DoInit;
	for(unsigned i = 0; i < sizeof(MgmtFuncArray) / sizeof(MgmtFuncArray[0]); i++) {
		params.mgmtFce = MgmtFuncArray[i];
		MgmtFuncArray[i](&params);
	}
}
