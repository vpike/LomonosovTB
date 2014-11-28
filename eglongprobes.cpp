#include <string>
#include <vector>
#include <algorithm>

#include "egmaintypes.h"
#include "egpglobals.h"
#include "egsindex.h"
#include "egintflocal.h"
#include "eglongprobes.h"

using namespace std;

string move_valued_to_string(move_valued move) {
	string res;
	res.clear();
	res.push_back('a' + (move.from & 0x07));
	res.push_back('1' + (move.from >> 4));
	res.push_back('a' + (move.target & 0x07));
	res.push_back('1' + (move.target >> 4));
	if (move.promote != EMPTY)
		res.push_back(piece_name_local[move.promote]);
	if (move.with_value) {
		res.push_back(' ');
		char str_value[8];
		sprintf(str_value, "%d", move.value);
		res.append(str_value);
	}
	return res;
}

void remove_enpass(short_pieces_env *local_env, color_position *local_pos) {
	for (int i = 0; i < local_env->men_count; i++)
		if (local_env->pieces[i] <= WPAWN && (local_pos->cur_pos.piece_pos[i] & 0x70) == pawn_data[local_env->pieces[i]][PD_MY_ENPASS_RANK])
			local_pos->cur_pos.piece_pos[i] += pawn_data[local_env->pieces[i]][PD_MOVE] * 3;
}

// It called for all positions and get value
bool probe_forward_move_tree(short_pieces_env *local_env, color_position *local_pos, char *search_results, int capture, unsigned char promote_pc, int table_type) {
	// First, get move
	move_valued move;
	position pos;
	memcpy(&pos, search_results + 3, sizeof(position));
	int a = (local_pos->cur_wtm ? 0 : local_env->white_pieces);
	int b = (local_pos->cur_wtm ? local_env->white_pieces : local_env->men_count);
	for (int i = a; i < b; i++) {
		if (pos.piece_pos[i] != local_pos->cur_pos.piece_pos[i]) {
			move.from = pos.piece_pos[i];
			move.target = local_pos->cur_pos.piece_pos[i];
			break;
		}
	}
	move.promote = promote_pc;
	move.with_value = true;
	if (search_results[2]) { // need_invert
		move.from ^= 0x70;
		move.target ^= 0x70;
	}

	// get moves array
	vector<move_valued> *moves;
	memcpy(&moves, search_results + 3 + sizeof(position), sizeof(void *));

	// local_pos to minor_pos
	short_pieces_env minor_env;
	unsigned char pcs[8];
	minor_env.pieces = pcs;
	color_position minor_pos;
	position_to_minor_env(local_env, local_pos, &minor_env, &minor_pos, capture, promote_pc);

	// if kk table
	if (minor_env.men_count == 2) {
		move.value = 0;
		moves->push_back(move);
		return true;
	}

	// probe table
	int eval = 0;
	int probe_result = PROBE_NO_TABLE;
	unsigned long minor_index = get_table_index_local(minor_pos.cur_wtm, &minor_env);
	probe_result = get_value_from_load_position_local(&eval, table_type, &minor_env, &minor_pos, minor_index);
	if (probe_result != PROBE_OK) {
		search_results[1] = 1;
		return true;
	}
	change_internal_value(&eval, table_type);
	move.value = eval;
	moves->push_back(move);
	return true;
}

