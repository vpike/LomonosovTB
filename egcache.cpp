#include <stdlib.h>
#include "egcache.h"
#include "egcachecontrol.h"
#include "egpglobals.h"

void lru_cache::set_capacity(unsigned long long bytes, bool blocking) {
	if (blocking) lock(CAPACITY, WRLOCK);
	capacity = bytes;
	if (blocking) lock(CAPACITY, UNLOCK);
}

void lru_cache::insert(unsigned long index, unsigned long long piece_number, int table_type, char *buffer, unsigned long buf_size, int bufferizer) {
	lock(CAPACITY, RDLOCK);
	if (buf_size > capacity << 20)
		return;
	lock(CAPACITY, UNLOCK);
#ifdef _M_X64
	key_type key = (((key_type)table_type) << 58) | (((key_type)index) << 42) | ((key_type)piece_number);
#else
	key_type key = make_pair((((unsigned long)table_type) << 16) | index, piece_number);
#endif
	lock(KEY_TO_VALUE, WRLOCK);
	if (key_to_value.find(key) != key_to_value.end()) {
		lock(KEY_TO_VALUE, UNLOCK);
		return;
	}
	cache_block_t cache_block;
	cache_block.block = (char*)malloc((buf_size + 1) * sizeof(char));
	memcpy(cache_block.block + 1, buffer, buf_size);	
	cache_block.block[0] = 1;
	cache_block.bufferizer = bufferizer;
	cache_block.buf_size = buf_size;
	lock(CAPACITY, RDLOCK);
	if (!memory_allocation_done && size >= capacity) {
		lock(CAPACITY, UNLOCK);
		allocate_cache_memory();
		lock(CAPACITY, RDLOCK);
	}
	unsigned long long cache_size;
#ifdef SAME_BLOCKS
	cache_size = key_tracker.size() * buf_size;
#else
	cache_size = size;
#endif
	while (cache_size >= capacity) {
		list<key_type>::iterator it = key_tracker.begin();
#ifdef _M_X64
		unordered_map<key_type, value_type>::iterator it_map = key_to_value.find(*it);
#else
		map<key_type, value_type>::iterator it_map = key_to_value.find(*it);
#endif
		cache_size -= it_map->second.first.buf_size;
		char *block = it_map->second.first.block;
		// decrement counter
		mutex_lock(block_counter_mutexer);
		block[0]--;
		if (block[0] == 0)
			free(block);
		mutex_unlock(block_counter_mutexer);

		key_to_value.erase(it_map);
		key_tracker.pop_front();

		--number_in_cache;
		++number_pop_from_cache;
	}
#ifndef SAME_BLOCKS
	size = cache_size + buf_size;
#endif
	lock(CAPACITY, UNLOCK);
	key_tracker.push_back(key);
	key_to_value.insert(make_pair(key, make_pair(cache_block, --key_tracker.end())));
	++number_in_cache;
	lock(KEY_TO_VALUE, UNLOCK);
	lock(NUMBER_LOADF, WRLOCK);
	number_load_from_file++;
	lock(NUMBER_LOADF, UNLOCK);
}

bool lru_cache::find(unsigned long index, unsigned long long piece_number, int table_type, char **buffer, unsigned long *buf_size) {
#ifdef _M_X64
	key_type key = (((key_type)table_type) << 58) | (((key_type)index) << 42) | ((key_type)piece_number);
#else
	key_type key = make_pair((((unsigned long)table_type) << 16) | index, piece_number);
#endif
	lock(KEY_TO_VALUE, RDLOCK);
#ifdef _M_X64
	unordered_map<key_type, value_type>::iterator it = key_to_value.find(key);
#else
	map<key_type, value_type>::iterator it = key_to_value.find(key);
#endif
	bool find_cache = (it != key_to_value.end());
	if (find_cache) {
		*buffer = it->second.first.block + 1;
		*buf_size = it->second.first.buf_size;
		lock(KEY_TRACKER, WRLOCK);
		key_tracker.splice(key_tracker.end(), key_tracker, it->second.second);
		lock(KEY_TRACKER, UNLOCK);
		lock(KEY_TO_VALUE, UNLOCK);
		lock(NUMBER_LOADC, WRLOCK);
		number_load_from_cache++;
		lock(NUMBER_LOADC, UNLOCK);
		return true;
	}
	lock(KEY_TO_VALUE, UNLOCK);
	return false;
}

