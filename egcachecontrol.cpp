#include "egcachecontrol.h"
#include "egpglobals.h"

lru_cache global_cache;

unordered_map<unsigned long, bufferizer_list<read_file_bufferizer> *> read_file_bufferizers;
unordered_map<unsigned long, bufferizer_list<cache_file_bufferizer> *> cache_file_bufferizers[TYPES_COUNT];
unordered_map<unsigned long, custom_tb_indexer *> tb_indexers;
unordered_map<unsigned long, piece_offsets_list *> piece_offsets_map;

unsigned long long auto_clean_count = 0;
bool memory_allocation_done = false;
unsigned long long total_cache_size = (unsigned long long)2048 << 20;
unsigned long long max_hidden_size = 1024 << 20;
unsigned long long cur_hidden_size = 0;
int clean_percent = 5;
#ifdef LOG_HIDDEN
void log_cur_hidden_size(char *message, int dif) {
	FILE *f = fopen("log_cur_hidden_size.txt", "a");
	fprintf(f, "%lld; %s: %d\n", cur_hidden_size, message, dif);
	fclose(f);
}
#endif
int max_requests = 1;
int max_requests_rd_buf = 1;

unsigned long long get_indexers_size() {
	unsigned long long result = 0;
	unordered_map<unsigned long, custom_tb_indexer *>::iterator it;
	rwlock_rdlock(tb_indexers_locker);
	for (it = tb_indexers.begin(); it != tb_indexers.end(); it++) {
		result += it->second->get_size();
	}
	rwlock_unlock(tb_indexers_locker);
	return result;
}

unsigned long long get_bufferizers_size(int table_type) {
	unsigned long long result = 0;
	unordered_map<unsigned long, bufferizer_list<cache_file_bufferizer> *>::iterator it;
	unordered_map<unsigned long, bufferizer_list<cache_file_bufferizer> *> *bufferizers;
	bufferizers = &cache_file_bufferizers[table_type];
	rwlock_rdlock(cache_file_buf_locker);
	for (it = bufferizers->begin(); it != bufferizers->end(); it++) {
		bufferizer_list<cache_file_bufferizer> *buf_list = it->second->root;
		while (buf_list != NULL) {
			result += buf_list->bufferizer->get_size();
			buf_list = buf_list->next;
		}
	}
	rwlock_unlock(cache_file_buf_locker);
	return result;
}

unsigned long long get_bufferizers_size_all() {
	unsigned long long result = 0;
	for (int i = MIN_TYPE; i <= MAX_TYPE; i++)
		result += get_bufferizers_size(i);
	return result;
}

unsigned long long get_rd_bufferizers_size() {
	unsigned long long result = 0;
	unordered_map<unsigned long, bufferizer_list<read_file_bufferizer> *>::iterator it;
	rwlock_rdlock(read_file_buf_locker);
	for (it = read_file_bufferizers.begin(); it != read_file_bufferizers.end(); it++) {
		bufferizer_list<read_file_bufferizer> *buf_list = it->second->root;
		while (buf_list != NULL) {
			//result += sizeof(bufferizer_list);
			result += buf_list->bufferizer->get_size();
			buf_list = buf_list->next;
		}
	}
	rwlock_unlock(read_file_buf_locker);
	return result;
}

unsigned long long get_piece_offsets_size() {
	unsigned long long result = 0;
	unordered_map<unsigned long, piece_offsets_list *>::iterator it;
	rwlock_rdlock(piece_offsets_locker);
	for (it = piece_offsets_map.begin(); it != piece_offsets_map.end(); it++) {
		piece_offsets_list *offsets_list = it->second;
		while (offsets_list != NULL) {
			result += offsets_list->count * sizeof(file_offset);
			offsets_list = offsets_list->next;
		}
	}
	rwlock_unlock(piece_offsets_locker);
	return result;
}

#include <time.h>

