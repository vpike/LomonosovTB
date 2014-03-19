#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <sys/stat.h>
#include "lomonosov_tb.h"
#include "../egmaintypes.h"
#include "../egpglobals.h"
#include "../egintflocal.h"
#include "../egcachecontrol.h"
#include "../egtbfile.h"
#ifdef LOMONOSOV_FULL
#include "../egindex.h"
#endif

#ifdef PARALLEL_TB
#include <pthread.h>
#endif

#ifdef LOMONOSOV_FULL
#define UNKNOWN_TB_TYPE(table_type) (table_type < MIN_TYPE || table_type > MAX_TYPE)
int table_order_count = 8;
int table_order[8] = {WL, TL, PL, ML, ZWL, ZTL, ZPL, ZML};
char *orders[] = {"DTM", "WL", "WDL", "PL", "nothing", "DTZ50", "WL50", "WDL50", "PL50", "nothing"};
#else
#define UNKNOWN_TB_TYPE(table_type) (!MUTABLE_TYPE(table_type))
int table_order_count = 4;
int table_order[4] = {WL, PL, ZWL, ZPL};
char *orders[] = {"nothing", "WL", "nothing", "PL", "nothing", "nothing", "WL50", "nothing", "PL50", "nothing"};
#endif

void reset_not_exist_tables(bool all_exist) {
	unsigned char byte = all_exist ? 0 : 0xff;
	for (int i = MIN_TYPE; i <= MAX_TYPE; i++) {
		memset(not_exist_tables[i], byte, NOT_EXIST_TABLES_SIZE);
		min_block_size[i] = 0;
	}
}

