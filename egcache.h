#ifndef EGCACHE_H_
#define EGCACHE_H_

#ifndef DISABLE_PARALLEL_TB
#define PARALLEL_TB
#include <pthread.h>
#endif

#include <cassert>
#include <list>
#ifdef _M_X64
#include <unordered_map>
#else
#include <map>
#endif

#define KEY_TRACKER 0
#define KEY_TO_VALUE 1
#define NUMBER_IN 2
#define NUMBER_POP 3
#define NUMBER_LOADC 4
#define NUMBER_LOADF 5
#define CAPACITY 6

#define WRLOCK 0
#define RDLOCK 1
#define UNLOCK 2

using namespace std;

typedef struct {
	char *block;
#ifndef SAME_BLOCKS
	unsigned long buf_size;
#endif
	int bufferizer;
} cache_block_t;

#ifdef _M_X64
typedef unsigned long long key_type;
/* key contains 
	piece_number in 42 last bits, 
	table_index in 58 - 43 (16 bits) bits from end
	if_ternary in 59 bit from end*/
#else
typedef pair<unsigned long, unsigned long long> key_type;
// first contains table_index in 16 last bits and if_ternary in 17 bit from end
#endif
typedef pair<cache_block_t, list<key_type>::iterator> value_type;

class lru_cache {
private:
	unsigned long long number_load_from_cache;
	unsigned long long number_load_from_file;
    unsigned long long number_pop_from_cache;
	unsigned long long number_in_cache;
#ifdef PARALLEL_TB
	pthread_rwlock_t key_tracker_lock;
	pthread_rwlock_t key_to_value_lock;
	pthread_rwlock_t number_in_lock;
	pthread_rwlock_t number_pop_lock;
	pthread_rwlock_t number_loadc_lock;
	pthread_rwlock_t number_loadf_lock;
	pthread_rwlock_t capacity_lock;
#endif
	unsigned long long capacity;
#ifndef SAME_BLOCKS
	unsigned long long size;
#endif
	list<key_type> key_tracker;
#ifdef _M_X64
	unordered_map<key_type, value_type> key_to_value;
#else
	map<key_type, value_type> key_to_value;
#endif
public:
	lru_cache() {
		capacity = 1024 << 20;
		number_load_from_cache = 0;
		number_load_from_file = 0;
		number_in_cache = 0;
		number_pop_from_cache = 0;
#ifndef SAME_BLOCKS
		size = 0;
#endif
#ifdef PARALLEL_TB
		pthread_rwlock_init(&key_tracker_lock, NULL);
		pthread_rwlock_init(&key_to_value_lock, NULL);
		pthread_rwlock_init(&number_in_lock, NULL);
		pthread_rwlock_init(&number_pop_lock, NULL);
		pthread_rwlock_init(&number_loadc_lock, NULL);
		pthread_rwlock_init(&number_loadf_lock, NULL);
		pthread_rwlock_init(&capacity_lock, NULL);
#endif
	}
	void set_capacity(unsigned long long bytes, bool blocking = true);
	void insert(unsigned long index, unsigned long long piece_number, int table_type, char *buffer, unsigned long buf_size, int bufferizer);
	bool find(unsigned long index, unsigned long long piece_number, int table_type, char **buffer, unsigned long *buf_size);
	void clean(int table_type);
	void clean_all();
	void lock(int rwlock_kind, int kind);
	unsigned long long get_size(bool blocking = true) {
#ifndef SAME_BLOCKS
		if (blocking) lock(CAPACITY, RDLOCK);
		unsigned long long result = size;
		if (blocking) lock(CAPACITY, UNLOCK);
		return result;
#else
		return key_tracker.size() * key_to_value.begin()->second.first.buf_size;
#endif
	}
	unsigned long long get_number_load_from_file() {
		lock(NUMBER_LOADF, RDLOCK);
		unsigned long long result = number_load_from_file;
		lock(NUMBER_LOADF, UNLOCK);
		return result;
	}
	unsigned long long get_number_load_from_cache() {
		lock(NUMBER_LOADC, RDLOCK);
		unsigned long long result = number_load_from_cache;
		lock(NUMBER_LOADC, UNLOCK);
		return result;
	}
	unsigned long long get_number_in_cache() {
		lock(NUMBER_IN, RDLOCK);
		unsigned long long result = number_in_cache;
		lock(NUMBER_IN, UNLOCK);
		return result;
	}
	unsigned long long get_number_pop_from_cache() {
		lock(NUMBER_POP, RDLOCK);
		unsigned long long result = number_pop_from_cache;
		lock(NUMBER_POP, UNLOCK);
		return result;
	}
	void rehash();
};

#endif

