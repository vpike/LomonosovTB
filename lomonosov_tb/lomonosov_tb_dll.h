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

// initialization
extern "C" __declspec(dllexport) bool APIENTRY __stdcall DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpReserved);
// control paths
extern "C" __declspec(dllexport) void __stdcall dll_add_table_path(const char *path);
extern "C" __declspec(dllexport) void __stdcall dll_set_table_path(const char *path);
// control cache
extern "C" __declspec(dllexport) void __stdcall dll_set_cache_size(int MB);
extern "C" __declspec(dllexport) void __stdcall dll_clear_cache(char table_type);
extern "C" __declspec(dllexport) void __stdcall dll_clear_cache_all();
// control order
extern "C" __declspec(dllexport) bool __stdcall dll_set_table_order(const char *order); // return result of setting (true or false)
extern "C" __declspec(dllexport) int __stdcall dll_get_table_order(char *order); // return count of orders
// scan tables & get max pieces count
extern "C" __declspec(dllexport) int __stdcall dll_get_max_pieces_count(char table_type);
extern "C" __declspec(dllexport) int __stdcall dll_get_max_pieces_count_with_order();
// table names
extern "C" __declspec(dllexport) bool __stdcall dll_get_table_name(const char *fen, char *tbname);
extern "C" __declspec(dllexport) void __stdcall dll_get_missing_table_name(char *tbname);
// probe
extern "C" __declspec(dllexport) int __stdcall dll_probe_fen(const char *fen, int *eval, char table_type);
extern "C" __declspec(dllexport) int __stdcall dll_probe_fen_with_order(const char *fen, int *eval);
extern "C" __declspec(dllexport) int __stdcall dll_probe_fen_dtmz50(const char *fen, int *eval);
extern "C" __declspec(dllexport) int __stdcall dll_probe_position(int side, unsigned int *psqW, unsigned int *psqB, int *piCount, int sqEnP, int *eval, char table_type, unsigned char castlings = 0);
extern "C" __declspec(dllexport) int __stdcall dll_probe_position_with_order(int side, unsigned int *psqW, unsigned int *psqB, int *piCount, int sqEnP, int *eval, unsigned char castlings = 0);
extern "C" __declspec(dllexport) int __stdcall dll_probe_position_dtmz50(int side, unsigned int *psqW, unsigned int *psqB, int *piCount, int sqEnP, int *eval, unsigned char castlings = 0);
#ifndef TB_DLL_EXPORT
extern "C" __declspec(dllexport) unsigned long long __stdcall dll_get_number_load_from_cache();
extern "C" __declspec(dllexport) unsigned long long __stdcall dll_get_number_load_from_file();
extern "C" __declspec(dllexport) unsigned long long __stdcall dll_get_number_pop_from_cache();
extern "C" __declspec(dllexport) unsigned long long __stdcall dll_get_number_in_cache();
extern "C" __declspec(dllexport) unsigned long long __stdcall dll_get_cache_size();
extern "C" __declspec(dllexport) unsigned long long __stdcall dll_get_hidden_size();
extern "C" __declspec(dllexport) void __stdcall dll_set_logging(bool logging);
extern "C" __declspec(dllexport) void __stdcall dll_set_hidden_cache_clean_percent(int percent);
extern "C" __declspec(dllexport) void __stdcall dll_print_statistics(const char *file_name);
extern "C" __declspec(dllexport) int __stdcall dll_probe_fen_special_mate_state(const char *fen, int *eval, char table_type);
#endif