// It called for all positions and find the exact value
bool probe_forward_move_line(short_pieces_env *local_env, color_position *local_pos, char *search_results, int capture, unsigned char promote_pc, int table_type) {
	// First, get move
	move_valued move;
	position pos;
	memcpy(&pos, search_results + 2, sizeof(position));
	int a = (local_pos->cur_wtm ? 0 : local_env->white_pieces);
	int b = (local_pos->cur_wtm ? local_env->white_pieces : local_env->men_count);
	for (int i = a; i < b; i++) {
		if (pos.piece_pos[i] != local_pos->cur_pos.piece_pos[i]) {
			move.from = pos.piece_pos[i];
			move.target = local_pos->cur_pos.piece_pos[i];
			break;
		}
	}
	move.promote = promote_pc;
	move.with_value = false;
	if (search_results[1]) {
		move.from ^= 0x70;
		move.target ^= 0x70;
	}

	// get moves array
	vector<move_valued> *moves;
	memcpy(&moves, search_results + 2 + sizeof(position), sizeof(void *));

	// get searched value
	int searched_eval;
	memcpy(&searched_eval, search_results + 2 + sizeof(position) + sizeof(void *), sizeof(int));

	// local_pos to minor_pos
	short_pieces_env minor_env;
	unsigned char pcs[8];
	minor_env.pieces = pcs;
	color_position minor_pos;
	position_to_minor_env(local_env, local_pos, &minor_env, &minor_pos, capture, promote_pc);

	// if kk table
	if (minor_env.men_count == 2) {
		if ((DTM_TYPE(table_type) && searched_eval == -1) || (!DTM_TYPE(table_type) && searched_eval == 0)) {
			moves->push_back(move);
			search_results[0] = 1;
		}
		return true;
	}

	// probe table
	int eval = 0;
	if (DTM_TYPE(table_type) && searched_eval != -1)
		eval = REJECT_SOME(((searched_eval & 1)^1));
	int probe_result = PROBE_NO_TABLE;
	unsigned long minor_index = get_table_index_local(minor_pos.cur_wtm, &minor_env);
	probe_result = get_value_from_load_position_local(&eval, table_type, &minor_env, &minor_pos, minor_index);
	if (probe_result != PROBE_OK)
		return true;
	if (searched_eval == eval) {
		moves->push_back(move);
		search_results[0] = 1;
	}
	return true;
}

int sign(int value) {
	if (value > 0)
		return 1;
	if (value < 0)
		return -1;
	return 0;
}

bool move_valued_comparer(move_valued move1, move_valued move2) {
	int sign1 = sign(move1.value), sign2 = sign(move2.value);
	if (sign1 != sign2)
		return sign1 < sign2;
	return move1.value > move2.value;
}

int get_tree_from_load_position_local(vector<move_valued> *moves, int table_type, short_pieces_env *local_env, color_position *local_pos, bool need_invert) {
	moves->clear();
	char search_results[3 + sizeof(position) + sizeof(void *)]; // {return flag from gen, flag not finished, flag inverted, start position, pointer to moves array}
	search_results[0] = 0;
	search_results[1] = 0;
	search_results[2] = need_invert;
	memcpy(search_results + 3, &local_pos->cur_pos, sizeof(position));
	memcpy(search_results + 3 + sizeof(position), &moves, sizeof(void *));

	unsigned char local_board[128];
	clear_board_local(local_board);
	put_pieces_on_board_local(local_env, local_pos, local_board);
	gen_forward_moves_all_local(local_env, local_pos, local_board, search_results, table_type, probe_forward_move_tree);
	sort(moves->begin(), moves->end(), move_valued_comparer);
	return search_results[1] ? PROBE_NOT_FINISHED : PROBE_OK;
}

// 'eval' = eval of current position (unsigned).
// It returns the best moves to 'move'.
int get_best_move_from_load_position_local(move_valued *move, int eval, int table_type, short_pieces_env *local_env, color_position *local_pos, bool need_invert) {
	vector<move_valued> moves(0);
	vector<move_valued> *moves_ptr = &moves;
	char search_results[2 + sizeof(position) + sizeof(void *) + sizeof(int)]; // {return flag from gen, start position, pointer to move, searched value}
	search_results[0] = 0;
	search_results[1] = need_invert;
	memcpy(search_results + 2, &local_pos->cur_pos, sizeof(position));
	memcpy(search_results + 2 + sizeof(position), &moves_ptr, sizeof(void *));
	if (DTM_TYPE(table_type)) {
		if (eval != -1)
			eval = eval - 1;
	} else {
		if (eval != 0)
			eval = -eval;
	}
	memcpy(search_results + 2 + sizeof(position) + sizeof(void *), &eval, sizeof(int));

	unsigned char local_board[128];
	clear_board_local(local_board);
	put_pieces_on_board_local(local_env, local_pos, local_board);
	gen_forward_moves_all_local(local_env, local_pos, local_board, search_results, table_type, probe_forward_move_line);
	if (moves.size() == 0)
		return PROBE_NO_TABLE;
	*move = moves[0];
	return PROBE_OK;
}

