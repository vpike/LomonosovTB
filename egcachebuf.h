#ifndef EGCACHEBUF_H_
#define EGCACHEBUF_H_

#include "egdecompress.h"
#include "egcache.h"

#define EMPTY_CACHE				0
#define COMPRESSED_IN_CACHE		1
#define UNCOMPRESSED_IN_CACHE	2
#define DEFAULT_CACHE_MODE		3
extern char established_cache_mode;

class cache_file_bufferizer: public compressed_file_bufferizer {
private:
	char cache_mode;
	bool current_buffer_from_cache;
	char *non_cache_buffer;
	unsigned long cache_index;
	int cache_table_type;
protected:
	size_t read_compressed_buffer(char **compressed_buffer, size_t *comp_size, unsigned long long *piece_number);
	void free_compressed_buffer(char *compressed_buffer);
	void read_buffer();
	bool new_src_file(unsigned int file_number);
	unsigned int load_piece_table();
	void set_buf_size(unsigned long value) {
		read_file_bufferizer::set_buf_size(value);
		non_cache_buffer = buffer;
	}
	void add_virtual_info_to_cache();
public:
	cache_file_bufferizer(int index, int table_type);
	~cache_file_bufferizer();

	bool begin_read(const char *filename, file_offset start_pos, file_offset length) {
		bool res = compressed_file_bufferizer::begin_read(filename, start_pos, length/*, &cache_index*/);
		if (established_cache_mode == DEFAULT_CACHE_MODE) {
			if (arch_type & TB_FIX_COMP_SIZE) {
				set_cache_mode(COMPRESSED_IN_CACHE);
			} else
				set_cache_mode(UNCOMPRESSED_IN_CACHE);
		} else
			set_cache_mode(established_cache_mode);
		non_cache_buffer = buffer;
		return res;
	}

	int get_size() {
		return sizeof(cache_file_bufferizer) - sizeof(compressed_file_bufferizer) + compressed_file_bufferizer::get_size();
	}
	int get_size_with_src() {
		return sizeof(cache_file_bufferizer) - sizeof(compressed_file_bufferizer) + compressed_file_bufferizer::get_size_with_src();
	}
	int get_cache_mode() { return cache_mode; }
	void set_cache_mode(int mode) {
		cache_mode = mode;
		probe_one_exact = false;
		if (cache_mode == UNCOMPRESSED_IN_CACHE) {
			if ((arch_type & TB_BINARY) || ((arch_type & TB_TERNARY) && !(arch_type & TB_FIX_COMP_SIZE)))
				set_bit_shift_of_block(true);
			else
				set_bit_shift_of_block(false);
		} else {
			set_bit_shift_of_block(false);
			if ((arch_type & TB_COMP_METHOD_MASK) == TB_RE_PAIR) {
				probe_one_exact = true;
			}
		}
	}
};

#endif

