#ifndef EGLONGPROBES_H_
#define EGLONGPROBES_H_

#include <string>

struct move_valued {
	int value;
	unsigned char from;
	unsigned char target;
	unsigned char promote;
	bool with_value;
};

int sign(int value);
std::string move_valued_to_string(move_valued move);

int get_tree_from_load_position_local(vector<move_valued> *moves, int table_type, short_pieces_env *local_env, color_position *local_pos, bool need_invert);
int get_best_move_from_load_position_local(move_valued *move, int eval, int table_type, short_pieces_env *local_env, color_position *local_pos, bool need_invert);
int get_line_from_load_position_local(vector<move_valued> *moves, int eval, int table_type, short_pieces_env *local_env, color_position *local_pos, bool need_invert, int moves_bound);

#endif