bool APIENTRY __stdcall DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpReserved) {
	if (fdwReason == DLL_PROCESS_ATTACH) {
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
    return TRUE;
}

void __stdcall add_table_path(const char *path) {
	const char *tmp_path = path;
	char *line = new char[MAX_PATH];
	int size;
	rwlock_wrlock(paths_locker);
	while(tmp_path != NULL && strcmp(tmp_path, "") != 0) {
		size = strcspn(tmp_path, ";");
		line = strncpy(line, tmp_path, size);
		line[size] = '\0';
		if (line[size - 1] != '\\')
			line = strcat(line, "\\");
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

void __stdcall set_table_path(const char *path) {
	table_paths.clear();
	add_table_path(path);
}

#ifndef TB_DLL_EXPORT
unsigned long long __stdcall get_number_load_from_cache() {
	return global_cache.get_number_load_from_cache();
}

unsigned long long __stdcall get_number_load_from_file() {
	return global_cache.get_number_load_from_file();
}

unsigned long long __stdcall get_number_pop_from_cache() {
	return global_cache.get_number_pop_from_cache();
}

unsigned long long __stdcall get_number_in_cache() {
	return global_cache.get_number_in_cache();
}

unsigned long long __stdcall get_cache_size() {
	return global_cache.get_size();
}

unsigned long long __stdcall get_hidden_size() {
	return cur_hidden_size;
}

void __stdcall print_statistics(const char *file_name) {
	FILE *stat = fopen(file_name, "w");
	fprintf(stat, "TBhits = %lld\n", tbhits);
	fprintf(stat, "Blocks loaded from file = %lld\n", global_cache.get_number_load_from_file());
	fprintf(stat, "Blocks loaded from cache = %lld\n", global_cache.get_number_load_from_cache());
	if (tbhits != 0) fprintf(stat, "Procent of loaded from cache = %.2lf%%\n", (double)(tbhits - global_cache.get_number_load_from_file()) * 100.0 / tbhits);
	fprintf(stat, "Current cache size = %lld Mb (%lld Mb)\n", global_cache.get_size() >> 20, (total_cache_size - max_hidden_size) >> 20);
	fprintf(stat, "Current hidden size = %lld Mb (%lld Mb)\n", cur_hidden_size >> 20, max_hidden_size >> 20);
	fprintf(stat, "Table order = ");
	for (int i = 0; i < table_order_count; i++) {
		fprintf(stat, "%s%s", orders[table_order[i]], (i < table_order_count - 1) ? ";" : "\n");
	}
	rwlock_rdlock(tb_indexers_locker);
	rwlock_rdlock(cache_file_buf_locker);
	rwlock_rdlock(piece_offsets_locker);
	unsigned long long result = 0;
	std::unordered_map<unsigned long, custom_tb_indexer *>::iterator it;
	std::unordered_map<unsigned long, bufferizer_list<cache_file_bufferizer> *>::iterator it_buf;
	std::unordered_map<unsigned long, bufferizer_list<read_file_bufferizer> *>::iterator it_rd;
	std::unordered_map<unsigned long, piece_offsets_list *>::iterator it_offsets;
	int total_size, total_requests;
	bufferizer_list<cache_file_bufferizer> *buf_list;
	bufferizer_list<read_file_bufferizer> *buf_list_rd;
	bufferizer_list<custom_tb_indexer> *ind_list;
	piece_offsets_list *offsets_list;
	char *table_type_name[] = {"ML", "WL", "TL", "PL", "DL", "ZML", "ZWL", "ZTL", "ZPL", "ZDL"};
	for (it = tb_indexers.begin(); it != tb_indexers.end(); it++) {
		//tb_indexer
		char tbname[40];
		it->second->get_tb_name(tbname);
		fprintf(stat, "%s: size of indexer = %7.2lf Kb\n", tbname, ((double)it->second->get_size()) / 1024.0);
		for (unsigned char color = IDX_WHITE; color <= IDX_BLACK; color++) {
			fprintf(stat, "%s color:\n", (color == IDX_WHITE) ? "White" : "Black");
			unsigned long index;
			//cache_file_bufferizers
			for (int table_type = MIN_TYPE; table_type <= MAX_TYPE; table_type++) {
				if (!MUTABLE_TYPE(table_type))
					// same indexers for all colors and all table's types
					index = (it->first << 1) + color;
				else
					// different indexer for any color and any table's type
					index = it->first & 0xffff;
				it_buf = cache_file_bufferizers[table_type].find(index);
				if (it_buf != cache_file_bufferizers[table_type].end()) {
					total_size = 0;
					total_requests = 0;
					buf_list = it_buf->second;
					while (buf_list != NULL) {
						total_size += buf_list->bufferizer->get_size();
						total_requests += buf_list->requests;
						buf_list = buf_list->next;
					}
					fprintf(stat, "%s tables: total size of bufferizers = %7.2lf Kb, total number of requests = %d\n", table_type_name[table_type], ((double)total_size) / 1024.0, total_requests);
					buf_list = it_buf->second;
					while (buf_list != NULL) {
						fprintf(stat, "Size of bufferizer = %7.2lf Kb, number of requests = %d\n", ((double)buf_list->bufferizer->get_size()) / 1024.0, buf_list->requests);
						buf_list = buf_list->next;
					}
				}
				unsigned long key = (((unsigned long)table_type) << 16) | index;
				//read_file_bufferizers
				it_rd = read_file_bufferizers.find(key);
				if (it_rd != read_file_bufferizers.end()) {
					fprintf(stat, "Read file bufferizers:\n");
					buf_list_rd = it_rd->second;
					while (buf_list_rd != NULL) {
						fprintf(stat, "Size of bufferizer = %7.2lf Kb, number of requests = %d, file = %d\n", ((double)buf_list_rd->bufferizer->get_size()) / 1024.0, buf_list_rd->requests, buf_list_rd->file_number);
						buf_list_rd = buf_list_rd->next;
					}
				}
				//piece_offsets
				it_offsets = piece_offsets_map.find(key);
				if (it_offsets != piece_offsets_map.end()) {
					fprintf(stat, "Piece offsets tables:\n");
					offsets_list = it_offsets->second;
					while (offsets_list != NULL) {
						fprintf(stat, "Size of pcs table = %7.2lf Kb, file = %d\n", (offsets_list->count*sizeof(file_offset)) / 1024.0, offsets_list->file_number);
						offsets_list = offsets_list->next;
					}
				}
			}
		}

		fprintf(stat, "\n");
	}
	rwlock_unlock(piece_offsets_locker);
	rwlock_unlock(cache_file_buf_locker);
	rwlock_unlock(tb_indexers_locker);
	rwlock_unlock(read_file_buf_locker);
	fclose(stat);
#if defined(LOMONOSOV_FULL) && defined(_DEBUG)
	// for debug memory leaks
	clear_cache_all();
	free_init_indexes();
	clear_tb_index_tree();
#endif
}

void __stdcall set_logging(bool logging) {
	logging_memory = logging;
	// delete last statistics
	if (!access("memory_log.log", 0)) unlink("memory_log.log");
	if (!access("cleaning.log", 0)) unlink("cleaning.log");
	if (!access("realloc_cache.log", 0)) unlink("realloc_cache.log");
}
#endif

bool __stdcall get_name_ending(const char *fen, char *tbname) {
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

int get_position_sign(int eval) {
	if (eval == -1)
		return 0;
	else if (eval & 1)
		return 1;
	else
		return -1;
}

void change_internal_value(int *eval, char table_type) {
	switch (table_type) {
	case ML: case PL:
		if (*eval == 0)
			*eval = -1;
		else
			*eval = get_position_sign(*eval) * (*eval);
		break;
	case ZML: case ZPL:
		if (*eval == 0)
			*eval = -1;
		else
			*eval = get_position_sign(*eval) * ((*eval) + 1) / 2;
		break;
		// old
	/*case TL: case WL: case ZTL: case ZWL:
		if ((*eval) == 0)
			*eval = -1;
		else
			*eval = ((*eval) + 1) / 2 + TERNARY_LOSE_VALUE;*/
	}
}

int __stdcall probe_fen(const char *fen, int *eval, char table_type) {
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

int __stdcall probe_fen_special_mate_state(const char *fen, int *eval, char table_type) {
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
	if (result == PROBE_OK && *eval == 0)
		result = PROBE_MATE;
	change_internal_value(eval, table_type);
	return result;
}

int __stdcall probe_fen_dtmz50(const char *fen, int *eval)
{
	int result;
	result = PROBE_NO_TABLE;
	short_pieces_env local_env;
	unsigned char pcs_local[MAX_MEN];
	local_env.men_count = 0;
	local_env.pieces = pcs_local;
	local_env.indexer = NULL;
	color_position local_pos;
	unsigned long local_index;
	result = get_value_from_fen_local(fen, eval, ZWL, &local_env, &local_pos, &local_index);
#ifdef LOMONOSOV_FULL
	if (result == PROBE_NO_TABLE)
		result = get_value_from_load_position_local(eval, ZDL, &local_env, &local_pos, local_index);
	if (result == PROBE_NO_TABLE)
		result = get_value_from_load_position_local(eval, ZML, &local_env, &local_pos, local_index);
#endif
	if (result == PROBE_OK && *eval != 0) {
		result = get_value_from_load_position_local(eval, PL, &local_env, &local_pos, local_index);
#ifdef LOMONOSOV_FULL
		if (result == PROBE_NO_TABLE)
			result = get_value_from_load_position_local(eval, ML, &local_env, &local_pos, local_index);
#endif
	}
	change_internal_value(eval, PL);
	return result;
}

int __stdcall probe_fen_with_order(const char *fen, int *eval) {
	int result;
	result = PROBE_NO_TABLE;
	*eval = 0;
	if (table_order_count <= 0)
		return result;
	short_pieces_env local_env;
	unsigned char pcs_local[MAX_MEN];
	local_env.men_count = 0;
	local_env.pieces = pcs_local;
	local_env.indexer = NULL;
	color_position local_pos;
	unsigned long local_index;
	int table_type = table_order[0];
	result = get_value_from_fen_local(fen, eval, table_type, &local_env, &local_pos, &local_index);
	for (int i = 1; i < table_order_count && result != PROBE_OK; i++) {
		table_type = table_order[i];
		result = get_value_from_load_position_local(eval, table_type, &local_env, &local_pos, local_index);
	}
	change_internal_value(eval, table_type);
	return result;
}

int __stdcall probe_position(int side, unsigned int *psqW, unsigned int *psqB, int *piCount, int sqEnP, int *eval, char table_type, unsigned char castlings) {
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

int __stdcall probe_position_dtmz50(int side, unsigned int *psqW, unsigned int *psqB, int *piCount, int sqEnP, int *eval, unsigned char castlings) {
	int result;
	result = PROBE_NO_TABLE;
	short_pieces_env local_env;
	unsigned char pcs_local[MAX_MEN];
	local_env.men_count = 0;
	local_env.pieces = pcs_local;
	local_env.indexer = NULL;
	color_position local_pos;
	unsigned long local_index;
	result = get_value_from_position_local(side, psqW, psqB, piCount, sqEnP, eval, ZWL, &local_env, &local_pos, &local_index);
#ifdef LOMONOSOV_FULL
	if (result == PROBE_NO_TABLE)
		result = get_value_from_load_position_local(eval, ZDL, &local_env, &local_pos, local_index);
	if (result == PROBE_NO_TABLE)
		result = get_value_from_load_position_local(eval, ZML, &local_env, &local_pos, local_index);
#endif
	if (result == PROBE_OK && *eval != 0) {
		result = get_value_from_load_position_local(eval, PL, &local_env, &local_pos, local_index);
#ifdef LOMONOSOV_FULL
		if (result == PROBE_NO_TABLE)
			result = get_value_from_load_position_local(eval, ML, &local_env, &local_pos, local_index);
#endif
	}
	change_internal_value(eval, PL);
	return result;
}

int __stdcall probe_position_with_order(int side, unsigned int *psqW, unsigned int *psqB, int *piCount, int sqEnP, int *eval, unsigned char castlings) {
	int result;
	result = PROBE_NO_TABLE;
	*eval = 0;
	if (table_order_count <= 0)
		return result;
	short_pieces_env local_env;
	unsigned char pcs_local[MAX_MEN];
	local_env.men_count = 0;
	local_env.pieces = pcs_local;
	local_env.indexer = NULL;
	color_position local_pos;
	unsigned long local_index;
	int table_type = table_order[0];
	result = get_value_from_position_local(side, psqW, psqB, piCount, sqEnP, eval, table_type, &local_env, &local_pos, &local_index);
	for (int i = 1; i < table_order_count && result != PROBE_OK; i++) {
		table_type = table_order[i];
		result = get_value_from_load_position_local(eval, table_type, &local_env, &local_pos, local_index);
	}
	change_internal_value(eval, table_type);
	return result;
}

bool __stdcall set_table_order(const char *order) {
	table_order_count = 0;
	const char *tmp_path = order;
	char line[MAX_PATH];
	int size;
	while(tmp_path != NULL && tmp_path != "") {
		size = strcspn(tmp_path, ";");
		strncpy(line, tmp_path, size);
		line[size] = '\0';
		int j = 0;
		for ( ; j <= MAX_TYPE; j++)
			if (strcmp(line, orders[j]) == 0) break;
		if (j > MAX_TYPE)
			return false;
		table_order[table_order_count] = j;
		table_order_count++;
		tmp_path = strchr(tmp_path, ';');
		if (tmp_path != NULL)
			tmp_path = &tmp_path[1];
	}
	return true;
}

int __stdcall get_table_order(char *order) {
	order[0] = '\0';
	for (int i = 0; i < table_order_count; i++) {
		strcat(order, orders[table_order[i]]);
		if (i < table_order_count - 1)
			strcat(order, ";");
	}
	return table_order_count;
}

void __stdcall set_cache_size(int MB) {
	total_cache_size = MB;
	total_cache_size <<= 20;
	memory_allocation_done = false;
	allocate_cache_memory();
	// Rehashing seems to be not effective (because of hidden cache takes more memory than cache of block)
	//global_cache.rehash();
}

char * __stdcall get_missing_table_name(void) {
	return probe_missing_table_name;
}

//create TB_ini.txt in current directory whis information about exist files and write this in not_exist_tables
//return 0 if error and lenght max pieces if success
void find_not_files() {
	int table_type;
	FILE *TB_file;
	WIN32_FIND_DATAA f;
	HANDLE dir;
	char *str = new char[MAX_PATH];
	char *cur_path = new char[MAX_PATH];
	char *time_path = new char[50];
	char *ex;
	struct tm* clock;
	struct stat attrib;
	int size;
	rwlock_rdlock(paths_locker);
	list<char *>::iterator it_paths = table_paths.begin();
	GetCurrentDirectoryA(sizeof(cur_path[0]) * MAX_PATH, cur_path);
	cur_path = strcat(cur_path, "\\TB_error.txt");
	DeleteFileA(cur_path);
	rwlock_wrlock(not_exist_tables_locker);
	reset_not_exist_tables(false);
	rwlock_unlock(not_exist_tables_locker);
	GetCurrentDirectoryA(sizeof(cur_path[0]) * MAX_PATH, cur_path);
	cur_path = strcat(cur_path, "\\TB_ini.txt");
	TB_file = fopen(cur_path, "w");
	while (it_paths != table_paths.end()) {
		str = strcpy(str, *it_paths);
		str[strlen(str) - 1] = '\0';
		if (stat(str, &attrib)) {
			fclose(TB_file);
			delete(str);
			delete(cur_path);
			delete(time_path);
			rwlock_unlock(paths_locker);
			return;
		}
		clock = gmtime(&(attrib.st_mtime));
		str = strcat(str, "\\*.*");
		dir = FindFirstFileA(str, &f);
		str = strcat(str, " /");
		sprintf(time_path, "%d", clock->tm_year);
		str = strcat(str, time_path);
		sprintf(time_path, "%d", clock->tm_yday);
		str = strcat(str, time_path);
		sprintf(time_path, "%d", clock->tm_hour);
		str = strcat(str, time_path);
		sprintf(time_path, "%d", clock->tm_min);
		str = strcat(str, time_path);
		sprintf(time_path, "%d", clock->tm_sec);
		str = strcat(str, time_path);
		int len = strlen(str);
		str[len] = '/';
		str[len+1] = '\0';
		fprintf(TB_file, "%s\n", str);
		int extension_length;
		if (dir != INVALID_HANDLE_VALUE) {
			do {
				str = strcpy(str, f.cFileName);
				ex = strchr(str, '.');
				if (ex) ex++;
				if (ex && !(f.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && compare_extension(ex, &table_type, &extension_length) && table_type <= MAX_TYPE) {
					// it's used only in the rehashing of global_cache. But the rehashing is disabled.
					//calculate_min_block_size(*it_paths, f.cFileName, table_type);
					ex = &ex[extension_length];
					while (*ex == '.' || (*ex <= '9' && *ex >= '0')) // it may be not first volume (for example *.1)
						ex = &ex[1];
					if (*ex == '\0') { // if it's first volume
						int size_bg = strcspn(&str[1], "Kk") + 1, size_en = strcspn(str, ".");
						int pieces_cnt = size_en;
						if (strcspn(str, "234567") != strlen(str))
							pieces_cnt--;
						if (max_pieces_count[table_type] < pieces_cnt)
							max_pieces_count[table_type] = pieces_cnt;
						if (size_bg == size_en - size_bg) { // may be full color symmetry
							char *tmp_str1 = new char[10], *tmp_str2 = new char[10];
							tmp_str1 = strncpy(tmp_str1, str, size_bg);
							tmp_str1[size_bg] = '\0';
							tmp_str2 = strncpy(tmp_str2, &str[size_bg], size_bg);
							tmp_str2[size_bg] = '\0';
							if (!strcmp(tmp_str1, tmp_str2)) { // full color symmetry
								new_set_cur_table_not_exist(str, table_type);
								int str_color = strlen(str) - 1;
								while (str[str_color] != 'w' && str[str_color] != 'b')
									str_color--;
								if (str[str_color] == 'w')
									str[str_color] = 'b';
								else
									str[str_color] = 'w';
								new_set_cur_table_not_exist(str, table_type);
							} else
								new_set_cur_table_not_exist(str, table_type);
							delete tmp_str1;
							delete tmp_str2;
						} else {
							new_set_cur_table_not_exist(str, table_type);
						}
					}
				}
			} while (FindNextFileA(dir, &f));
		}
		FindClose(dir);
		it_paths++;
	}
	rwlock_unlock(paths_locker);
	str = strncpy(str, "//", 2);
	for (table_type = MIN_TYPE; table_type <= MAX_TYPE; table_type++)
		str[table_type+2] = '0' + max_pieces_count[table_type];
	str[table_type+2] = '\0';
	fprintf(TB_file, "%s\n", str);
	delete(str);
	delete(cur_path);
	delete(time_path);
	rwlock_rdlock(not_exist_tables_locker);
	for (table_type = MIN_TYPE; table_type <= MAX_TYPE; table_type++) {
		for (unsigned int i = 0; i < NOT_EXIST_TABLES_SIZE; ++i) {
			if (fwrite(&not_exist_tables[table_type][i], sizeof(char), 1, TB_file) != 1) {
				rwlock_unlock(not_exist_tables_locker);
				fclose(TB_file);
				return;
			}
		}
	}
	rwlock_unlock(not_exist_tables_locker);
	for (table_type = MIN_TYPE; table_type <= MAX_TYPE; table_type++)
		fwrite(&min_block_size[table_type], sizeof(unsigned long), 1, TB_file);
	fclose(TB_file);
	known_not_exist = true;
}

//read TB_ini.txt in current directory whis information about exist files
//return 0 if error and lenght max pieces if success
void explicit_get_max_pieces_count() {
	int table_type;
	char *path_file = new char[MAX_PATH];
	FILE *TB_file;
	GetCurrentDirectoryA(sizeof(path_file[0]) * MAX_PATH, path_file);
	path_file = strcat(path_file, "\\TB_error.txt");
	TB_file = fopen(path_file, "rb");
	if (TB_file != NULL)
	{
		fclose(TB_file);
		delete(path_file);	
		find_not_files();
		return;
	}
	GetCurrentDirectoryA(sizeof(path_file[0]) * MAX_PATH, path_file);
	path_file = strcat(path_file, "\\TB_ini.txt");
	TB_file = fopen(path_file, "rb");
	if (TB_file == NULL)
	{
		delete(path_file);	
		find_not_files();
		return;
	}
	char str[MAX_PATH];
	char *time_path = new char[50];
	char *time_path_tmp = new char[10];
	bool fl = true;
	struct tm* clock;
	struct stat attrib;
	int separ, max_size = 0;
	char *time_path_from_TB;
	rwlock_rdlock(paths_locker);
	list<char *>::iterator it_paths = table_paths.begin();
	rwlock_wrlock(not_exist_tables_locker);
	reset_not_exist_tables(false);
	rwlock_unlock(not_exist_tables_locker);
	while (fl){
		if (fgets(str, MAX_PATH, TB_file)) {
			if (strncmp(str, "//", 2) != 0) {
				separ = strcspn(str, " ");
				time_path_from_TB = &str[separ + 2];
				str[separ - 3] = '\0';
				if (it_paths == table_paths.end() || strcmp(str, *it_paths)) {
					delete(path_file);
					delete(time_path);
					delete(time_path_tmp);
					fclose(TB_file);
					rwlock_unlock(paths_locker);
					find_not_files();
					return;
				}
				str[separ - 4] = '\0';
				it_paths++;
				separ = strcspn(time_path_from_TB, "/");
				time_path_from_TB[separ] = '\0';
				if (stat(str, &attrib)) {
					delete(path_file);
					delete(time_path);
					delete(time_path_tmp);
					fclose(TB_file);
					rwlock_unlock(paths_locker);
					find_not_files();
					return;
				}
				clock = gmtime(&(attrib.st_mtime));
				sprintf(time_path, "%d", clock->tm_year);
				sprintf(time_path_tmp, "%d", clock->tm_yday);
				time_path = strcat(time_path, time_path_tmp);
				sprintf(time_path_tmp, "%d", clock->tm_hour);
				time_path = strcat(time_path, time_path_tmp);
				sprintf(time_path_tmp, "%d", clock->tm_min);
				time_path = strcat(time_path, time_path_tmp);
				sprintf(time_path_tmp, "%d", clock->tm_sec);
				time_path = strcat(time_path, time_path_tmp);
				if (strcmp(time_path, time_path_from_TB)) {
					delete(path_file);
					delete(time_path);
					delete(time_path_tmp);
					fclose(TB_file);
					rwlock_unlock(paths_locker);
					find_not_files();
					return;
				}
			}
			else {
				for (table_type = MIN_TYPE; table_type <= MAX_TYPE; table_type++)
					max_pieces_count[table_type] = str[2+table_type] - '0';
				fl = false;
			}
		}
		else {
			delete(path_file);
			delete(time_path);
			delete(time_path_tmp);
			fclose(TB_file);
			rwlock_unlock(paths_locker);
			find_not_files();
			return;
		}
	}
	delete(path_file);
	delete(time_path);
	delete(time_path_tmp);
	if (it_paths != table_paths.end()) {
		fclose(TB_file);
		rwlock_unlock(paths_locker);
		find_not_files();
		return;
	}
	rwlock_unlock(paths_locker);
	rwlock_wrlock(not_exist_tables_locker);
	for (table_type = MIN_TYPE; table_type <= MAX_TYPE; table_type++) {
		unsigned int index = 0;
		fl = false;
		while (index < NOT_EXIST_TABLES_SIZE && !fl) {
			if (fread(&not_exist_tables[table_type][index], sizeof(char), 1, TB_file) != 1)
				fl = true;
			++index;
		}
	}
	rwlock_unlock(not_exist_tables_locker);
	for (table_type = MIN_TYPE; table_type <= MAX_TYPE; table_type++)
		fread(&min_block_size[table_type], sizeof(unsigned long), 1, TB_file);
	fclose(TB_file);
	if (fl)
		find_not_files();
	else
		known_not_exist = true;
}

void safe_explicit_get_max_pieces_count() {
	explicit_get_max_pieces_count();
	if (!known_not_exist)
		reset_not_exist_tables(true);
	// Rehashing seems to be not effective (because of hidden cache takes more memory than cache of block)
	//global_cache.rehash();
}

int get_max_pieces_count_extended(char table_type) {
	int max = 0;
	for (std::vector<int>::iterator it_types = types_vector[table_type].begin(); it_types != types_vector[table_type].end(); it_types++) {
		int cur_max = max_pieces_count[*it_types];
		if (cur_max > max)
			max = cur_max;
	}
	return max;
}

int __stdcall get_max_pieces_count(char table_type) {
	if (UNKNOWN_TB_TYPE(table_type))
		return 0;
	if (!known_not_exist)
		safe_explicit_get_max_pieces_count();
	return get_max_pieces_count_extended(table_type);
}

int __stdcall get_max_pieces_count_with_order() {
	if (!known_not_exist)
		safe_explicit_get_max_pieces_count();
	int max = 0;
	for (int i = 0; i < table_order_count; i++) {
		int cur_max = get_max_pieces_count_extended(table_order[i]);
		if (cur_max > max)
			max = cur_max;
	}
	return max;
}

#include <unordered_map>

void explicit_clear_cache(char table_type) {
	rwlock_wrlock(probe_locker);
	std::unordered_map<unsigned long, bufferizer_list<cache_file_bufferizer> *>::iterator it;
	std::unordered_map<unsigned long, bufferizer_list<read_file_bufferizer> *>::iterator it_rd;
	std::unordered_map<unsigned long, piece_offsets_list *>::iterator it_offsets;
	std::unordered_map<unsigned long, custom_tb_indexer *>::iterator it_indexer;
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

void __stdcall clear_cache(char table_type) {
	if (UNKNOWN_TB_TYPE(table_type))
		return;
	for (std::vector<int>::iterator it_types = types_vector[table_type].begin(); it_types != types_vector[table_type].end(); it_types++)
		explicit_clear_cache(*it_types);
}

void __stdcall clear_cache_all() {
	rwlock_wrlock(probe_locker);
	std::unordered_map<unsigned long, bufferizer_list<cache_file_bufferizer> *>::iterator it;
	for (int table_type = MIN_TYPE; table_type <= MAX_TYPE; table_type++) {
		for (it = cache_file_bufferizers[table_type].begin(); it != cache_file_bufferizers[table_type].end(); it++) {
			delete it->second;
		}
		cache_file_bufferizers[table_type].clear();
	}
	std::unordered_map<unsigned long, bufferizer_list<read_file_bufferizer> *>::iterator it_rd;
	for (it_rd = read_file_bufferizers.begin(); it_rd != read_file_bufferizers.end(); it_rd++) {
		delete it_rd->second;
	}
	read_file_bufferizers.clear();
	std::unordered_map<unsigned long, piece_offsets_list *>::iterator it_offsets;
	for (it_offsets = piece_offsets_map.begin(); it_offsets != piece_offsets_map.end(); it_offsets++) {
		delete it_offsets->second;
	}
	piece_offsets_map.clear();
	std::unordered_map<unsigned long, custom_tb_indexer *>::iterator it_indexer;
	for (it_indexer = tb_indexers.begin(); it_indexer != tb_indexers.end(); it_indexer++) {
		delete it_indexer->second;
	}
	tb_indexers.clear();
	cur_hidden_size = 0;
	global_cache.clean_all();
	set_cache_size(total_cache_size >> 20);
	rwlock_unlock(probe_locker);
}

void __stdcall set_hidden_cache_clean_percent(int percent) {
	clean_percent = percent;
}
