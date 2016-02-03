#include <windows.h>

/*
table_type in get_max_pieces_count, probe_fen, probe_position:
0 - ML tables
1 - WL tables
2 - TL tables
3 - PL tables
4 - TL and DL ternary tables (TL is more preferable than DL)
5 - ZML tables
6 - ZWL tables
7 - ZTL tables
9 - ZPL tables
9 - ZTL and ZDL WDL50 tables (ZTL is more preferable than ZDL)

table_type in clear_cache:
0 - clear ML
1 - clear WL
2 - clear TL
3 - clear TL and DL
4 - clear PL
5 - clear ZML
6 - clear ZWL
7 - clear ZTL
8 - clear ZPL
6 - clear ZTL and ZDL
*/

#define FUNC_SYNTAX extern "C" __declspec(dllexport)

// initialization
FUNC_SYNTAX bool APIENTRY __stdcall DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpReserved);
// control paths
FUNC_SYNTAX void dll_add_table_path(const char *path);
FUNC_SYNTAX void dll_set_table_path(const char *path);
// control cache
FUNC_SYNTAX void dll_set_cache_size(int MB);
FUNC_SYNTAX void dll_clear_cache(char table_type);
FUNC_SYNTAX void dll_clear_cache_all();
// control order
FUNC_SYNTAX bool dll_set_table_order(const char *order); // return result of setting (true or false)
FUNC_SYNTAX int dll_get_table_order(char *order); // return count of orders
// scan tables & get max pieces count
FUNC_SYNTAX int dll_get_max_pieces_count(char table_type);
FUNC_SYNTAX int dll_get_max_pieces_count_with_order();
// table names
FUNC_SYNTAX bool dll_get_table_name(const char *fen, char *tbname);
FUNC_SYNTAX void dll_get_missing_table_name(char *tbname);
// probe
FUNC_SYNTAX int dll_probe_fen(const char *fen, int *eval, char table_type);
FUNC_SYNTAX int dll_probe_fen_with_order(const char *fen, int *eval, char *table_type = 0);
FUNC_SYNTAX int dll_probe_fen_dtmz50(const char *fen, int *eval);
FUNC_SYNTAX int dll_probe_position(int side, unsigned int *psqW, unsigned int *psqB, int *piCount, int sqEnP, int *eval, char table_type, unsigned char castlings = 0);
FUNC_SYNTAX int dll_probe_position_with_order(int side, unsigned int *psqW, unsigned int *psqB, int *piCount, int sqEnP, int *eval, char *table_type = 0, unsigned char castlings = 0);
FUNC_SYNTAX int dll_probe_position_dtmz50(int side, unsigned int *psqW, unsigned int *psqB, int *piCount, int sqEnP, int *eval, unsigned char castlings = 0);
#ifndef TB_DLL_EXPORT
FUNC_SYNTAX unsigned long long dll_get_number_load_from_cache();
FUNC_SYNTAX unsigned long long dll_get_number_load_from_file();
FUNC_SYNTAX unsigned long long dll_get_number_pop_from_cache();
FUNC_SYNTAX unsigned long long dll_get_number_in_cache();
FUNC_SYNTAX unsigned long long dll_get_cache_size();
FUNC_SYNTAX unsigned long long dll_get_hidden_size();
FUNC_SYNTAX void dll_set_logging(bool logging);
FUNC_SYNTAX void dll_set_hidden_cache_clean_percent(int percent);
FUNC_SYNTAX void dll_print_statistics(const char *file_name);
FUNC_SYNTAX int dll_probe_fen_special_mate_state(const char *fen, int *eval, char table_type);
// long probes. Be sure that size of moves is large enough.
FUNC_SYNTAX int dll_get_tree_fen(const char *fen, char *moves, char table_type);
FUNC_SYNTAX int dll_get_tree_bounded_fen(const char *fen, char *moves, char table_type, int best_bound, int mid_bound, int worse_bound);
FUNC_SYNTAX int dll_get_cells_fen(const char *fen, unsigned int piece_place, char *moves, char table_type);
FUNC_SYNTAX int dll_get_best_move_fen(const char *fen, char *move, char table_type);
FUNC_SYNTAX int dll_get_line_fen(const char *fen, char *moves, char table_type);
FUNC_SYNTAX int dll_get_line_bounded_fen(const char *fen, char *moves, char table_type, int moves_bound);
#endif
