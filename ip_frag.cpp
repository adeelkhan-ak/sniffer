#include "voipmonitor.h"

#include "ip_frag.h"


#if not DEFRAG_MOD_OLDVER

cIpFrag::cIpFrag(unsigned fdata_threads_split) {
	this->fdata_threads_split = fdata_threads_split > 1 ? fdata_threads_split : 1;
	fdata = new sDefrag[this->fdata_threads_split];
}

cIpFrag::~cIpFrag() {
	cleanup(0, true, -1, 0);
	delete [] fdata;
}

void cIpFrag::cleanup(unsigned int tv_sec, bool all,
		      int pushToStack_queue_index, int cleanup_limit) {
	if(cleanup_limit < 0) {
		cleanup_limit = 30;
	}
	for(unsigned fdata_thread_index = 0; fdata_thread_index < fdata_threads_split; fdata_thread_index++) {
		for(map<pair<vmIP, u_int32_t>, sFrags*>::iterator it_d = fdata[fdata_thread_index].begin(); it_d != fdata[fdata_thread_index].end(); ) {
			sFrags *frags = it_d->second;
			if(frags->size() &&
			   (all ||
			    ((tv_sec - frags->begin()->second->ts) > cleanup_limit))) {
				for(map<u_int16_t, sFrag*>::iterator it_s = frags->begin(); it_s != frags->end(); it_s++) {
					it_s->second->destroy(pushToStack_queue_index);
				}
				frags->clear();
			}
			if(!frags->size()) {
				fdata[fdata_thread_index].erase(it_d++);
				delete frags;
			} else {
				it_d++;
			}
		}
	}
}

#endif
