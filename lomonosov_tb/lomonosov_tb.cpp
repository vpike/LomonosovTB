#include <stdlib.h>
#include <stdio.h>
#include "lomonosov_tb.h"
#include "../egmaintypes.h"
#include "../egpglobals.h"
#include "../egintflocal.h"
#include "../eglongprobes.h"
#include "../egcachecontrol.h"
#include "../egtbfile.h"
#include "../egtbscanner.h"
#ifdef LOMONOSOV_FULL
#include "../egindex.h"
#endif

#ifdef PARALLEL_TB
#include <pthread.h>
#endif

void init_lomonosov_tb() {
	caching_file_bufferizers = true;
	init_simple_indexer();
	init_bitboards();
	init_types_vector();
	create_tb_index_tree();
#ifdef LOMONOSOV_FULL
	init_indexes();
#endif
	reset_not_exist_tables(true);
#ifdef PARALLEL_TB
	pthread_rwlock_init(&paths_lock, NULL);
	paths_locker = &paths_lock;
	pthread_rwlock_init(&not_exist_tables_lock, NULL);
	not_exist_tables_locker = &not_exist_tables_lock;
	pthread_rwlock_init(&read_file_buf_lock, NULL);
	read_file_buf_locker = &read_file_buf_lock;
	pthread_rwlock_init(&cache_file_buf_lock, NULL);
	cache_file_buf_locker = &cache_file_buf_lock;
	pthread_rwlock_init(&tb_indexers_lock, NULL);
	tb_indexers_locker = &tb_indexers_lock;
	pthread_rwlock_init(&piece_offsets_lock, NULL);
	piece_offsets_locker = &piece_offsets_lock;
	pthread_rwlock_init(&hidden_lock, NULL);
	hidden_locker = &hidden_lock;
	pthread_rwlock_init(&probe_lock, NULL);
	probe_locker = &probe_lock;
	pthread_rwlock_init(&requests_lock, NULL);
	requests_locker = &requests_lock;
	pthread_rwlock_init(&log_lock, NULL);
	log_locker = &log_lock;
	pthread_mutex_init(&auto_clean_count_mutex, NULL);
	auto_clean_count_mutexer = &auto_clean_count_mutex;
	pthread_mutex_init(&cache_bufferizer_mutex, NULL);
	cache_bufferizer_mutexer = &cache_bufferizer_mutex;
	pthread_mutex_init(&read_bufferizer_mutex, NULL);
	read_bufferizer_mutexer = &read_bufferizer_mutex;
	pthread_mutex_init(&block_counter_mutex, NULL);
	block_counter_mutexer = &block_counter_mutex;
#endif
	// Rehashing seems to be not effective
	/*if (caching_file_bufferizers) {
		tb_indexers.rehash(TABLE_MAX_INDEX);
		read_file_bufferizers.rehash(TABLE_MAX_INDEX);
		for (int i = MIN_TYPE; i <= MAX_TYPE; i++)
			cache_file_bufferizers[i].rehash(TABLE_MAX_INDEX);
		piece_offsets_map.rehash(TABLE_MAX_INDEX);
	}*/
	set_cache_size(2048); // default
}

void add_table_path(const char *path) {
	const char *tmp_path = path;
	char *line = new char[MAX_PATH];
	int size;
	rwlock_wrlock(paths_locker);
	while(tmp_path != NULL && strcmp(tmp_path, "") != 0) {
		size = strcspn(tmp_path, ";");
		line = strncpy(line, tmp_path, size);
		line[size] = '\0';
		if (line[size - 1] != PATH_SEPAR) {
			line[size] = PATH_SEPAR;
			line[size+1] = '\0';
		}
		char *tmp_line = (char *)malloc((strlen(line)+1)*sizeof(char));
		strcpy(tmp_line, line);
		table_paths.push_back(tmp_line);
		tmp_path = strchr(tmp_path, ';');
		if (tmp_path != NULL)
			tmp_path = &tmp_path[1];
	}
	rwlock_unlock(paths_locker);
	delete(line);
	rwlock_wrlock(not_exist_tables_locker);
	reset_not_exist_tables(true);
	rwlock_unlock(not_exist_tables_locker);
	known_not_exist = false;
}

void set_table_path(const char *path) {
	table_paths.clear();
	add_table_path(path);
}

void set_cache_size(int MB) {
	total_cache_size = MB;
	total_cache_size <<= 20;
	memory_allocation_done = false;
	allocate_cache_memory();
	// Rehashing seems to be not effective (because of hidden cache takes more memory than cache of block)
	//global_cache.rehash();
}

