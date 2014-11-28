#include "egcachebuf.h"
#include "egpglobals.h"
#include "egcachecontrol.h"
#ifdef LOMONOSOV_FULL
#include "egtypes.h"
#include "egglobals.h"
#endif

char established_cache_mode = DEFAULT_CACHE_MODE;

cache_file_bufferizer::cache_file_bufferizer(int index, int table_type) {
	current_buffer_from_cache = false;
	non_cache_buffer = NULL;
	cache_index = index;
	cache_table_type = table_type;
	not_caching = !caching_file_bufferizers;
	cache_mode = DEFAULT_CACHE_MODE;
}

cache_file_bufferizer::~cache_file_bufferizer()  {
	if (current_buffer_from_cache && cache_mode == UNCOMPRESSED_IN_CACHE) {
		char *block_counter = buffer - 1;
		mutex_lock(block_counter_mutexer);
		block_counter[0]--;
		if (block_counter[0] == 0)
			free(block_counter);
		mutex_unlock(block_counter_mutexer);
		buffer = non_cache_buffer;
		current_buffer_from_cache = false;
	}
}

size_t cache_file_bufferizer::read_compressed_buffer(char **compressed_buffer, size_t *comp_size, unsigned long long *piece_number) {
	if (cache_mode != COMPRESSED_IN_CACHE) {
		return compressed_file_bufferizer::read_compressed_buffer(compressed_buffer, comp_size, piece_number);
	}
	*piece_number = get_piece_number();
	unsigned long long real_piece_number = (*piece_number) + (unsigned long long)pieces_per_file * (unsigned long long)current_file_number
		+ (unsigned long long)current_virtual_file_number * VIRTUAL_FILE_BLOCKS_COUNT;
	if (!global_cache.find(cache_index, real_piece_number, cache_table_type, compressed_buffer, (unsigned long *)comp_size)) {
		size_t org_size = compressed_file_bufferizer::read_compressed_buffer(compressed_buffer, comp_size, piece_number);
		global_cache.insert(cache_index, real_piece_number, cache_table_type, *compressed_buffer, *comp_size, cache_index);
		current_buffer_from_cache = false;
		return org_size;
	}
	char *block_counter = *compressed_buffer - 1;
	mutex_lock(block_counter_mutexer);
	if (block_counter[0] == 127) {
		printf("cache_file_bufferizer::read_buffer: counter of block > MAX_CHAR!");
		ABORT(0);
	}
	block_counter[0]++;
	mutex_unlock(block_counter_mutexer);
	current_buffer_from_cache = true;
	size_t org_size;
	if (arch_type & TB_FIX_COMP_SIZE) {
		org_size = piece_offsets[(*piece_number) + 1] - piece_offsets[*piece_number];
	} else {
		file_offset piece_start_offset = (*piece_number) * uncompr_piece_size;
		org_size = uncompr_piece_size;
		// if it's the last piece, it may have different size
		if (piece_start_offset + uncompr_piece_size >= total_file_length)
			org_size = total_file_length - piece_start_offset;
	}
	return org_size;
}

void cache_file_bufferizer::free_compressed_buffer(char *compressed_buffer) {
	if (current_buffer_from_cache && cache_mode == COMPRESSED_IN_CACHE) {
		char *block_counter = compressed_buffer - 1;
		mutex_lock(block_counter_mutexer);
		block_counter[0]--;
		if (block_counter[0] == 0)
			free(block_counter);
		mutex_unlock(block_counter_mutexer);
		current_buffer_from_cache = false;
	} else
		free(compressed_buffer);
}

