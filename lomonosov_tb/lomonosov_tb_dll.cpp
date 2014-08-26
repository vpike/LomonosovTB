#include <stdlib.h>
#include <stdio.h>
#include "lomonosov_tb.h"
#include "lomonosov_tb_dll.h"
#include "../egmaintypes.h"
#include "../egpglobals.h"
#include "../egintflocal.h"
#include "../egcachecontrol.h"
#include "../egtbfile.h"
#include "../egtbscanner.h"
#ifdef LOMONOSOV_FULL
#include "../egindex.h"
#endif

#ifdef PARALLEL_TB
#include <pthread.h>
#endif

#ifdef LOMONOSOV_FULL
int table_order_count = 8;
int table_order[8] = {WL, TL, PL, ML, ZWL, ZTL, ZPL, ZML};
char *orders[] = {"DTM", "WL", "WDL", "PL", "nothing", "DTZ50", "WL50", "WDL50", "PL50", "nothing"};
#else
int table_order_count = 4;
int table_order[4] = {WL, PL, ZWL, ZPL};
char *orders[] = {"nothing", "WL", "nothing", "PL", "nothing", "nothing", "WL50", "nothing", "PL50", "nothing"};
#endif

bool APIENTRY __stdcall DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpReserved) {
	if (fdwReason == DLL_PROCESS_ATTACH) {
		init_lomonosov_tb();
    }
    return TRUE;
}

void __stdcall dll_add_table_path(const char *path) {
	add_table_path(path);
}

void __stdcall dll_set_table_path(const char *path) {
	set_table_path(path);
}

void __stdcall dll_set_cache_size(int MB) {
	set_cache_size(MB);
}

void __stdcall dll_clear_cache(char table_type) {
	clear_cache(table_type);
}

void __stdcall dll_clear_cache_all() {
	clear_cache_all();
}

bool __stdcall dll_set_table_order(const char *order) {
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

int __stdcall dll_get_table_order(char *order) {
	order[0] = '\0';
	for (int i = 0; i < table_order_count; i++) {
		strcat(order, orders[table_order[i]]);
		if (i < table_order_count - 1)
			strcat(order, ";");
	}
	return table_order_count;
}

int __stdcall dll_get_max_pieces_count(char table_type) {
	return get_max_pieces_count(table_type);
}

int __stdcall dll_get_max_pieces_count_with_order() {
	if (!known_not_exist)
		scan_tables();
	int max = 0;
	for (int i = 0; i < table_order_count; i++) {
		int cur_max = get_max_pieces_count_extended(table_order[i]);
		if (cur_max > max)
			max = cur_max;
	}
	return max;
}

bool __stdcall dll_get_table_name(const char *fen, char *tbname) {
	return get_table_name(fen, tbname);
}

void __stdcall dll_get_missing_table_name(char *tbname) {
	get_missing_table_name(tbname);
}

int __stdcall dll_probe_fen(const char *fen, int *eval, char table_type) {
	return probe_fen(fen, eval, table_type);
}

// It's special function for TB7Service.
// 1. It can return PROBE_MATE.
// 2. It must get right (*eval). *eval can be 1 (need in win) or -1 (need in lose)
int __stdcall dll_probe_fen_special_mate_state(const char *fen, int *eval, char table_type) {
	if (UNKNOWN_TB_TYPE(table_type))
		return PROBE_UNKNOWN_TB_TYPE;
	if (*eval == 1)
		*eval = NEED_IN_WIN;
	else if (*eval == -1)
		*eval = NEED_IN_LOSE;
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

int __stdcall dll_probe_fen_dtmz50(const char *fen, int *eval)
{
	*eval = 0;
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

int __stdcall dll_probe_fen_with_order(const char *fen, int *eval) {
	*eval = 0;
	int result;
	result = PROBE_NO_TABLE;
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

int __stdcall dll_probe_position(int side, unsigned int *psqW, unsigned int *psqB, int *piCount, int sqEnP, int *eval, char table_type, unsigned char castlings) {
	return probe_position(side, psqW, psqB, piCount, sqEnP, eval, table_type, castlings);
}

int __stdcall dll_probe_position_dtmz50(int side, unsigned int *psqW, unsigned int *psqB, int *piCount, int sqEnP, int *eval, unsigned char castlings) {
	*eval = 0;
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

int __stdcall dll_probe_position_with_order(int side, unsigned int *psqW, unsigned int *psqB, int *piCount, int sqEnP, int *eval, unsigned char castlings) {
	*eval = 0;
	int result;
	result = PROBE_NO_TABLE;
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

#ifndef TB_DLL_EXPORT
unsigned long long __stdcall dll_get_number_load_from_cache() {
	return global_cache.get_number_load_from_cache();
}

unsigned long long __stdcall dll_get_number_load_from_file() {
	return global_cache.get_number_load_from_file();
}

unsigned long long __stdcall dll_get_number_pop_from_cache() {
	return global_cache.get_number_pop_from_cache();
}

unsigned long long __stdcall dll_get_number_in_cache() {
	return global_cache.get_number_in_cache();
}

unsigned long long __stdcall dll_get_cache_size() {
	return global_cache.get_size();
}

unsigned long long __stdcall dll_get_hidden_size() {
	return cur_hidden_size;
}

void __stdcall dll_set_logging(bool logging) {
	logging_memory = logging;
	// delete last statistics
	if (!access("memory_log.log", 0)) unlink("memory_log.log");
	if (!access("cleaning.log", 0)) unlink("cleaning.log");
	if (!access("realloc_cache.log", 0)) unlink("realloc_cache.log");
}

void __stdcall dll_set_hidden_cache_clean_percent(int percent) {
	clean_percent = percent;
}

void __stdcall dll_print_statistics(const char *file_name) {
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

#endif
