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
char table_order[8] = {WL, TL, PL, ML, ZWL, ZTL, ZPL, ZML};
char *orders[] = {"DTM", "WL", "WDL", "PL", "nothing", "DTZ50", "WL50", "WDL50", "PL50", "nothing"};
#else
int table_order_count = 4;
char table_order[4] = {PL, WL};
char *orders[] = {"nothing", "WL", "nothing", "PL", "nothing", "nothing", "WL50", "nothing", "PL50", "nothing"};
#endif

bool APIENTRY __stdcall DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpReserved) {
	if (fdwReason == DLL_PROCESS_ATTACH) {
		// set current directory = the directory of dll
		char path[MAX_PATH];
		HMODULE hm = NULL;
		GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
			"lomonosov_tb.dll", &hm);
		GetModuleFileNameA(hm, path, sizeof(path));
		int len = strlen(path);
		while (len > 0 && path[len] != PATH_SEPAR)
			path[len--] = '\0';
		SetCurrentDirectory(path);

		init_lomonosov_tb();
    } else if (fdwReason == DLL_PROCESS_DETACH) {
		clear_cache_all();
		clear_tb_index_tree();
#ifdef LOMONOSOV_FULL
		free_init_indexes();
#endif
	}
    return TRUE;
}

void dll_add_table_path(const char *path) {
	add_table_path(path);
}

void dll_set_table_path(const char *path) {
	set_table_path(path);
}

void dll_set_cache_size(int MB) {
	set_cache_size(MB);
}

void dll_clear_cache(char table_type) {
	clear_cache(table_type);
}

void dll_clear_cache_all() {
	clear_cache_all();
}

bool dll_set_table_order(const char *order) {
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

int dll_get_table_order(char *order) {
	order[0] = '\0';
	for (int i = 0; i < table_order_count; i++) {
		strcat(order, orders[table_order[i]]);
		if (i < table_order_count - 1)
			strcat(order, ";");
	}
	return table_order_count;
}

int dll_get_max_pieces_count(char table_type) {
	return get_max_pieces_count(table_type);
}

int dll_get_max_pieces_count_with_order() {
	return get_max_pieces_count_with_order(table_order_count, table_order);
}

bool dll_get_table_name(const char *fen, char *tbname) {
	return get_table_name(fen, tbname);
}

void dll_get_missing_table_name(char *tbname) {
	get_missing_table_name(tbname);
}

int dll_probe_fen(const char *fen, int *eval, char table_type) {
	return probe_fen(fen, eval, table_type);
}

// It's special function for TB7Service.
// 1. It can return PROBE_MATE.
// 2. It must get right (*eval). *eval can be 1 (need in win) or -1 (need in lose)
int dll_probe_fen_special_mate_state(const char *fen, int *eval, char table_type) {
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

int dll_probe_fen_dtmz50(const char *fen, int *eval)
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

int dll_probe_fen_with_order(const char *fen, int *eval, char *table_type) {
	return probe_fen_with_order(fen, eval, table_order_count, table_order, table_type);
}

int dll_probe_position(int side, unsigned int *psqW, unsigned int *psqB, int *piCount, int sqEnP, int *eval, char table_type, unsigned char castlings) {
	return probe_position(side, psqW, psqB, piCount, sqEnP, eval, table_type, castlings);
}

int dll_probe_position_dtmz50(int side, unsigned int *psqW, unsigned int *psqB, int *piCount, int sqEnP, int *eval, unsigned char castlings) {
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

int dll_probe_position_with_order(int side, unsigned int *psqW, unsigned int *psqB, int *piCount, int sqEnP, int *eval, char *table_type, unsigned char castlings) {
	return probe_position_with_order(side, psqW, psqB, piCount, sqEnP, eval, table_order_count, table_order, table_type, castlings);
}

#ifndef TB_DLL_EXPORT
unsigned long long dll_get_number_load_from_cache() {
	return global_cache.get_number_load_from_cache();
}

unsigned long long dll_get_number_load_from_file() {
	return global_cache.get_number_load_from_file();
}

unsigned long long dll_get_number_pop_from_cache() {
	return global_cache.get_number_pop_from_cache();
}

unsigned long long dll_get_number_in_cache() {
	return global_cache.get_number_in_cache();
}

unsigned long long dll_get_cache_size() {
	return global_cache.get_size();
}

unsigned long long dll_get_hidden_size() {
	return cur_hidden_size;
}

void dll_set_logging(bool logging) {
	set_logging(logging);
}

void dll_set_hidden_cache_clean_percent(int percent) {
	clean_percent = percent;
}

void dll_print_statistics(const char *file_name) {
	print_statistics(file_name, table_order_count, table_order, orders);
}

int dll_get_tree_fen(const char *fen, char *moves, char table_type) {
	std::string str;
	int result = get_tree_fen(fen, &str, table_type);
	strcpy(moves, str.c_str());
	return result;
}

int dll_get_tree_bounded_fen(const char *fen, char *moves, char table_type, int best_bound, int mid_bound, int worse_bound) {
	std::string str;
	int result = get_tree_bounded_fen(fen, &str, table_type, best_bound, mid_bound, worse_bound);
	strcpy(moves, str.c_str());
	return result;
}

int dll_get_cells_fen(const char *fen, unsigned int piece_place, char *moves, char table_type) {
	std::string str;
	int result = get_cells_fen(fen, piece_place, &str, table_type);
	strcpy(moves, str.c_str());
	return result;
}

int dll_get_best_move_fen(const char *fen, char *move, char table_type) {
	std::string str;
	int result = get_best_move_fen(fen, &str, table_type);
	strcpy(move, str.c_str());
	return result;
}

int dll_get_line_fen(const char *fen, char *moves, char table_type) {
	std::string str;
	int result = get_line_fen(fen, &str, table_type);
	strcpy(moves, str.c_str());
	return result;
}

int dll_get_line_bounded_fen(const char *fen, char *moves, char table_type, int moves_bound) {
	std::string str;
	int result = get_line_bounded_fen(fen, &str, table_type, moves_bound);
	strcpy(moves, str.c_str());
	return result;
}

#endif