void explicit_clear_cache(char table_type) {
	rwlock_wrlock(probe_locker);
	unordered_map<unsigned long, bufferizer_list<cache_file_bufferizer> *>::iterator it;
	unordered_map<unsigned long, bufferizer_list<read_file_bufferizer> *>::iterator it_rd;
	unordered_map<unsigned long, piece_offsets_list *>::iterator it_offsets;
	unordered_map<unsigned long, custom_tb_indexer *>::iterator it_indexer;
	for (it = cache_file_bufferizers[table_type].begin(); it != cache_file_bufferizers[table_type].end(); it++) {
		//delete itself
		bufferizer_list<cache_file_bufferizer> *cache_buf_list = it->second;
		while (cache_buf_list != NULL) {
			cur_hidden_size -= cache_buf_list->bufferizer->get_size();
			cache_buf_list = cache_buf_list->next;
		}
		delete it->second;
		//delete indexer
		it_indexer = tb_indexers.find(MUTABLE_TYPE(table_type) ? 
			(((unsigned long)table_type << 16) | it->first) : (it->first >> 1));
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
		it_rd = read_file_bufferizers.find(key);
		if (it_rd != read_file_bufferizers.end()) {
			bufferizer_list<read_file_bufferizer> *list = it_rd->second;
			while (list != NULL) {
				cur_hidden_size -= list->bufferizer->get_size();
				list = list->next;
			}
			delete it_rd->second;
			read_file_bufferizers.erase(it_rd);
		}
		//delete form map
		cache_file_bufferizers[table_type].erase(it++);
		break;
	}
	cache_file_bufferizers[table_type].clear();
	global_cache.clean(table_type);
	set_cache_size(total_cache_size >> 20);
	rwlock_unlock(probe_locker);
}

void clear_cache(char table_type) {
	if (UNKNOWN_TB_TYPE(table_type))
		return;
	for (vector<int>::iterator it_types = types_vector[table_type].begin(); it_types != types_vector[table_type].end(); it_types++)
		explicit_clear_cache(*it_types);
}

void clear_cache_all() {
	rwlock_wrlock(probe_locker);
	unordered_map<unsigned long, bufferizer_list<cache_file_bufferizer> *>::iterator it;
	for (int table_type = MIN_TYPE; table_type <= MAX_TYPE; table_type++) {
		for (it = cache_file_bufferizers[table_type].begin(); it != cache_file_bufferizers[table_type].end(); it++) {
			delete it->second;
		}
		cache_file_bufferizers[table_type].clear();
	}
	unordered_map<unsigned long, bufferizer_list<read_file_bufferizer> *>::iterator it_rd;
	for (it_rd = read_file_bufferizers.begin(); it_rd != read_file_bufferizers.end(); it_rd++) {
		delete it_rd->second;
	}
	read_file_bufferizers.clear();
	unordered_map<unsigned long, piece_offsets_list *>::iterator it_offsets;
	for (it_offsets = piece_offsets_map.begin(); it_offsets != piece_offsets_map.end(); it_offsets++) {
		delete it_offsets->second;
	}
	piece_offsets_map.clear();
	unordered_map<unsigned long, custom_tb_indexer *>::iterator it_indexer;
	for (it_indexer = tb_indexers.begin(); it_indexer != tb_indexers.end(); it_indexer++) {
		delete it_indexer->second;
	}
	tb_indexers.clear();
	cur_hidden_size = 0;
	global_cache.clean_all();
	set_cache_size(total_cache_size >> 20);
	rwlock_unlock(probe_locker);
}

int get_max_pieces_count(char table_type) {
	if (UNKNOWN_TB_TYPE(table_type))
		return 0;
	if (!known_not_exist)
		scan_tables();
	return get_max_pieces_count_extended(table_type);
}

bool get_table_name(const char *fen, char *tbname) {
	short_pieces_env local_env;
	unsigned char pcs_local[MAX_MEN];
	local_env.pieces = pcs_local;
	bool ztb_invert_color;
	if (load_pieces_from_fen_local(fen, &ztb_invert_color, &local_env)) {
		get_tb_name_without_slice_local(tbname, &local_env);
		return true;
	}
	return false;
}

void get_missing_table_name(char *tbname) {
	strcpy(tbname, probe_missing_table_name);
}

