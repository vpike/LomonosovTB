#ifndef EGCACHECONTROL_H_
#define EGCACHECONTROL_H_

#ifdef _MSC_VER
#include <unordered_map>
#else
#include <tr1/unordered_map>
using namespace std::tr1;
#endif
#include "egmaintypes.h"
#include "egfilebuf.h"
#include "egcache.h"
#include "egcachebuf.h"

using namespace std;

class piece_offsets_list {
public:
	piece_offsets_list *next;
	file_offset *piece_offsets;
	unsigned int file_number;
	unsigned int max_size_of_buf;
	unsigned int count;
	unsigned char virtual_file_number;
	piece_offsets_list(file_offset *pcs_offsets, unsigned int f_number, unsigned int max_buf_size, unsigned int cnt, unsigned char v_number) {
		file_number = f_number;
		piece_offsets = pcs_offsets;
		max_size_of_buf = max_buf_size;
		count = cnt;
		virtual_file_number = v_number;
		next = NULL;
	}
	~piece_offsets_list() {
		if (piece_offsets) free(piece_offsets);
		if (next) delete next;
	}
	void add(file_offset *pcs_offsets, unsigned int f_number, unsigned int max_buf_size, unsigned int cnt, unsigned char v_number) {
		piece_offsets_list *new_elem = new piece_offsets_list(pcs_offsets, f_number, max_buf_size, cnt, v_number);
		piece_offsets_list *tmp = this;
		while (tmp->next != NULL) {
			tmp = tmp->next;
		}
		tmp->next = new_elem;
	}
};

template <typename bufferizer_class> class bufferizer_list {
public:
	int requests;
	unsigned int file_number;
	file_offset *virtual_files_shift;
	unsigned char virtual_files_count;
	bufferizer_class *bufferizer;
	bufferizer_list *root;
	bufferizer_list *next;
	bufferizer_list(bufferizer_class *cache_buf, unsigned int file_number_init, file_offset *virtual_shifts, unsigned char virtual_count) {
		requests = 1;
		file_number = file_number_init;
		bufferizer = cache_buf;
		virtual_files_shift = virtual_shifts;
		virtual_files_count = virtual_count;
		root = this;
		next = NULL;
	}
	void add(bufferizer_class *cache_buf, unsigned int file_number_init, file_offset *virtual_shifts, unsigned char virtual_count) {
		bufferizer_list *new_elem = new bufferizer_list(cache_buf, file_number_init, virtual_shifts, virtual_count);
		new_elem->root = this->root;
		bufferizer_list *elem = this->root;
		while (elem->next != NULL) {
			elem = elem->next;
		}
		elem->next = new_elem;
	}
	void delete_element(bufferizer_list *buf_list) {
		bufferizer_list *elem = root;
		if (buf_list == root) {
			bufferizer_list *new_root = root->next;
			while (elem != NULL) {
				elem->root = new_root;
				elem = elem->next;
			}
		}
		else {
			while (elem->next != buf_list)
				elem = elem->next;
			elem->next = buf_list->next;
		}
		buf_list->root = buf_list;
		buf_list->next = NULL;
		delete buf_list;
	}
	~bufferizer_list() {
		delete bufferizer;
		if (virtual_files_shift)
			free(virtual_files_shift);
		if (next)
			delete next;
	}
};

extern lru_cache global_cache;
// key is (table_type << 15 | table_index)
extern unordered_map<unsigned long, bufferizer_list<read_file_bufferizer> *> read_file_bufferizers;
// key is (table_index)
extern unordered_map<unsigned long, bufferizer_list<cache_file_bufferizer> *> cache_file_bufferizers[TYPES_COUNT];
// key is (table_index >> 1)
extern unordered_map<unsigned long, custom_tb_indexer *> tb_indexers;
// key is (table_type << 15 | table_index)
extern unordered_map<unsigned long, piece_offsets_list *> piece_offsets_map;

extern unsigned long long auto_clean_count;
extern bool memory_allocation_done;
extern unsigned long long total_cache_size;
extern unsigned long long max_hidden_size;
extern unsigned long long cur_hidden_size;
#ifdef LOG_HIDDEN
void log_cur_hidden_size(char *message, int dif);
#endif
extern int clean_percent;
extern int max_requests;
extern int max_requests_rd_buf;

unsigned long long get_indexers_size();
unsigned long long get_bufferizers_size(int table_type);
unsigned long long get_bufferizers_size_all();
unsigned long long get_rd_bufferizers_size();
unsigned long long get_piece_offsets_size();
void clean_hidden_cache();
void allocate_cache_memory();

#endif /* EGCACHECONTROL_H_ */