void clean_hidden_cache() {
	rwlock_unlock(probe_locker);
	rwlock_wrlock(probe_locker);
	mutex_lock(auto_clean_count_mutexer);
	auto_clean_count++;
	mutex_unlock(auto_clean_count_mutexer);
	FILE *f;
	clock_t t1, t2;
	if (logging_memory) {
		char log_filename[MAX_PATH];
		sprintf(log_filename, "%s%s", path_to_logs, "cleaning.log");
		f = fopen(log_filename, "a");
		time_t t_begin;
		time(&t_begin);
		t1 = clock();
		//fprintf(f, "%s: Real size = %lld\ncleaning from %lld Bytes to ", ctime(&t_begin), get_indexers_size() + get_bufferizers_size(false) + get_bufferizers_size(true), cur_hidden_size);
		fprintf(f, "%s: cleaning from %lld Mb to ", ctime(&t_begin), cur_hidden_size >> 20);
	}
	unordered_map<unsigned long, bufferizer_list<cache_file_bufferizer> *>::iterator it;
	unordered_map<unsigned long, custom_tb_indexer *>::iterator it_indexer;
	unordered_map<unsigned long, piece_offsets_list *>::iterator it_offsets;
	unordered_map<unsigned long, bufferizer_list<read_file_bufferizer> *>::iterator it_read_buf;
	unsigned long indexer_index;
	for (int table_type = MIN_TYPE; table_type <= MAX_TYPE; table_type++) {
		it = cache_file_bufferizers[table_type].begin();
		while (it != cache_file_bufferizers[table_type].end()) {
			bufferizer_list<cache_file_bufferizer> *buf_list = it->second;
			bufferizer_list<cache_file_bufferizer> *root = NULL;
			if (!MUTABLE_TYPE(table_type))
				indexer_index = it->first >> 1;
			else
				indexer_index = (table_type << 16) | it->first;
			while (buf_list != NULL) {
				if (buf_list->requests * 100 < max_requests * clean_percent) {
					bufferizer_list<cache_file_bufferizer> *tmp = buf_list;
					while (tmp != NULL) {
						cur_hidden_size -= tmp->bufferizer->get_size();
						tmp = tmp->next;
					}
					if (root == NULL) {
						//delete itself
						delete buf_list;
						//delete indexer
						it_indexer = tb_indexers.find(indexer_index);
						if (it_indexer != tb_indexers.end()) {
							cur_hidden_size -= it_indexer->second->get_size();
							delete it_indexer->second;
							tb_indexers.erase(it_indexer);
						}
						//delete piece_offsets
						unsigned long key = (((unsigned long)table_type) << 16) | it->first;
						it_offsets = piece_offsets_map.find(key);
						if (it_offsets != piece_offsets_map.end()) {
							piece_offsets_list *list = it_offsets->second;
							while (list != NULL) {
								cur_hidden_size -= list->count * sizeof(file_offset);
								list = list->next;
							}
							delete it_offsets->second;
							piece_offsets_map.erase(it_offsets);
						}
						//delete read_file_bufferizers
						it_read_buf = read_file_bufferizers.find(key);
						if (it_read_buf != read_file_bufferizers.end()) {
							bufferizer_list<read_file_bufferizer> *list = it_read_buf->second;
							while (list != NULL) {
								cur_hidden_size -= list->bufferizer->get_size();
								list = list->next;
							}
							delete it_read_buf->second;
							read_file_bufferizers.erase(it_read_buf);
						}
						//delete from map
						cache_file_bufferizers[table_type].erase(it++);
						break;
					}
					else {
						root->next = NULL;
						delete buf_list;
						break;
					}
				}
				else {
					root = buf_list;
					buf_list->requests = 0;
				}
				buf_list = buf_list->next;
			}
			if (root != NULL)
				it++;
		}
	}

	unordered_map<unsigned long, bufferizer_list<read_file_bufferizer> *>::iterator it_rd = read_file_bufferizers.begin();
	while (it_rd != read_file_bufferizers.end()) {
		bufferizer_list<read_file_bufferizer> *buf_list = it_rd->second;
		bufferizer_list<read_file_bufferizer> *root = NULL;
		while (buf_list != NULL) {
			if (buf_list->requests * 100 < max_requests_rd_buf * clean_percent) {
				bufferizer_list<read_file_bufferizer> *tmp = buf_list;
				while (tmp != NULL) {
					cur_hidden_size -= tmp->bufferizer->get_size();
					tmp = tmp->next;
				}
				if (root == NULL) {
					delete buf_list;
					read_file_bufferizers.erase(it_rd++);
					break;
				}
				else {
					root->next = NULL;
					delete buf_list;
					break;
				}
			}
			else {
				root = buf_list;
				buf_list->requests = 1;
			}
			buf_list = buf_list->next;
		}
		if (root != NULL)
			it_rd++;
	}

	if (logging_memory) {
		t2 = clock();
		fprintf(f, "%lld Mb. Second taken: %5.3lf\n", cur_hidden_size >> 20, ((float)(t2 - t1))/CLOCKS_PER_SEC);
		//fprintf(f, "Real hidden size = %lld Bytes\n", get_indexers_size() + get_bufferizers_size(false) + get_bufferizers_size(true));
		fclose(f);
	}
	max_requests = 1;
	max_requests_rd_buf = 1;
	rwlock_unlock(probe_locker);
	rwlock_rdlock(probe_locker);
}

void allocate_cache_memory() {
	rwlock_wrlock(hidden_locker);
	global_cache.lock(CAPACITY, WRLOCK);
	unsigned long long hidden_size = cur_hidden_size;
	unsigned long long cache_size = global_cache.get_size(false);
	long long free_size = total_cache_size - hidden_size - cache_size;
	if (free_size <= (5 << 20)) {
		// last 5 MB to least
		if (cache_size < hidden_size)
			cache_size += free_size;
		memory_allocation_done = true;
	} else
		// 3/4 to hidden and 1/4 to cache
		cache_size += (free_size >> 2);
	cache_size &= ((unsigned long long)(-1)) << 20;
	// cache cannot have size 0
	if (cache_size == 0)
		cache_size = 2 << 20;
	hidden_size = total_cache_size - cache_size;
	global_cache.set_capacity(cache_size, false);
	max_hidden_size = hidden_size;
	if (logging_memory) {
		char log_filename[MAX_PATH];
		sprintf(log_filename, "%s%s", path_to_logs, "realloc_cache.log");
		FILE *log = fopen(log_filename, "a");
		time_t t_begin;
		time(&t_begin);
		fprintf(log, "%s: cache memory reallocated\n", ctime(&t_begin));
		fprintf(log, "Max for hidden cache = %lld Mb\n", hidden_size >> 20);
		fprintf(log, "Max for cache = %lld Mb\n", cache_size >> 20);
		fclose(log);
	}
	global_cache.lock(CAPACITY, UNLOCK);
	rwlock_unlock(hidden_locker);
}
