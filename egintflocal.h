#ifndef EGINTFLOCAL_H_
#define EGINTFLOCAL_H_

#include <stdio.h>
#include <vector>
#include "egmaintypes.h"
#include "egcachebuf.h"

// Values, returned by get_value_...:
// In dtm:
// 0 - mate, 0 moves to mate
// 1 -  win, 1 moves to mate
// 2 - lose, 2 moves to mate
// ...
// In dtz50:
// 0 - mate, 0 moves
// 1 -  win, 1 move to capture/pawn move
// 2 - lose, 1 move to c.
// 3 -  win, 2 moves to c.
// 4 - lose, 2 moves to c.
// ...
// 199 -  win, 100 moves to c.
// 200 - lose, 100 moves to c.
#define NEXT_VALUE(mtm, dtz50) ((dtz50 && (mtm % 2 == 1)) ? (mtm + 3) : (mtm + 1))
#define PREV_VALUE(mtm, dtz50) ((dtz50 && (mtm % 2 == 0)) ? (mtm - 3) : (mtm - 1))

// set it before get_value_from_load_position_local, if want restrict and boost pl-probing
#define NEED_IN_LOSE 4097 // it's logical, we need in lose forward positions, if this position is win
#define NEED_IN_WIN 4096
#define NEED_IN_SOME(win) (NEED_IN_WIN + win)
#define REJECT_PROBE(win, eval) (NEED_IN_WIN + win == eval)

typedef struct {
	position cur_pos;
	bool cur_wtm;
} color_position;

// These are used in move generation and probing minors.
typedef bool forward_move_func_local(short_pieces_env *local_env, color_position *local_pos, char *search_results, int capture, unsigned char promote_pc, int table_type);
typedef bool forward_move_simple_func_local(short_pieces_env *local_env, color_position *local_pos, char *search_results);
void clear_board_local(unsigned char *local_board);
void put_pieces_on_board_local(short_pieces_env *local_env, color_position *local_pos, unsigned char *local_board);
void take_pieces_from_board_local(short_pieces_env *local_env, color_position *local_pos, unsigned char *local_board);
bool is_legal_position_local(short_pieces_env *local_env, position *pos, bool white_to_move);
int get_pslice(position *pos, unsigned char *local_pieces, int local_white_pieces);
int get_invert_pslice(position *pos, unsigned char *local_pieces, int local_men_count);
void invert_position(position *pos, int local_men_count);
void invert_colors(position *pos, int local_white_pieces);
void position_to_minor_env(short_pieces_env *local_env, color_position *local_pos, short_pieces_env *minor_env, 
	color_position *minor_pos, int capture, unsigned long promote_pc);
bool gen_forward_moves_capture_local(short_pieces_env *local_env, color_position *local_pos, unsigned char *local_board,
	char *search_results, int table_type, forward_move_func_local *func);

// Get the value from a certain table on a certain path. It can get wrong value for the types PL and ZPL,
// because of the don't care values for these types can occur (see get_value_from_own_color for right using).
// For correct getting value, please use one of the next 4 functions.
int get_value_local(int *eval, int table_type, short_pieces_env *local_env, color_position *local_pos,
	unsigned long local_index, char *table_path);
// Get the value, using only own color.
int get_value_from_own_color_local(int *eval, int table_type, short_pieces_env *local_env, color_position *local_pos,
	unsigned long local_index);
// Get the value with the possiblity of getting the value from another color.
int get_value_from_load_position_local(int *eval, int table_type, short_pieces_env *local_env, color_position *local_pos,
	unsigned long local_index);
// Get the value from the fen.
int get_value_from_fen_local(const char *fen, int *eval, int table_type, short_pieces_env *local_env, color_position *local_pos,
	unsigned long *local_index);
// Get the value from the lomonosov position.
int get_value_from_position_local(int side, unsigned int *psqW, unsigned int *psqB, int *piCount, int sqEnP, int *eval,
	int table_type, short_pieces_env *local_env, color_position *local_pos, unsigned long *local_index);

// Next functions convert fen and lomonosov position to internal position type.
// For the correct using see get_value_from_fen_local and get_value_from_position_local.
bool load_pieces_from_fen_local(const char *s, bool *ztb_invert_color, short_pieces_env *local_env);
bool load_fen_local(const char *s, bool *invert_pieces, short_pieces_env *local_env, color_position *local_pos);
bool load_lomonosov_tb_position_local(int side, unsigned int *psqW, unsigned int *psqB, int *piCount, int sqEnP, 
	bool *ztb_invert_color, short_pieces_env *local_env, color_position *local_pos);

// Get the fen from the position in internal form.
void form_fen_local(position *pos, bool wtm, char *fen, short_pieces_env *local_env);
// Get the name of ending for the fen.
void get_tb_name_without_slice_local(char *buf, short_pieces_env *local_env);
void get_output_tb_filename_local(char *tab_file, int kind, short_pieces_env *local_env, char *table_path);

// Combination of table types, used in all functions, except get_value_local.
// One digit in WL, ZWL, PL, ZPL, ML, ZML, TL, ZTL.
// Two digits in DL and ZDL: TL, DL and ZTL, ZDL.
extern std::vector<int> types_vector[TYPES_COUNT];
void init_types_vector();
int get_max_pieces_count_extended(char table_type);

void change_internal_value(int *eval, char table_type);

// Adress to the array of existing tables.
unsigned long get_table_index_local(bool wtm, short_pieces_env *local_env);
void set_cur_table_not_exist_local(unsigned long index, int table_type);
int get_cur_table_not_exist_local(unsigned long index, int table_type);

#endif /* EGINTFLOCAL_H_ */