void cache_file_bufferizer::read_buffer() {
	if (uncompressed_mode || cache_mode != UNCOMPRESSED_IN_CACHE) {
		compressed_file_bufferizer::read_buffer(/*&cache_index*/);
		return;
	}
	
	if (cur_file_pos_read >= end_file_pos) return;
	
	unsigned long long piece_number = get_piece_number();
	unsigned long long real_piece_number = piece_number + (unsigned long long)pieces_per_file * (unsigned long long)current_file_number
		+ (unsigned long long)current_virtual_file_number * VIRTUAL_FILE_BLOCKS_COUNT;

	if (current_buffer_from_cache) {
		char *block_counter = buffer - 1;
		mutex_lock(block_counter_mutexer);
		block_counter[0]--;
		if (block_counter[0] == 0)
			free(block_counter);
		mutex_unlock(block_counter_mutexer);
	}
	if (!global_cache.find(cache_index, real_piece_number, cache_table_type, &buffer, &bytes_in_buffer)) {
		buffer = non_cache_buffer;
		current_buffer_from_cache = false;
		compressed_file_bufferizer::read_buffer(/*&cache_index*/);
		if ((arch_type & TB_BINARY) && bit_shift_of_block) {
			unsigned long bit_size = bytes_in_buffer * bits_for_record;
			unsigned long byte_size;
			if (bits_for_record <= 8) {
				byte_size = (bit_size + 7) >> 3;
			}
			else {
				byte_size = (bit_size + 15) >> 4;
			}
			global_cache.insert(cache_index, real_piece_number, cache_table_type, buffer, byte_size, cache_index);
		} else
			global_cache.insert(cache_index, real_piece_number, cache_table_type, buffer, bytes_in_buffer, cache_index);
		return;
	}
	char *block_counter = buffer - 1;
	mutex_lock(block_counter_mutexer);
	if (block_counter[0] == 127) {
		printf("cache_file_bufferizer::read_buffer: counter of block > MAX_CHAR!");
		ABORT(0);
	}
	block_counter[0]++;
	mutex_unlock(block_counter_mutexer);
	current_buffer_from_cache = true;
	file_offset piece_start_offset = (arch_type & TB_FIX_COMP_SIZE) ? piece_offsets[piece_number] : (piece_number * uncompr_piece_size);
	buf_pos = cur_file_pos_read - piece_start_offset;
	if ((arch_type & TB_TERNARY) && (arch_type & TB_COMP_METHOD_MASK) == TB_RE_PAIR && bit_shift_of_block) {
		buf_pos = buf_pos / 5;
	}
	if (arch_type & TB_BINARY) {
		if (bits_for_record <= 8) {
			index_in_buffer = buf_pos;
			buf_pos = (buf_pos * bits_for_record) >> 3;
		} else {
			index_in_buffer = buf_pos >> 1;
			buf_pos = (buf_pos * bits_for_record) >> 4;
			if (buf_pos & 1) --buf_pos;
		}
	}
	cur_file_pos_read = piece_start_offset + bytes_in_buffer;
}

bool cache_file_bufferizer::new_src_file(unsigned int file_number) {
	if (not_caching) {
		compressed_file_bufferizer::new_src_file(file_number);
		if (src_file) {
			rwlock_wrlock(hidden_locker);
			cur_hidden_size += src_file->get_size();
#ifdef LOG_HIDDEN
			log_cur_hidden_size("new_src", src_file->get_size());
#endif
			rwlock_unlock(hidden_locker);
			return true;
		}
		return false;
	}
	unsigned long key = (((unsigned long)cache_table_type) << 16) | cache_index;
	unordered_map<unsigned long, bufferizer_list<read_file_bufferizer> *>::iterator it;
	rwlock_rdlock(read_file_buf_locker);
	if (caching_file_bufferizers && (it = read_file_bufferizers.find(key)) != read_file_bufferizers.end() && it->second != NULL) {
		bufferizer_list<read_file_bufferizer> *buf_list = it->second;
		mutex_lock(read_bufferizer_mutexer);
		while (buf_list != NULL) {
			if (buf_list->file_number == file_number && buf_list->bufferizer->trylock()) {
				// for getting new src_file in begin_read
				if (!opened_ && strcmp(buf_list->bufferizer->read_file_name, org_file_name) != 0) {
					buf_list->bufferizer->unlock();
					buf_list = buf_list->next;
					continue;
				}
				mutex_unlock(read_bufferizer_mutexer);
				buf_list->requests++;
				rwlock_rdlock(requests_locker);
				if (buf_list->requests > max_requests_rd_buf) {
					rwlock_unlock(requests_locker);
					rwlock_wrlock(requests_locker);
					max_requests_rd_buf = buf_list->requests;
				}
				rwlock_unlock(requests_locker);
				src_file = buf_list->bufferizer;
				virtual_files_count = buf_list->virtual_files_count;
				virtual_files_shift = buf_list->virtual_files_shift;
				break;
			}
			buf_list = buf_list->next;
		}
		if (src_file == NULL)
			mutex_unlock(read_bufferizer_mutexer);
	}
	rwlock_unlock(read_file_buf_locker);
	if (src_file == NULL) {
		compressed_file_bufferizer::new_src_file(file_number);
 		if (src_file) {
			src_file->trylock();
			if (caching_file_bufferizers) {
				rwlock_wrlock(read_file_buf_locker);
				if ((it = read_file_bufferizers.find(key)) != read_file_bufferizers.end()) {
					it->second->add(src_file, file_number, virtual_files_shift, virtual_files_count);
				}
				else {
					bufferizer_list<read_file_bufferizer> *buf_list = new bufferizer_list<read_file_bufferizer>(src_file, file_number, virtual_files_shift, virtual_files_count);
					read_file_bufferizers.insert(make_pair(key, buf_list));
				}
				rwlock_unlock(read_file_buf_locker);
				src_file->not_caching = false;
				rwlock_wrlock(hidden_locker);
				cur_hidden_size += src_file->get_size();
#ifdef LOG_HIDDEN
				log_cur_hidden_size("new_src_cache", src_file->get_size());
#endif
				rwlock_unlock(hidden_locker);
			}
		}
	}
	return (src_file != NULL);
}