// 'eval' = eval of current position (unsigned).
// It returns the line of best moves to 'moves'.
// If all tables don't exist, the line will be unfinished.
int get_line_from_load_position_local(vector<move_valued> *moves, int eval, int table_type, short_pieces_env *local_env, color_position *local_pos, bool need_invert, int moves_bound) {
	if (!DTM_TYPE(table_type) || DTZ50_TYPE(table_type)) {
		moves->clear();
		return PROBE_UNKNOWN_TB_TYPE;
	}
	unsigned char local_board[128];
	short_pieces_env minor_env;
	unsigned char pcs[8];
	minor_env.pieces = pcs;
	color_position minor_pos;
	while (true) {
		if (eval == 0) // mate
			return PROBE_OK;
		char search_results[2 + sizeof(position) + sizeof(void *) + sizeof(int)]; // {return flag from gen, start position, pointer to moves array, searched value}
		search_results[0] = 0;
		search_results[1] = need_invert;
		memcpy(search_results + 2, &local_pos->cur_pos, sizeof(position));
		memcpy(search_results + 2 + sizeof(position), &moves, sizeof(void *));
		if (eval != -1)
			eval = eval - 1;
		memcpy(search_results + 2 + sizeof(position) + sizeof(void *), &eval, sizeof(int));

		clear_board_local(local_board);
		put_pieces_on_board_local(local_env, local_pos, local_board);
		int moves_size = moves->size();
		gen_forward_moves_all_local(local_env, local_pos, local_board, search_results, table_type, probe_forward_move_line);
		if (moves_size == moves->size())
			return PROBE_NOT_FINISHED;
		if (eval == -1)
			return PROBE_OK;
		if (moves->size() >= moves_bound)
			return PROBE_OK;

		// make move
		remove_enpass(local_env, local_pos);
		move_valued move = moves->back();
		if (need_invert) {
			move.from ^= 0x70;
			move.target ^= 0x70;
		}
		int capture = EMPTY;
		int a = (!local_pos->cur_wtm ? 0 : local_env->white_pieces);
		int b = (!local_pos->cur_wtm ? local_env->white_pieces : local_env->men_count);
		for (int i = a; i < b; i++) {
			if (move.target == local_pos->cur_pos.piece_pos[i]) {
				capture = i;
				local_pos->cur_pos.piece_pos[i] = BOX;
				break;
			}
		}
		a = (local_pos->cur_wtm ? 0 : local_env->white_pieces);
		b = (local_pos->cur_wtm ? local_env->white_pieces : local_env->men_count);
		for (int i = a; i < b; i++) {
			if (move.from == local_pos->cur_pos.piece_pos[i]) {
				local_pos->cur_pos.piece_pos[i] = move.target;
				break;
			}
		}
		
		bool new_need_invert;
		position_to_minor_env(local_env, local_pos, &minor_env, &minor_pos, capture, move.promote, &new_need_invert);
		memcpy(local_env->pieces, minor_env.pieces, 8);
		local_env->men_count = minor_env.men_count;
		local_env->white_pieces = minor_env.white_pieces;
		local_env->full_color_symmetry = minor_env.full_color_symmetry;
		local_env->pslice_number = minor_env.pslice_number;
		local_pos->cur_pos = minor_pos.cur_pos;
		local_pos->cur_wtm = minor_pos.cur_wtm;
		need_invert ^= new_need_invert;
	}

	return PROBE_OK;
}