int probe_fen(const char *fen, int *eval, char table_type) {
	*eval = 0;
	if (UNKNOWN_TB_TYPE(table_type))
		return PROBE_UNKNOWN_TB_TYPE;
	int result;
	result = PROBE_NO_TABLE;
	short_pieces_env local_env;
	unsigned char pcs_local[MAX_MEN];
	local_env.men_count = 0;
	local_env.pieces = pcs_local;
	local_env.indexer = NULL;
	color_position local_pos;
	unsigned long local_index;
	result = get_value_from_fen_local(fen, eval, table_type, &local_env, &local_pos, &local_index);
	change_internal_value(eval, table_type);
	return result;
}

int probe_position(int side, unsigned int *psqW, unsigned int *psqB, int *piCount, int sqEnP, int *eval, char table_type, unsigned char castlings) {
	*eval = 0;
	if (UNKNOWN_TB_TYPE(table_type))
		return PROBE_UNKNOWN_TB_TYPE;
	int result;
	result = PROBE_NO_TABLE;
	short_pieces_env local_env;
	unsigned char pcs_local[MAX_MEN];
	local_env.men_count = 0;
	local_env.pieces = pcs_local;
	local_env.indexer = NULL;
	color_position local_pos;
	unsigned long local_index;
	result = get_value_from_position_local(side, psqW, psqB, piCount, sqEnP, eval, table_type, &local_env, &local_pos, &local_index);
	change_internal_value(eval, table_type);
	return result;
}

int get_tree_fen(const char *fen, std::string *moves, char table_type) {
	return get_tree_bounded_fen(fen, moves, table_type, 1024, 1024, 1024);
}

int get_tree_bounded_fen(const char *fen, std::string *moves, char table_type, int best_bound, int mid_bound, int worse_bound) {
	if (UNKNOWN_TB_TYPE(table_type))
		return PROBE_UNKNOWN_TB_TYPE;
	int result;
	result = PROBE_NO_TABLE;
	short_pieces_env local_env;
	unsigned char pcs_local[MAX_MEN];
	local_env.men_count = 0;
	local_env.pieces = pcs_local;
	local_env.indexer = NULL;
	color_position local_pos;
	unsigned long local_index;
	bool need_invert;
	result = parse_fen(fen, &local_env, &local_pos, &need_invert);
	if (result != PROBE_OK)
		return result;
	vector<move_valued> moves_valued;
	result = get_tree_from_load_position_local(&moves_valued, table_type, &local_env, &local_pos, need_invert);
	moves->clear();
	int bounds[3] = {best_bound, mid_bound, worse_bound};
	int b = 0, cnt = 0;
	for (int i = 0; i < moves_valued.size(); i++) {
		if (i > 0 && sign(moves_valued[i-1].value) != sign(moves_valued[i].value)) {
			b += sign(moves_valued[i].value) - sign(moves_valued[i-1].value);
			cnt = 0;
		}
		if (cnt < bounds[b]) {
			if (moves->size() > 0)
				moves->append(";");
			moves->append(move_valued_to_string(moves_valued[i]));
			cnt++;
		}
	}
	return result;
}

int get_tree_position(int side, unsigned int *psqW, unsigned int *psqB, int *piCount, int sqEnP, std::string *moves, char table_type, unsigned char castlings) {
	if (UNKNOWN_TB_TYPE(table_type))
		return PROBE_UNKNOWN_TB_TYPE;
	int result;
	result = PROBE_NO_TABLE;
	short_pieces_env local_env;
	unsigned char pcs_local[MAX_MEN];
	local_env.men_count = 0;
	local_env.pieces = pcs_local;
	local_env.indexer = NULL;
	color_position local_pos;
	unsigned long local_index;
	bool need_invert;
	result = parse_position(side, psqW, psqB, piCount, sqEnP, &local_env, &local_pos, &need_invert);
	if (result != PROBE_OK)
		return result;
	vector<move_valued> moves_valued;
	result = get_tree_from_load_position_local(&moves_valued, table_type, &local_env, &local_pos, need_invert);
	moves->clear();
	for (int i = 0; i < moves_valued.size(); i++) {
		if (moves->size() > 0)
			moves->append(";");
		moves->append(move_valued_to_string(moves_valued[i]));
	}
	return result;
}