void cache_file_bufferizer::add_virtual_info_to_cache() {
	if (not_caching)
		return;
	unsigned long key = (((unsigned long)cache_table_type) << 16) | cache_index;
	unordered_map<unsigned long, bufferizer_list<read_file_bufferizer> *>::iterator it;
	rwlock_rdlock(read_file_buf_locker);
	it = read_file_bufferizers.find(key);
	if (it == read_file_bufferizers.end()) {
		printf("cache_file_bufferizer::add_virtual_info_to_cache: current src_file is not found in cache!\n");
		ABORT(1);
	}
	bufferizer_list<read_file_bufferizer> *buf_list = it->second;
	// we sure that src_file is locked
	while (buf_list != NULL && buf_list->bufferizer != src_file)
		buf_list = buf_list->next;
	if (buf_list == NULL) {
		printf("cache_file_bufferizer::add_virtual_info_to_cache: current src_file is not found in bufferizers list from cache!\n");
		ABORT(1);
	}
	buf_list->virtual_files_count = virtual_files_count;
	buf_list->virtual_files_shift = virtual_files_shift;
	rwlock_unlock(read_file_buf_locker);
}

unsigned int cache_file_bufferizer::load_piece_table() {
	if (not_caching) {
		return compressed_file_bufferizer::load_piece_table();
	}
	unordered_map<unsigned long, piece_offsets_list *>::iterator it;
	piece_offsets = NULL;
	unsigned long key = (((unsigned long)cache_table_type) << 16) | cache_index;
	rwlock_rdlock(piece_offsets_locker);
	if (caching_file_bufferizers && (it = piece_offsets_map.find(key)) != piece_offsets_map.end()) {
		piece_offsets_list *offsets_list = it->second;
		while (offsets_list != NULL) {
			if (offsets_list->file_number == current_file_number && offsets_list->virtual_file_number == current_virtual_file_number) {
				piece_offsets = offsets_list->piece_offsets;
				if (!probe_one_exact && offsets_list->max_size_of_buf > max_size_of_buf) {
					max_size_of_buf = offsets_list->max_size_of_buf;
					set_buf_size(max_size_of_buf);
				}
				piece_table_loaded = true;
				rwlock_unlock(piece_offsets_locker);
				return offsets_list->count;
			}
			offsets_list = offsets_list->next;
		}
	}
	rwlock_unlock(piece_offsets_locker);
	if (piece_offsets == NULL) {
		unsigned int pcs_count = compressed_file_bufferizer::load_piece_table();
		if (caching_file_bufferizers) {
 			rwlock_wrlock(piece_offsets_locker);
			if ((it = piece_offsets_map.find(key)) == piece_offsets_map.end()) {
				piece_offsets_list *offsets_list = new piece_offsets_list(piece_offsets, current_file_number, max_size_of_buf, pcs_count, current_virtual_file_number);
				piece_offsets_map.insert(make_pair(key, offsets_list));
				rwlock_wrlock(hidden_locker);
				cur_hidden_size += pcs_count * sizeof(file_offset);
#ifdef LOG_HIDDEN
				log_cur_hidden_size("offset", pcs_count * sizeof(file_offset));
#endif
				rwlock_unlock(hidden_locker);
			}
			else {
				piece_offsets_list *offsets_list = it->second;
				while (offsets_list != NULL) {
					if (offsets_list->file_number == current_file_number && offsets_list->virtual_file_number == current_virtual_file_number)
						break;
					offsets_list = offsets_list->next;
				}
				if (offsets_list != NULL) {
					free(piece_offsets);
					piece_offsets = offsets_list->piece_offsets;
				}
				else {
					it->second->add(piece_offsets, current_file_number, max_size_of_buf, pcs_count, current_virtual_file_number);
					rwlock_wrlock(hidden_locker);
					cur_hidden_size += pcs_count * sizeof(file_offset);
#ifdef LOG_HIDDEN
					log_cur_hidden_size("offset", pcs_count * sizeof(file_offset));
#endif
					rwlock_unlock(hidden_locker);
				}
			}
			rwlock_unlock(piece_offsets_locker);
		}
		return pcs_count;
	}
	return 0;
}
