#ifndef LOMONOSOV_TB_H_
#define LOMONOSOV_TB_H_

/*
Lomonosov TableBases. Basic module for probing.

See description of the table types and the codes of probing result in the beginning of egmaintypes.h.

Resulting evaluations for ternary tables are:
-1: lose,
0: draw,
1: win.
Resulting evaluations for dtm tables are:
0: draw,
+n: win, n moves to win (win / capture move / pawn move, for dtz50),
-n: lose, n moves to lose (lose / capture move / pawn move, for dtz50),
-1: mate (mate or -n, n = 1, for dtz50.

See description of binary position structure in egintflocal.cpp before the function load_lomonosov_tb_position_local(...).
*/

// initialization
void init_lomonosov_tb();
// control table paths
void add_table_path(const char *path);
void set_table_path(const char *path);
// control cache
void set_cache_size(int MB);
void clear_cache(char table_type);
void clear_cache_all();
// scan table paths for tables & get max pieces count
int get_max_pieces_count(char table_type);
// table names
bool get_table_name(const char *fen, char *tbname);
void get_missing_table_name(char *tbname);
// probe
int probe_fen(const char *fen, int *eval, char table_type);
int probe_position(int side, unsigned int *psqW, unsigned int *psqB, int *piCount, int sqEnP, int *eval, char table_type, unsigned char castlings = 0);

#endif