int get_best_move_fen(const char *fen, std::string *move, char table_type) {
	if (UNKNOWN_TB_TYPE(table_type))
		return PROBE_UNKNOWN_TB_TYPE;
	int result;
	result = PROBE_NO_TABLE;
	short_pieces_env local_env;
	unsigned char pcs_local[MAX_MEN];
	local_env.men_count = 0;
	local_env.pieces = pcs_local;
	local_env.indexer = NULL;
	color_position local_pos;
	bool need_invert;
	result = parse_fen(fen, &local_env, &local_pos, &need_invert);
	if (result != PROBE_OK)
		return result;
	int eval;
	unsigned long local_index = get_table_index_local(local_pos.cur_wtm, &local_env);
	result = get_value_from_load_position_local(&eval, table_type, &local_env, &local_pos, local_index);
	if (result != PROBE_OK)
		return result;
	move_valued res_move_valued;
	result = get_best_move_from_load_position_local(&res_move_valued, eval, table_type, &local_env, &local_pos, need_invert);
	if (result == PROBE_OK) {
		move->clear();
		move->append(move_valued_to_string(res_move_valued));
	}
	return result;
}

int get_line_fen(const char *fen, std::string *moves, char table_type) {
	return get_line_bounded_fen(fen, moves, table_type, 8192);
}

int get_line_bounded_fen(const char *fen, std::string *moves, char table_type, int moves_bound) {
	if (UNKNOWN_TB_TYPE(table_type))
		return PROBE_UNKNOWN_TB_TYPE;
	int result;
	result = PROBE_NO_TABLE;
	short_pieces_env local_env;
	unsigned char pcs_local[MAX_MEN];
	local_env.men_count = 0;
	local_env.pieces = pcs_local;
	local_env.indexer = NULL;
	color_position local_pos;
	bool need_invert;
	result = parse_fen(fen, &local_env, &local_pos, &need_invert);
	int with_number = (local_pos.cur_wtm ? 0 : 1);
	if (need_invert)
		with_number ^= 1;
	if (result != PROBE_OK)
		return result;
	int eval;
	unsigned long local_index = get_table_index_local(local_pos.cur_wtm, &local_env);
	result = get_value_from_load_position_local(&eval, table_type, &local_env, &local_pos, local_index);
	if (result != PROBE_OK)
		return result;
	vector<move_valued> moves_valued;
	result = get_line_from_load_position_local(&moves_valued, eval, table_type, &local_env, &local_pos, need_invert, moves_bound);
	moves->clear();
	for (int i = 0; i < moves_valued.size(); i++) {
		if (moves->size() > 0)
			moves->append(" ");
		if (i == 0 || (i & 1) == with_number) {
			char str_num[10];
			sprintf(str_num, "%d", (i + 3) / 2);
			moves->append(str_num);
			moves->append(((i & 1) == with_number) ? "." : "...");
		}
		moves->append(move_valued_to_string(moves_valued[i]));
	}
	return result;
}

int get_line_position(int side, unsigned int *psqW, unsigned int *psqB, int *piCount, int sqEnP, std::string *moves, char table_type, unsigned char castlings) {
	if (UNKNOWN_TB_TYPE(table_type))
		return PROBE_UNKNOWN_TB_TYPE;
	int result;
	result = PROBE_NO_TABLE;
	short_pieces_env local_env;
	unsigned char pcs_local[MAX_MEN];
	local_env.men_count = 0;
	local_env.pieces = pcs_local;
	local_env.indexer = NULL;
	color_position local_pos;
	bool need_invert;
	result = parse_position(side, psqW, psqB, piCount, sqEnP, &local_env, &local_pos, &need_invert);
	int with_number = (local_pos.cur_wtm ? 0 : 1);
	if (need_invert)
		with_number ^= 1;
	if (result != PROBE_OK)
		return result;
	int eval;
	unsigned long local_index = get_table_index_local(local_pos.cur_wtm, &local_env);
	result = get_value_from_load_position_local(&eval, table_type, &local_env, &local_pos, local_index);
	if (result != PROBE_OK)
		return result;
	vector<move_valued> moves_valued;
	result = get_line_from_load_position_local(&moves_valued, eval, table_type, &local_env, &local_pos, need_invert, 8192);
	moves->clear();
	for (int i = 0; i < moves_valued.size(); i++) {
		if (moves->size() > 0)
			moves->append(" ");
		if (i == 0 || (i & 1) == with_number) {
			char str_num[10];
			sprintf(str_num, "%d", (i + 3) / 2);
			moves->append(str_num);
			moves->append((i == 0) ? "..." : ".");
		}
		moves->append(move_valued_to_string(moves_valued[i]));
	}
	return result;
}