void lru_cache::clean(int table_type) {
	lock(KEY_TO_VALUE, WRLOCK);
#ifdef _M_X64
	unordered_map<key_type, value_type>::iterator it;
	unordered_map<key_type, value_type>::iterator it_for_delete;
#else
	map<key_type, value_type>::iterator it;
	map<key_type, value_type>::iterator it_for_delete;
#endif
	int type_of_key;
	for (it = key_to_value.begin(); it != key_to_value.end(); ) {
#ifdef _M_X64
		type_of_key = it->first >> 58;
#else
		type_of_key = it->first.first >> 16;
#endif
		if (table_type == type_of_key) {
			it_for_delete = it;
			it++;
			size -= it_for_delete->second.first.buf_size;
			key_tracker.erase(it_for_delete->second.second);
			free(it_for_delete->second.first.block);
			key_to_value.erase(it_for_delete);
			number_in_cache--;
			number_pop_from_cache++;
		}
		else
			it++;
	}
	lock(KEY_TO_VALUE, UNLOCK);
}

void lru_cache::clean_all() {
	lock(KEY_TO_VALUE, WRLOCK);
#ifdef _M_X64
	unordered_map<key_type, value_type>::iterator it;
#else
	map<key_type, value_type>::iterator it;
#endif
	mutex_lock(block_counter_mutexer);
	for (it = key_to_value.begin(); it != key_to_value.end(); it++ ) {
		// decrement counter
		char *block = it->second.first.block;
		block[0]--;
		if (block[0] == 0)
			free(block);
	}
	mutex_unlock(block_counter_mutexer);
	key_to_value.clear();
	key_tracker.clear();
	number_pop_from_cache += number_in_cache;
	number_in_cache = 0;
	size = 0;
	lock(KEY_TO_VALUE, UNLOCK);
}

void lru_cache::lock(int rwlock_kind, int kind) {
#ifdef PARALLEL_TB
	pthread_rwlock_t *rwlock;
	switch (rwlock_kind) {
	case KEY_TRACKER: rwlock = &key_tracker_lock; break;
	case KEY_TO_VALUE: rwlock = &key_to_value_lock; break;
	case NUMBER_IN: rwlock = &number_in_lock; break;
	case NUMBER_POP: rwlock = &number_pop_lock; break;
	case NUMBER_LOADC: rwlock = &number_loadc_lock; break;
	case NUMBER_LOADF: rwlock = &number_loadf_lock; break;
	case CAPACITY: rwlock = &capacity_lock; break;
	}
	switch (kind) {
	case WRLOCK: pthread_rwlock_wrlock(rwlock); break;
	case RDLOCK: pthread_rwlock_rdlock(rwlock); break;
	case UNLOCK: pthread_rwlock_unlock(rwlock); break;
	}
#endif
}

void lru_cache::rehash() {
#ifdef _M_X64
	unsigned long min_block = 0;
	for (int i = MIN_TYPE; i <= MAX_TYPE; i++) {
		if ((min_block_size[i] != 0) && (min_block == 0 || min_block > min_block_size[i]))
			min_block = min_block_size[i];
	}
	if (min_block <= 0)
		return;
	// size of bucket
	size_t size_of_bucket = min_block + 16;
	lock(CAPACITY, RDLOCK);
	size_t max_count = total_cache_size / size_of_bucket;
	lock(CAPACITY, UNLOCK);
	lock(KEY_TO_VALUE, WRLOCK);
	key_to_value.rehash(max_count);
	size_t buckets_size = key_to_value.bucket_count() << 4;
	total_cache_size -= buckets_size;
	allocate_cache_memory();
	lock(KEY_TO_VALUE, UNLOCK);
	if (logging_memory) {
		char log_filename[MAX_PATH];
		sprintf(log_filename, "%s%s", path_to_logs, "memory_log.log");
		FILE *f = fopen(log_filename, "a");
		fprintf(f, "Min size of block = %d bytes = %d Kb\n", (int)size_of_bucket, (int)(size_of_bucket >> 10));
		fprintf(f, "Size of unordered_map = %d bytes = %d Mb\n", (int)buckets_size, (int)(buckets_size >> 20));
		fclose(f);
	}
#endif
}
