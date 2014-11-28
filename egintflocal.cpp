#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "egintflocal.h"
#include "egmaintypes.h"
#include "egpglobals.h"
#include "egtbfile.h"
#include "egcachecontrol.h"
#ifdef LOMONOSOV_FULL
#include "egindex.h"
#include "tern_nota.h"
#endif

vector<int> types_vector[TYPES_COUNT];

#define WIN_CAPTURE 2

//functions with short_pieces_env

#ifdef _MSC_VER
inline int get_pslice(position *pos, unsigned char *local_pieces, int local_white_pieces) {
#else
int get_pslice(position *pos, unsigned char *local_pieces, int local_white_pieces) {
#endif
	int pslice = 0, j;
	for (int i = local_white_pieces - 1; local_pieces[i] == WPAWN; i--)
		if (pos->piece_pos[i] != BOX && (j=(adjust_enpassant_pos(pos->piece_pos[i], local_pieces[i]) >> 4)) > pslice && j < 7)
			pslice = j;
	return pslice;
}

int get_invert_pslice(position *pos, unsigned char *local_pieces, int local_men_count) {
	int pslice = 0, j;
	for (int i = local_men_count - 1; local_pieces[i] == BPAWN; i--)
		if (pos->piece_pos[i] != BOX && (j=7 - (adjust_enpassant_pos(pos->piece_pos[i], local_pieces[i]) >> 4)) > pslice && j < 7)
			pslice = j;
	return pslice;
}

#ifdef _MSC_VER
inline void invert_position(position *pos, int local_men_count) {
#else
void invert_position(position *pos, int local_men_count) {
#endif
	for (int i = 0; i < local_men_count; i++)
		if (pos->piece_pos[i] != BOX)
			pos->piece_pos[i] ^= 0x70;
}

#ifdef _MSC_VER
inline void invert_colors(position *pos, int local_white_pieces) {
#else
void invert_colors(position *pos, int local_white_pieces) {
#endif
	unsigned char tmp;
	for (int i = 0; i < local_white_pieces; i++) {
		tmp = pos->piece_pos[i];
		pos->piece_pos[i] = pos->piece_pos[i + local_white_pieces];
		pos->piece_pos[i + local_white_pieces] = tmp;
	}
}

#ifdef _MSC_VER
inline bool has_full_color_symmetry_local(short_pieces_env *local_env) {
#else
bool has_full_color_symmetry_local(short_pieces_env *local_env) {
#endif
    bool symm;
    int i;

    // a P-sliced table cannot have color symmetry
    symm = (local_env->white_pieces == local_env->men_count-local_env->white_pieces) && !local_env->pslice_number;
    if (symm)
        for (i = 0; i < local_env->white_pieces; i++)
            if (local_env->pieces[i] != local_env->pieces[i + local_env->white_pieces] &&
                    (local_env->pieces[i] != WPAWN || local_env->pieces[i + local_env->white_pieces] != BPAWN)) {
                symm = false;
                break;
            }
    return symm;
}

void get_tb_name_without_slice_local(char *buf, short_pieces_env *local_env) {
	int i, j;

	j = 0;
	for (i = 0; i < local_env->men_count; i++)
		buf[j++] = piece_name_local[local_env->pieces[i]];
	buf[j]   = '\0';
}

void get_tb_name_local(char *buf, short_pieces_env *local_pieces) {
	int i, j;

	j = 0;
	for (i = 0; i < local_pieces->men_count; i++) {
		if (i == local_pieces->white_pieces && local_pieces->pieces[i-1] == WPAWN && local_pieces->pslice_number)
			buf[j++] = local_pieces->pslice_number+1+'0';
		buf[j++] = piece_name_local[local_pieces->pieces[i]];
	}
	buf[j]   = '\0';
}

void get_output_tb_filename_local(char *tab_file, int kind, short_pieces_env *local_env, char *table_path) {
	char tbname[MAX_PATH];

	get_tb_name_local(tbname, local_env);
	sprintf(tab_file, "%s%s.%s", table_path, tbname, tbext[kind]);
}

void position_to_minor_env(short_pieces_env *local_env, color_position *local_pos, short_pieces_env *minor_env, 
	color_position *minor_pos, int capture, unsigned long promote_pc, bool *was_invert) {
	int start, end, search_pc, horizont, promote_num;
	minor_env->men_count = local_env->men_count;
	minor_env->white_pieces = local_env->white_pieces;
	memcpy(minor_env->pieces, local_env->pieces, local_env->men_count);
	minor_pos->cur_pos = local_pos->cur_pos;
	minor_pos->cur_wtm = !local_pos->cur_wtm;
	if (capture != EMPTY) {
		minor_env->men_count--;
		if (capture < local_env->white_pieces)
			minor_env->white_pieces--;
		memcpy(minor_env->pieces + capture, local_env->pieces + capture + 1, minor_env->men_count - capture);
		memcpy(minor_pos->cur_pos.piece_pos + capture, local_pos->cur_pos.piece_pos + capture + 1, minor_env->men_count - capture);
	}
	if (promote_pc != EMPTY) {
		start = (local_pos->cur_wtm ? 0 : local_env->white_pieces);
		end = (local_pos->cur_wtm ? local_env->white_pieces : local_env->men_count);
		search_pc = (local_pos->cur_wtm ? WPAWN : BPAWN);
		horizont = (local_pos->cur_wtm ? 7 : 0);
		for (int i = start; i < end; i++)
			if (local_env->pieces[i] == search_pc && (local_pos->cur_pos.piece_pos[i] >> 4) == horizont)
				promote_num = i;
		if (capture != EMPTY && promote_num > capture)
			promote_num--;
		minor_env->pieces[promote_num] = promote_pc;
	}
	int map[8];
	bool invert_color;
	pieces_to_canonical(minor_env->pieces, minor_env->men_count, &minor_env->white_pieces, map, &invert_color);
	position tmp_pos = minor_pos->cur_pos;
	for (int i = 0; i < minor_env->men_count; i++)
		minor_pos->cur_pos.piece_pos[map[i]] = tmp_pos.piece_pos[i];
	minor_env->pslice_number = minor_env->pieces[minor_env->white_pieces - 1] == WPAWN; // if pslice = 0, it shoold be known in has_full_color_symmetry
	if ((minor_env->full_color_symmetry = has_full_color_symmetry_local(minor_env)) && !minor_pos->cur_wtm) {
		invert_color = true;
		invert_colors(&minor_pos->cur_pos, minor_env->white_pieces);
	}
	if (invert_color) {
		invert_position(&minor_pos->cur_pos, minor_env->men_count);
		minor_pos->cur_wtm = !minor_pos->cur_wtm;
	}
	minor_env->pslice_number = get_pslice(&minor_pos->cur_pos, minor_env->pieces, minor_env->white_pieces);
	if (was_invert)
		*was_invert = invert_color;
}

void put_pieces_on_board_local(short_pieces_env *local_env, color_position *local_pos, unsigned char *local_board) {
	int i, p;

	for (i = 0; i < local_env->men_count; i++)
		if ((p=local_pos->cur_pos.piece_pos[i]) != BOX) {
			p = adjust_enpassant_pos(p, local_env->pieces[i]);
			local_board[p] = i;
		}
}

void take_pieces_from_board_local(short_pieces_env *local_env, color_position *local_pos, unsigned char *local_board) {
	int i, p;

	for (i = 0; i < local_env->men_count; i++)
		if ((p=local_pos->cur_pos.piece_pos[i]) != BOX) {
			p = adjust_enpassant_pos(p, local_env->pieces[i]);
			local_board[p] = EMPTY;
		}
}

void clear_board_local(unsigned char *local_board) {
	memset(local_board, EMPTY, 128);
}

void determine_enpass(int *enpass_in_cur_pos, short_pieces_env *local_env, color_position *local_pos) {
	*enpass_in_cur_pos = 0;
	for (int i = 0; i < local_env->men_count; i++)
		if (local_env->pieces[i] <= WPAWN && (local_pos->cur_pos.piece_pos[i] & 0x70) == pawn_data[local_env->pieces[i]][PD_MY_ENPASS_RANK])
			*enpass_in_cur_pos = i;
}

void change_enpass(int *enpass_in_cur_pos, short_pieces_env *local_env, color_position *local_pos) {
	if ((*enpass_in_cur_pos) > 0 && 
	(local_pos->cur_pos.piece_pos[*enpass_in_cur_pos] & 0x70) == pawn_data[local_env->pieces[*enpass_in_cur_pos]][PD_MY_ENPASS_RANK])
		local_pos->cur_pos.piece_pos[*enpass_in_cur_pos] += pawn_data[local_env->pieces[*enpass_in_cur_pos]][PD_MOVE] * 3;
}

void return_to_enpass(int *enpass_in_cur_pos, short_pieces_env *local_env, color_position *local_pos) {
	if ((*enpass_in_cur_pos) > 0 && 
	(local_pos->cur_pos.piece_pos[*enpass_in_cur_pos] & 0x70) == pawn_data[pawn_data[local_env->pieces[*enpass_in_cur_pos]][PD_ENEMY]][PD_ENPASS_RANK])
		local_pos->cur_pos.piece_pos[*enpass_in_cur_pos] -= pawn_data[local_env->pieces[*enpass_in_cur_pos]][PD_MOVE] * 3;
}

bool is_check_local(short_pieces_env *local_env, position *pos, bool white_king) {
	long long king_place, pc_att;
	//unsigned char
	int a, b;
	unsigned char ps_block;
	signed int block_idx;
	int i, j, ps;
	a = (!white_king ? 0 : local_env->white_pieces);
	b = (!white_king ? local_env->white_pieces - 1 : local_env->men_count - 1);
	king_place = bb_places[pos->piece_pos[(white_king)?0:local_env->white_pieces]];
	for (i = a; i <= b; i++)
		if (!((ps = pos->piece_pos[i]) & BOX)) { // piece is not captured
			pc_att = bb_piece_attacks[local_env->pieces[i]][ps];
			if (pc_att & king_place) {
				if ((block_idx = bb_not_block_index[local_env->pieces[i]]) < 0) // cannot be blocked
					return true;
				else {
					for (j = 0; j < local_env->men_count; j++) // iterate through defending pieces
						if (!((ps_block = pos->piece_pos[j]) & BOX))
							pc_att &=
								bb_piece_not_blocks[(unsigned int)block_idx][ps]
										[adjust_enpassant_pos(ps_block, local_env->pieces[j])];
					if (pc_att & king_place) return true;
				}
			}
		}
	return false;
}

bool is_legal_position_local(short_pieces_env *local_env, position *pos, bool white_to_move) { // checks for check, should also check enpassant pawns
	unsigned char sq1, sq2, sq3;
	int pc, a, b, aopp, bopp, i, j;
	bool fnd;

	if (is_check_local(local_env, pos, !white_to_move)) return false;
	a = white_to_move ? 0 : local_env->white_pieces;
	b = white_to_move ? local_env->white_pieces - 1 : local_env->men_count - 1;
	for (j = b; j > a && local_env->pieces[j] <= WPAWN; j--) {
		if (pos->piece_pos[j] == BOX) continue;
		if ((pos->piece_pos[j] & 0x70) == pawn_data[local_env->pieces[j]][PD_MY_ENPASS_RANK]) return false;
	}
	aopp = (!white_to_move) ? 0 : local_env->white_pieces;
	bopp = (!white_to_move) ? local_env->white_pieces - 1 : local_env->men_count - 1;
	pc = (white_to_move)?BPAWN:WPAWN;
	for (j = bopp; j > aopp && local_env->pieces[j] == pc; j--) {
		if (pos->piece_pos[j] == BOX) continue;
		if ((pos->piece_pos[j] & 0x70) == pawn_data[pc][PD_MY_ENPASS_RANK]) {
			// 1) check opposite pawn
			fnd = false;
			sq1 = (pos->piece_pos[j] & 0x07) + pawn_data[1-pc][PD_ENPASS_RANK];
			for (i = b; i > a && local_env->pieces[i] <= WPAWN; i--)
				if ((sq1 & 0x70) == (pos->piece_pos[i] & 0x70) &&
						(sq1 == pos->piece_pos[i]+1 || sq1+1 == pos->piece_pos[i])) {
					fnd = true;
					break;
				}
			if (!fnd) return false;
			sq1 = pos->piece_pos[j] + pawn_data[pc][PD_MOVE]; // e2
			sq2 = sq1 + pawn_data[pc][PD_MOVE];               // e3
			sq3 = sq2 + pawn_data[pc][PD_MOVE]; // maybe this is not needed // e4
			// 2) check board emptyness. There is no need to adjust enpassants
			for (i = 0; i < local_env->men_count; i++) {
				pc = pos->piece_pos[i];
				if (pc == sq1 || pc == sq2 || pc == sq3) return false;
			}
			// 3) was king in check?
			sq2 = pos->piece_pos[j];
			pos->piece_pos[j] = sq1;
			fnd = is_check_local(local_env, pos, white_to_move);
			pos->piece_pos[j] = sq2;
			if (fnd) return false;
		}
	}
	return true;
}

bool probe_forward_move(short_pieces_env *local_env, color_position *local_pos, char *search_results, int capture, unsigned char promote_pc, int table_type);

// if func = NULL, then only generate and return result about moves existence
bool gen_forward_moves_all_local(short_pieces_env *local_env, color_position *local_pos, unsigned char *local_board,
	char *search_results, int table_type, forward_move_func_local *func) {
	int i, j, j0, n, k, p, a, b, pc;
	unsigned char p0;
	signed int v, vh;
	bool capture;
	bool result = false;
	a = (local_pos->cur_wtm ? 0 : local_env->white_pieces);
	b = (local_pos->cur_wtm ? local_env->white_pieces - 1 : local_env->men_count - 1);
	int enpass_in_cur_pos;
	determine_enpass(&enpass_in_cur_pos, local_env, local_pos);
	for (i = a; i <= b; i++) {
		if (local_pos->cur_pos.piece_pos[i] == BOX)
			continue;
		j = j0 = local_pos->cur_pos.piece_pos[i];
		pc = local_env->pieces[i];

		if (pc == WPAWN || pc == BPAWN) { // adjust enpassant, gen moves for pawns
			// flag, that it's pawn move
			if (func == probe_forward_move)
				((int *)search_results)[4] = 1;
			j = adjust_enpassant_pos(j, pc);
			v = pawn_data[pc][PD_MOVE];
			if (local_board[n = j+v] == EMPTY) { // can move forward 1 step
				local_pos->cur_pos.piece_pos[i] = n;
				change_enpass(&enpass_in_cur_pos, local_env, local_pos);
				if (!is_check_local(local_env, &local_pos->cur_pos, local_pos->cur_wtm)) {
					if (!func) {
						local_pos->cur_pos.piece_pos[i] = j0;
						return_to_enpass(&enpass_in_cur_pos, local_env, local_pos);
						return true;
					}
					if ((n & 0x70) != pawn_data[pc][PD_PROMO_RANK]) {
						if (func && (!func(local_env, local_pos, search_results, EMPTY, EMPTY, table_type) || search_results[0])) {
							local_pos->cur_pos.piece_pos[i] = j0;
							return_to_enpass(&enpass_in_cur_pos, local_env, local_pos);
							return search_results[0];
						}
					}
					else {
						for (unsigned char promote_pc = KNIGHT; promote_pc <= QUEEN; promote_pc++) {
							if (promote_pc != KING) {
								if (func && (!func(local_env, local_pos, search_results, EMPTY, promote_pc, table_type) || search_results[0])) {
									local_pos->cur_pos.piece_pos[i] = j0;
									return_to_enpass(&enpass_in_cur_pos, local_env, local_pos);
									return search_results[0];
								}
							}
						}
					}
					result = true;
				}
				if ((j & 0x70) == pawn_data[pc][PD_FIRST_RANK] && local_board[n += v] == EMPTY) { // can move forward 2 steps
					local_pos->cur_pos.piece_pos[i] = n;
					if (!is_check_local(local_env, &local_pos->cur_pos, local_pos->cur_wtm)) {
						if (!func) {
							return_to_enpass(&enpass_in_cur_pos, local_env, local_pos);
							local_pos->cur_pos.piece_pos[i] = j0; // put the piece back
							return true;
						}
						if ((local_board[n-1] != EMPTY && local_env->pieces[local_board[n-1]] == pawn_data[pc][PD_ENEMY]) ||
							(local_board[n+1] != EMPTY && local_env->pieces[local_board[n+1]] == pawn_data[pc][PD_ENEMY]))
							local_pos->cur_pos.piece_pos[i] = n - pawn_data[pc][PD_MOVE] * 3;
						if (func && (!func(local_env, local_pos, search_results, EMPTY, EMPTY, table_type) || search_results[0])) {
							local_pos->cur_pos.piece_pos[i] = j0;
							return_to_enpass(&enpass_in_cur_pos, local_env, local_pos);
							return search_results[0];
						}
						result = true;
					}
				}
				return_to_enpass(&enpass_in_cur_pos, local_env, local_pos);
			}
			// check for enpassant captures
			if ((j & 0x70) == pawn_data[pc][PD_ENPASS_RANK]) {
				for (vh = -1; vh <= 1; vh += 2) {
					n = j+v+vh;
					if (n & 0x88) continue;
					if ((k=local_board[j+vh]) != EMPTY
							&& local_env->pieces[k] == pawn_data[pc][PD_ENEMY]
							&& (local_pos->cur_pos.piece_pos[k] & 0x70) == pawn_data[pc][PD_ENEMY_ENPASS_RANK]) {
						local_pos->cur_pos.piece_pos[i] = n;
						p0 = local_pos->cur_pos.piece_pos[k];
						local_pos->cur_pos.piece_pos[k] = BOX;
						if (!is_check_local(local_env, &local_pos->cur_pos, local_pos->cur_wtm)) {
							if (!func) {
								local_pos->cur_pos.piece_pos[i] = j0; // put the piece back
								local_pos->cur_pos.piece_pos[k] = p0;
								return true;
							}
							if (func && (!func(local_env, local_pos, search_results, k, EMPTY, table_type) || search_results[0])) {
								local_pos->cur_pos.piece_pos[i] = j0;
								local_pos->cur_pos.piece_pos[k] = p0;
								return search_results[0];
							}
							result = true;
						}
						local_pos->cur_pos.piece_pos[k] = p0;
					}
				}
			}
			// normal captures will be processed further
			// unset pawn move flag
			if (func == probe_forward_move)
				((int *)search_results)[4] = 0;
		}

		// proceed with normal pieces, for pawns only captures are generated
		// we do not update board[] here, since it's not used
		//board[j] = 0; // EMPTY
		k = vec_start[pc];
		change_enpass(&enpass_in_cur_pos, local_env, local_pos);
		while ((v = board_vec[k++])) {
			n = j + v;
			capture = false;
			while (!(n & 0x88)) {
				if ((p = local_board[n]) != EMPTY) { // capture?
					if ((p<local_env->white_pieces) == local_pos->cur_wtm) break; // the same color
#ifdef DEBUG
					if (p == 0 || p == local_env->white_pieces)
						printf("gen_forward_moves_all from illegal position %llx! king is captured, pc.idx = %d, org.pos = %.2x.\n", local_pos->cur_pos.as_number, i, j);
#endif
					p0 = local_pos->cur_pos.piece_pos[p]; // we cannot simply use 'n' here because of enpassant pawns
					local_pos->cur_pos.piece_pos[p] = BOX;
					capture = true;
				}
				else if (pc <= WPAWN) break; // a pawn must capture
				local_pos->cur_pos.piece_pos[i] = n;
				// we do not update board[] here, since it's not used
				if (!is_check_local(local_env, &local_pos->cur_pos, local_pos->cur_wtm)) { // legal position
					if (!func) {
						local_pos->cur_pos.piece_pos[i] = j0;
						if (capture) local_pos->cur_pos.piece_pos[p] = p0;
						return_to_enpass(&enpass_in_cur_pos, local_env, local_pos);
						return true;
					}
					if (pc == WPAWN || pc == BPAWN) {
						if ((n & 0x70) != pawn_data[pc][PD_PROMO_RANK]) {
							if (func && (!func(local_env, local_pos, search_results, p, EMPTY, table_type) || search_results[0])) {
								local_pos->cur_pos.piece_pos[i] = j0;
								if (capture) local_pos->cur_pos.piece_pos[p] = p0;
								return_to_enpass(&enpass_in_cur_pos, local_env, local_pos);
								return search_results[0];
							}
						}
						else {
							for (unsigned char promote_pc = KNIGHT; promote_pc <= QUEEN; promote_pc++) {
								if (promote_pc != KING) {
									if (func && (!func(local_env, local_pos, search_results, p, promote_pc, table_type) || search_results[0])) {
										local_pos->cur_pos.piece_pos[i] = j0;
										if (capture) local_pos->cur_pos.piece_pos[p] = p0;
										return_to_enpass(&enpass_in_cur_pos, local_env, local_pos);
										return search_results[0];
									}
								}
							}
						}
					}
					else {
						if (func && (!func(local_env, local_pos, search_results, p, EMPTY, table_type) || search_results[0])) {
							local_pos->cur_pos.piece_pos[i] = j0;
							if (capture) local_pos->cur_pos.piece_pos[p] = p0;
							return_to_enpass(&enpass_in_cur_pos, local_env, local_pos);
							return search_results[0];
						}
					}
					result = true;
				}                                                        

				if (capture) {
					local_pos->cur_pos.piece_pos[p] = p0;
					break;
				}
				if (pc <= KING) break;
				n += v;
			}
		}
		return_to_enpass(&enpass_in_cur_pos, local_env, local_pos);
		local_pos->cur_pos.piece_pos[i] = j0; // put the piece back
	}
	return (func ? true : result);
}

bool gen_forward_moves_check_local(short_pieces_env *local_env, color_position *local_pos, unsigned char *local_board,
	char *search_results, forward_move_simple_func_local *func) {
	int i, j, j0, n, k, p, a, b, pc;
	unsigned char p0;
	signed int v, vh;
	bool capture;
	a = (local_pos->cur_wtm ? 0 : local_env->white_pieces);
	b = (local_pos->cur_wtm ? local_env->white_pieces - 1 : local_env->men_count - 1);
	int enpass_in_cur_pos;
	determine_enpass(&enpass_in_cur_pos, local_env, local_pos);
	for (i = a; i <= b; i++) {
		j = j0 = local_pos->cur_pos.piece_pos[i];
		pc = local_env->pieces[i];

		if (pc == WPAWN || pc == BPAWN) { // adjust enpassant, gen moves for pawns
			j = adjust_enpassant_pos(j, pc);
			v = pawn_data[pc][PD_MOVE];
			if (local_board[n = j+v] == EMPTY) { // can move forward 1 step
				local_pos->cur_pos.piece_pos[i] = n;
				change_enpass(&enpass_in_cur_pos, local_env, local_pos);
				if (!is_check_local(local_env, &local_pos->cur_pos, local_pos->cur_wtm)) {
					if ((n & 0x70) != pawn_data[pc][PD_PROMO_RANK]) {
						if (is_check_local(local_env, &local_pos->cur_pos, !local_pos->cur_wtm)
							&& (!func(local_env, local_pos, search_results) || search_results[0])) {
							local_pos->cur_pos.piece_pos[i] = j0;
							return_to_enpass(&enpass_in_cur_pos, local_env, local_pos);
							return search_results[0];
						}
					}
					else {
						for (unsigned char promote_pc = KNIGHT; promote_pc <= QUEEN; promote_pc++)
							if (promote_pc != KING) {
								local_env->pieces[i] = promote_pc;
								if (is_check_local(local_env, &local_pos->cur_pos, !local_pos->cur_wtm) && 
									(!func(local_env, local_pos, search_results) || search_results[0])) {
									local_env->pieces[i] = pc;
									local_pos->cur_pos.piece_pos[i] = j0;
									return_to_enpass(&enpass_in_cur_pos, local_env, local_pos);
									return search_results[0];
								}
								local_env->pieces[i] = pc;
							}
					}
				}
				if ((j & 0x70) == pawn_data[pc][PD_FIRST_RANK] && local_board[n += v] == EMPTY) { // can move forward 2 steps
					local_pos->cur_pos.piece_pos[i] = n;
					if (is_check_local(local_env, &local_pos->cur_pos, !local_pos->cur_wtm) &&
						!is_check_local(local_env, &local_pos->cur_pos, local_pos->cur_wtm)) {
						if ((local_board[n-1] != EMPTY && local_env->pieces[local_board[n-1]] == pawn_data[pc][PD_ENEMY]) ||
							(local_board[n+1] != EMPTY && local_env->pieces[local_board[n+1]] == pawn_data[pc][PD_ENEMY]))
							local_pos->cur_pos.piece_pos[i] = n - pawn_data[pc][PD_MOVE] * 3;
						if (!func(local_env, local_pos, search_results) || search_results[0]) {
							local_pos->cur_pos.piece_pos[i] = j0;
							return_to_enpass(&enpass_in_cur_pos, local_env, local_pos);
							return search_results[0];
						}
					}
				}
				return_to_enpass(&enpass_in_cur_pos, local_env, local_pos);
			}
			// check for enpassant captures
			if ((j & 0x70) == pawn_data[pc][PD_ENPASS_RANK]) {
				for (vh = -1; vh <= 1; vh += 2) {
					n = j+v+vh;
					if (n & 0x88) continue;
					if ((k=local_board[j+vh]) != EMPTY
							&& local_env->pieces[k] == pawn_data[pc][PD_ENEMY]
							&& (local_pos->cur_pos.piece_pos[k] & 0x70) == pawn_data[pc][PD_ENEMY_ENPASS_RANK]) {
						local_pos->cur_pos.piece_pos[i] = n;
						p0 = local_pos->cur_pos.piece_pos[k];
						local_pos->cur_pos.piece_pos[k] = BOX;
						if (is_check_local(local_env, &local_pos->cur_pos, !local_pos->cur_wtm) &&
							!is_check_local(local_env, &local_pos->cur_pos, local_pos->cur_wtm)) {
							if (!func(local_env, local_pos, search_results) || search_results[0]) {
								local_pos->cur_pos.piece_pos[i] = j0;
								local_pos->cur_pos.piece_pos[k] = p0;
								return search_results[0];
							}
						}
						local_pos->cur_pos.piece_pos[k] = p0;
					}
				}
			}
			// normal captures will be processed further
		}

		// proceed with normal pieces, for pawns only captures are generated
		// we do not update board[] here, since it's not used
		//board[j] = 0; // EMPTY
		k = vec_start[pc];
		change_enpass(&enpass_in_cur_pos, local_env, local_pos);
		while ((v = board_vec[k++])) {
			n = j + v;
			capture = false;
			while (!(n & 0x88)) {
				if ((p = local_board[n]) != EMPTY) { // capture?
					if ((p<local_env->white_pieces) == local_pos->cur_wtm) break; // the same color
#ifdef DEBUG
					if (p == 0 || p == local_env->white_pieces)
						printf("gen_forward_moves_all from illegal position %llx! king is captured, pc.idx = %d, org.pos = %.2x.\n", local_pos->cur_pos.as_number, i, j);
#endif
					p0 = local_pos->cur_pos.piece_pos[p]; // we cannot simply use 'n' here because of enpassant pawns
					local_pos->cur_pos.piece_pos[p] = BOX;
					capture = true;
				}
				else if (pc <= WPAWN) break; // a pawn must capture
				local_pos->cur_pos.piece_pos[i] = n;
				// we do not update board[] here, since it's not used
				if (!is_check_local(local_env, &local_pos->cur_pos, local_pos->cur_wtm)) { // legal position
					if (pc == WPAWN || pc == BPAWN) {
						if ((n & 0x70) != pawn_data[pc][PD_PROMO_RANK]) {
							if (is_check_local(local_env, &local_pos->cur_pos, !local_pos->cur_wtm)
								&& (!func(local_env, local_pos, search_results) || search_results[0])) {
								local_pos->cur_pos.piece_pos[i] = j0;
								if (capture) local_pos->cur_pos.piece_pos[p] = p0;
								return_to_enpass(&enpass_in_cur_pos, local_env, local_pos);
								return search_results[0];
							}
						}
						else {
							for (unsigned char promote_pc = KNIGHT; promote_pc <= QUEEN; promote_pc++) {
								if (promote_pc != KING) {
									local_env->pieces[i] = promote_pc;
									if (is_check_local(local_env, &local_pos->cur_pos, !local_pos->cur_wtm)
										&& (!func(local_env, local_pos, search_results) || search_results[0])) {
										local_env->pieces[i] = pc;
										local_pos->cur_pos.piece_pos[i] = j0;
										if (capture) local_pos->cur_pos.piece_pos[p] = p0;
										return_to_enpass(&enpass_in_cur_pos, local_env, local_pos);
										return search_results[0];
									}
									local_env->pieces[i] = pc;
								}
							}
						}
					}
					else {
						if (is_check_local(local_env, &local_pos->cur_pos, !local_pos->cur_wtm)
							&& (!func(local_env, local_pos, search_results) || search_results[0])) {
							local_pos->cur_pos.piece_pos[i] = j0;
							if (capture) local_pos->cur_pos.piece_pos[p] = p0;
							return_to_enpass(&enpass_in_cur_pos, local_env, local_pos);
							return search_results[0];
						}
					}
				}                                                        

				if (capture) {
					local_pos->cur_pos.piece_pos[p] = p0;
					break;
				}
				if (pc <= KING) break;
				n += v;
			}
		}
		return_to_enpass(&enpass_in_cur_pos, local_env, local_pos);
		local_pos->cur_pos.piece_pos[i] = j0; // put the piece back
	}
	return true;
}

// only pawn moves (not captures)
bool gen_forward_moves_pawn_local(short_pieces_env *local_env, color_position *local_pos, unsigned char *local_board,
	char *search_results, int table_type, forward_move_func_local *func) {
	int i, j, j0, n, k, p, a, b, pc;
	unsigned char p0;
	signed int v, vh;
	bool capture;
	bool result = false;
	a = (local_pos->cur_wtm ? 0 : local_env->white_pieces);
	b = (local_pos->cur_wtm ? local_env->white_pieces - 1 : local_env->men_count - 1);
	int enpass_in_cur_pos;
	determine_enpass(&enpass_in_cur_pos, local_env, local_pos);
	for (i = a; i <= b; i++) {
		j = j0 = local_pos->cur_pos.piece_pos[i];
		pc = local_env->pieces[i];

		if (pc == WPAWN || pc == BPAWN) { // adjust enpassant, gen moves for pawns
			j = adjust_enpassant_pos(j, pc);
			v = pawn_data[pc][PD_MOVE];
			if (local_board[n = j+v] == EMPTY) { // can move forward 1 step
				local_pos->cur_pos.piece_pos[i] = n;
				change_enpass(&enpass_in_cur_pos, local_env, local_pos);
				if (!is_check_local(local_env, &local_pos->cur_pos, local_pos->cur_wtm)) {
					if ((n & 0x70) != pawn_data[pc][PD_PROMO_RANK]) {
						if (!func(local_env, local_pos, search_results, EMPTY, EMPTY, table_type) || search_results[0]) {
							local_pos->cur_pos.piece_pos[i] = j0;
							return_to_enpass(&enpass_in_cur_pos, local_env, local_pos);
							return search_results[0];
						}
					}
					else {
						for (unsigned char promote_pc = KNIGHT; promote_pc <= QUEEN; promote_pc++) {
							if (promote_pc != KING) {
								if (!func(local_env, local_pos, search_results, EMPTY, promote_pc, table_type) || search_results[0]) {
									local_pos->cur_pos.piece_pos[i] = j0;
									return_to_enpass(&enpass_in_cur_pos, local_env, local_pos);
									return search_results[0];
								}
							}
						}
					}
					result = true;
				}
				if ((j & 0x70) == pawn_data[pc][PD_FIRST_RANK] && local_board[n += v] == EMPTY) { // can move forward 2 steps
					local_pos->cur_pos.piece_pos[i] = n;
					if (!is_check_local(local_env, &local_pos->cur_pos, local_pos->cur_wtm)) {
						if ((local_board[n-1] != EMPTY && local_env->pieces[local_board[n-1]] == pawn_data[pc][PD_ENEMY]) ||
							(local_board[n+1] != EMPTY && local_env->pieces[local_board[n+1]] == pawn_data[pc][PD_ENEMY]))
							local_pos->cur_pos.piece_pos[i] = n - pawn_data[pc][PD_MOVE] * 3;
						if (!func(local_env, local_pos, search_results, EMPTY, EMPTY, table_type) || search_results[0]) {
							local_pos->cur_pos.piece_pos[i] = j0;
							return_to_enpass(&enpass_in_cur_pos, local_env, local_pos);
							return search_results[0];
						}
						result = true;
					}
				}
				return_to_enpass(&enpass_in_cur_pos, local_env, local_pos);
			}
		}
		return_to_enpass(&enpass_in_cur_pos, local_env, local_pos);
		local_pos->cur_pos.piece_pos[i] = j0; // put the piece back
	}
	return true;
}

bool gen_forward_moves_capture_local(short_pieces_env *local_env, color_position *local_pos, unsigned char *local_board,
	char *search_results, int table_type, forward_move_func_local *func) {
	int i, j, j0, n, k, p, a, b, pc;
	unsigned char p0;
	signed int v, vh;
	bool capture;
	bool result = false;
	int moves_pos_count = 0;
	a = (local_pos->cur_wtm ? 0 : local_env->white_pieces);
	b = (local_pos->cur_wtm ? local_env->white_pieces - 1 : local_env->men_count - 1);
	int enpass_in_cur_pos;
	determine_enpass(&enpass_in_cur_pos, local_env, local_pos);
	for (i = a; i <= b; i++) {
		j = j0 = local_pos->cur_pos.piece_pos[i];
		pc = local_env->pieces[i];

		if (pc == WPAWN || pc == BPAWN) { // adjust enpassant, gen moves for pawns
			j = adjust_enpassant_pos(j, pc);
			v = pawn_data[pc][PD_MOVE];
			// not need to step on 1 and 2 forward
			// check for enpassant captures
			if ((j & 0x70) == pawn_data[pc][PD_ENPASS_RANK]) {
				for (vh = -1; vh <= 1; vh += 2) {
					n = j+v+vh;
					if (n & 0x88) continue;
					if ((k=local_board[j+vh]) != EMPTY
							&& local_env->pieces[k] == pawn_data[pc][PD_ENEMY]
							&& (local_pos->cur_pos.piece_pos[k] & 0x70) == pawn_data[pc][PD_ENEMY_ENPASS_RANK]) {
						local_pos->cur_pos.piece_pos[i] = n;
						p0 = local_pos->cur_pos.piece_pos[k];
						local_pos->cur_pos.piece_pos[k] = BOX;
						if (!is_check_local(local_env, &local_pos->cur_pos, local_pos->cur_wtm)) {
							if (!func(local_env, local_pos, search_results, k, EMPTY, table_type) || search_results[0]) {
								local_pos->cur_pos.piece_pos[i] = j0;
								local_pos->cur_pos.piece_pos[k] = p0;
								return search_results[0];
							}
							result = true;
						}
						local_pos->cur_pos.piece_pos[i] = j0;
						local_pos->cur_pos.piece_pos[k] = p0;
					}
				}
			}
			// normal captures will be processed further
		}

		// proceed with normal pieces, for pawns only captures are generated
		// we do not update board[] here, since it's not used
		//board[j] = 0; // EMPTY
		k = vec_start[pc];
		change_enpass(&enpass_in_cur_pos, local_env, local_pos);
		while ((v = board_vec[k++])) {
			n = j + v;
			capture = false;
			while (!(n & 0x88)) {
				if ((p = local_board[n]) != EMPTY) { // capture?
					if ((p<local_env->white_pieces) == local_pos->cur_wtm) break; // the same color
#ifdef DEBUG
					if (p == 0 || p == local_env->white_pieces)
						printf("gen_forward_moves_all from illegal position %llx! king is captured, pc.idx = %d, org.pos = %.2x.\n", local_pos->cur_pos.as_number, i, j);
#endif
					p0 = local_pos->cur_pos.piece_pos[p]; // we cannot simply use 'n' here because of enpassant pawns
					local_pos->cur_pos.piece_pos[p] = BOX;
					capture = true;
				}
				else if (pc <= WPAWN) break; // a pawn must capture
				if (capture) {
					local_pos->cur_pos.piece_pos[i] = n;
					// we do not update board[] here, since it's not used
					if (!is_check_local(local_env, &local_pos->cur_pos, local_pos->cur_wtm)) { // legal position
						if (pc == WPAWN || pc == BPAWN) {
							if ((n & 0x70) != pawn_data[pc][PD_PROMO_RANK]) {
								if (!func(local_env, local_pos, search_results, p, EMPTY, table_type) || search_results[0]) {
									local_pos->cur_pos.piece_pos[i] = j0;
									local_pos->cur_pos.piece_pos[p] = p0;
									return_to_enpass(&enpass_in_cur_pos, local_env, local_pos);
									return search_results[0];
								}
							}
							else {
								for (unsigned char promote_pc = KNIGHT; promote_pc <= QUEEN; promote_pc++) {
									if (promote_pc != KING) {
										if (!func(local_env, local_pos, search_results, p, promote_pc, table_type) || search_results[0]) {
											local_pos->cur_pos.piece_pos[i] = j0;
											local_pos->cur_pos.piece_pos[p] = p0;
											return_to_enpass(&enpass_in_cur_pos, local_env, local_pos);
											return search_results[0];
										}
									}
								}
							}
						}
						else {
							if (!func(local_env, local_pos, search_results, p, EMPTY, table_type) || search_results[0]) {
								local_pos->cur_pos.piece_pos[i] = j0;
								local_pos->cur_pos.piece_pos[p] = p0;
								return_to_enpass(&enpass_in_cur_pos, local_env, local_pos);
								return search_results[0];
							}
						}
						result = true;
					}
					local_pos->cur_pos.piece_pos[p] = p0;
					break;
				}
				if (pc <= KING) break;
				n += v;
			}
		}
		return_to_enpass(&enpass_in_cur_pos, local_env, local_pos);
		local_pos->cur_pos.piece_pos[i] = j0; // put the piece back
	}
	return true;
}

// It called for necessary positions and probe WDL
bool probe_forward_capture_local(short_pieces_env *local_env, color_position *local_pos, char *search_results, int capture, unsigned char promote_pc, int table_type) {
	short_pieces_env minor_env;
	unsigned char pcs[8];
	minor_env.pieces = pcs;
	color_position minor_pos;
	position_to_minor_env(local_env, local_pos, &minor_env, &minor_pos, capture, promote_pc);
	if (minor_env.men_count == 2) {
		search_results[1] = true;
		return true;
	}
	int eval = 0;
	int probe_result = PROBE_NO_TABLE;
	unsigned long minor_index = get_table_index_local(minor_pos.cur_wtm, &minor_env);
	probe_result = get_value_from_load_position_local(&eval, table_type, &minor_env, &minor_pos, minor_index);
	if (probe_result != PROBE_OK || eval > 1 || eval < -1) {
		char name[80];
		get_tb_name_local(name, &minor_env);
		printf("Error probing minor position %llx with wtm = %d from %s with code %d\n", minor_pos.cur_pos.as_number, minor_pos.cur_wtm, name, probe_result);
		return false;
	} else
		search_results[eval + 1] = true;
	return true;
}

// It called for necessary positions and probe PL
bool probe_forward_capture_pl_local(short_pieces_env *local_env, color_position *local_pos, char *search_results, int capture, unsigned char promote_pc, int table_type) {
	short_pieces_env minor_env;
	unsigned char pcs[8];
	minor_env.pieces = pcs;
	color_position minor_pos;
	position_to_minor_env(local_env, local_pos, &minor_env, &minor_pos, capture, promote_pc);
	if (minor_env.men_count == 2)
		return true;
	int eval = REJECT_SOME(search_results[1]);
	int probe_result = PROBE_NO_TABLE;
	unsigned long minor_index = get_table_index_local(minor_pos.cur_wtm, &minor_env);
	tbfile_entry *tbe = (tbfile_entry *)&search_results[2];
	probe_result = get_value_from_load_position_local(&eval, table_type, &minor_env, &minor_pos, minor_index);
	if (probe_result != PROBE_OK) {
		char name[80];
		get_tb_name_local(name, &minor_env);
		printf("Error probing minor position %llx with wtm = %d from %s with code %d\n", minor_pos.cur_pos.as_number, minor_pos.cur_wtm, name, probe_result);
		return false;
	} else {
		if (eval > 0 && (*tbe + eval) % 2 == 1) { //right result in position
			if (search_results[1] == 0 && *tbe < eval + 1)
				*tbe = eval + 1;
			if (search_results[1] == 1 && *tbe > eval + 1)
				*tbe = eval + 1;
		}
	}
	return true;
}

// It called for all positions and check for mate
bool probe_check_for_mate_local(short_pieces_env *local_env, color_position *local_pos, char *search_result) {
	unsigned char local_board[128];
	clear_board_local(local_board);
	put_pieces_on_board_local(local_env, local_pos, local_board);
	local_pos->cur_wtm = !local_pos->cur_wtm;
	if (!gen_forward_moves_all_local(local_env, local_pos, local_board, NULL, 0, NULL))
		*search_result = 1;
	local_pos->cur_wtm = !local_pos->cur_wtm;
	return true;
}

// It called for all positions and get value
bool probe_forward_move(short_pieces_env *local_env, color_position *local_pos, char *search_results, int capture, unsigned char promote_pc, int table_type) {
	int *search_values = (int *)search_results;
	short_pieces_env minor_env;
	unsigned char pcs[8];
	minor_env.pieces = pcs;
	color_position minor_pos;
	position_to_minor_env(local_env, local_pos, &minor_env, &minor_pos, capture, promote_pc);
	if (minor_env.men_count == 2) {
		search_values[2] = 0;
		return true;
	}
	int eval = search_values[5];
	int probe_result = PROBE_NO_TABLE;
	unsigned long minor_index = get_table_index_local(minor_pos.cur_wtm, &minor_env);
	if (capture == EMPTY && promote_pc == EMPTY && local_env->pslice_number == minor_env.pslice_number) // not minor. Probe only own color
		probe_result = get_value_from_own_color_local(&eval, table_type, &minor_env, &minor_pos, minor_index);
	else
		probe_result = get_value_from_load_position_local(&eval, table_type, &minor_env, &minor_pos, minor_index);
	if (probe_result != PROBE_OK)
		return false;
	if (DTM_TYPE(table_type)) {
		if (eval == -1) // draw
			search_values[2] = 0;
		else {
			// nulling move
			bool nulling = table_type >= DTZ50_BEGIN && (capture != EMPTY || search_values[4] == 1);
			if (eval % 2) { // win. We need in maximal
				if (nulling)
					eval = -1;
				if (search_values[3] == -2 || search_values[3] < eval)
					search_values[3] = eval;
			} else { // lose. We need minimal
				if (nulling)
					eval = 0;
				if (search_values[1] == -2 || search_values[1] > eval)
					search_values[1] = eval;
			}
		}
	} else
		search_values[eval + 2] = 0;
	return true;
}

unsigned char get_piece_local(unsigned char piece) {
	if (piece)
		return piece - 1;
	else
		return piece;
}

unsigned long get_table_index_local(bool wtm, short_pieces_env *local_env) {
	if (local_env->men_count > 7)
		return TABLE_MAX_INDEX + 1;
	unsigned char pieces_count[10] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
	for (int i = 0; i < local_env->men_count; i++) {
		if (local_env->pieces[i] == KING) continue;
		char piece = get_piece_local(local_env->pieces[i]);
		if (piece > KING - 1) piece--;
		piece = piece + 5 * (i >= local_env->white_pieces);
		pieces_count[piece]++;
	}
	unsigned long index = get_table_index_from_pieces_count(pieces_count);
	index = index * 7 + local_env->pslice_number;
	index = index * 2 + wtm;
	return index;
}

unsigned long change_color_in_index(unsigned long index) {
	bool color = index & 1;
	return ((index >> 1) << 1) | (!color);
}

cache_file_bufferizer *begin_read_table_file_cache_local(bool wtm, file_offset start_pos, file_offset length, int table_type, short_pieces_env *local_env, unsigned long local_index, char *table_path) {
	// clean hidden cache, if it's necessary
	rwlock_rdlock(hidden_locker);
	if (!memory_allocation_done && cur_hidden_size >= max_hidden_size) {
		rwlock_unlock(hidden_locker);
		allocate_cache_memory();
		rwlock_rdlock(hidden_locker);
	}
	if (cur_hidden_size >= max_hidden_size) {
		rwlock_unlock(hidden_locker);
		clean_hidden_cache();
	} else
		rwlock_unlock(hidden_locker);

	cache_file_bufferizer *filebuf = NULL;
	char tbname[MAX_PATH];
	unordered_map<unsigned long, bufferizer_list<cache_file_bufferizer> *> *bufferizers;
	bufferizers = &cache_file_bufferizers[table_type];
	unordered_map<unsigned long, bufferizer_list<cache_file_bufferizer> *>::iterator it;

	rwlock_rdlock(cache_file_buf_locker);
	if (caching_file_bufferizers && (it = bufferizers->find(local_index)) != bufferizers->end() && it->second != NULL) {
		bufferizer_list<cache_file_bufferizer> *buf_list = it->second;
		mutex_lock(cache_bufferizer_mutexer);
		while (buf_list != NULL) {
			if (buf_list->bufferizer->trylock()) {
				mutex_unlock(cache_bufferizer_mutexer);
				buf_list->requests++;
				rwlock_rdlock(requests_locker);
				if (buf_list->requests > max_requests) {
					rwlock_unlock(requests_locker);
					rwlock_wrlock(requests_locker);
					max_requests = buf_list->requests;
				}
				rwlock_unlock(requests_locker);
				filebuf = buf_list->bufferizer;
				if (filebuf && start_pos != FB_NO_SEEK)
					filebuf->seek(start_pos);
				break;
			}
			buf_list = buf_list->next;
		}
		if (filebuf == NULL)
			mutex_unlock(cache_bufferizer_mutexer);
	}
	rwlock_unlock(cache_file_buf_locker);
	if (filebuf == NULL) {
		get_output_tb_filename_local(tbname, wtm ? tt_to_ft_map[table_type] : (tt_to_ft_map[table_type] + 1), local_env, table_path);
		filebuf = new cache_file_bufferizer(local_index, table_type);
		if (!filebuf->begin_read(tbname, 0, FB_TO_THE_END)) {
			delete filebuf;
			filebuf = NULL;
		}
 		if (filebuf) {
			filebuf->trylock();
			if (start_pos != FB_NO_SEEK)
				filebuf->seek(start_pos);
			else
				filebuf->seek(0);
			if (caching_file_bufferizers) {
				rwlock_wrlock(cache_file_buf_locker);
				if ((it = bufferizers->find(local_index)) != bufferizers->end()) {
					it->second->add(filebuf, 0, NULL, 1);
				}
				else {
					bufferizer_list<cache_file_bufferizer> *buf_list = new bufferizer_list<cache_file_bufferizer>(filebuf, 0, NULL, 1);
					bufferizers->insert(make_pair(local_index, buf_list));
				}
				rwlock_unlock(cache_file_buf_locker);
				rwlock_wrlock(hidden_locker);
				cur_hidden_size += filebuf->get_size();
#ifdef LOG_HIDDEN
				log_cur_hidden_size("filebuf", filebuf->get_size());
#endif
				rwlock_unlock(hidden_locker);
			}
		}
	}
	return filebuf;
}

void adjust_indexer_local(short_pieces_env *local_env, bool perm_indexer) {
	int i;

	// todo: stack indexers
	if (local_env->indexer) delete local_env->indexer; // no mem.leak any more
	for (i = 0; i < local_env->men_count; i++)
		if (local_env->pieces[i] <= WPAWN) {
			if (perm_indexer)
				local_env->indexer = new pawn_mutable_indexer(local_env->men_count, local_env->white_pieces, local_env->pslice_number, local_env->pieces);
#ifdef LOMONOSOV_FULL
			else
				local_env->indexer = new pawn_king_opt_indexer(local_env->men_count, local_env->white_pieces, local_env->pslice_number, local_env->pieces);
#endif
			return;
		}
	if (perm_indexer)
		local_env->indexer = new mutable_indexer(local_env->men_count, local_env->white_pieces, local_env->pslice_number, local_env->pieces);
#ifdef LOMONOSOV_FULL
	else
		local_env->indexer = new simple_king_opt_indexer(local_env->men_count, local_env->white_pieces, local_env->pslice_number, local_env->pieces);
#endif
}

// todo: support enpassant pawns 7K/8/8/8/5pPk/8/3RP3/8 b - g3 0 1
bool load_fen_local(const char *s, bool *invert_pieces, short_pieces_env *local_env, color_position *local_pos) {
	int i, j;
	int x, y, left;
	char tbname[80];
	bool found;

	for (i = 0; i < local_env->men_count; i++)
		local_pos->cur_pos.piece_pos[i] = 0xff;
	get_tb_name_without_slice_local(tbname, local_env); // pieces here
	if (*invert_pieces)
		for (i = local_env->white_pieces; i < local_env->men_count; i++) tbname[i] &= 0xdf; // uppercase
	else
		for (i = 0; i < local_env->white_pieces; i++) tbname[i] &= 0xdf; // uppercase
	y = 7;
	x = 0;
	i = 0;
	left = local_env->men_count;
	while (s[i] == ' ') i++;
	for (; y >= 0 && s[i]; i++) {
		if (s[i] == '/') {
			y--;
			x = 0;
			continue;
		}
		if (s[i] >= '1' && s[i] <= '9') {
			x += s[i]-'0';
			if (x > 8) return false;
			continue;
		}
		if (s[i] == ' ' && y == 0) break;
		found = false;
		for (j = 0; j < local_env->men_count; j++)
			if (s[i] == tbname[j] && local_pos->cur_pos.piece_pos[j] == 0xff) {
				found = true;
				local_pos->cur_pos.piece_pos[j] = (y << 4) | x;
				x++;
				break;
			}
		if (!found) return false;
		left--;
	}
	if (left || s[i] != ' ') return false;
	i++;
	if (s[i] == 'w') local_pos->cur_wtm = true; else
		if (s[i] == 'b') local_pos->cur_wtm = false; else return false;
	i++; if (s[i] != ' ') return false;
	i++; if (s[i] != '-') return false; // castlings
	i++; if (s[i] != ' ') return false;
	i++; if (s[i] != '-') { // enpassant
		if (s[i] >= 'a' && s[i] <= 'h' && s[i+1] >= '3' && s[i+1] <= '6') {
			j = ((s[i+1]-'1') << 4) + (s[i] - 'a');
			bool found = false;
			if (local_pos->cur_wtm) {
				j -= 16; if (j < 64 || j > 71) return false;
				int start = (*invert_pieces) ? 0 : local_env->white_pieces;
				int end = (*invert_pieces) ? local_env->white_pieces : local_env->men_count;
				unsigned char pc = (*invert_pieces) ? WPAWN : BPAWN;
				for (x = start; x < end; x++)
					if (local_env->pieces[x] == pc && local_pos->cur_pos.piece_pos[x] == j) {
						local_pos->cur_pos.piece_pos[x] = j + 48;
						found = true;
						break;
					}
			} else {
				j += 16; if (j < 48 || j > 55) return false;
				int start = (*invert_pieces) ? local_env->white_pieces : 0;
				int end = (*invert_pieces) ? local_env->men_count : local_env->white_pieces;
				unsigned char pc = (*invert_pieces) ? BPAWN : WPAWN;
				for (x = start; x < end; x++)
					if (local_env->pieces[x] == pc && local_pos->cur_pos.piece_pos[x] == j) {
						local_pos->cur_pos.piece_pos[x] = j - 48;
						found = true;
						break;
					}
			}
			if (!found) return false;
		} else return false;
	}
	if (has_full_color_symmetry_local(local_env) && !local_pos->cur_wtm) {
		*invert_pieces = true;
		for (i = 0; i < local_env->white_pieces; ++i) {
			j = local_pos->cur_pos.piece_pos[i];
			local_pos->cur_pos.piece_pos[i] = local_pos->cur_pos.piece_pos[i + local_env->white_pieces];
			local_pos->cur_pos.piece_pos[i + local_env->white_pieces] = j;
		}
	}
	return true;
}

bool load_pieces_from_fen_local(const char *s, bool *ztb_invert_color, short_pieces_env *local_env) {
	unsigned int i, wc, bc;
	char white[MAX_MEN+1], black[MAX_MEN], tbname[10];
	int map[MAX_MEN], possible_slice_num, black_slice_num, y;
	
	local_env->men_count = wc = bc = 0;
	possible_slice_num = black_slice_num = 0;
	y = 7;
	for (i = 0; s[i]; i++) {
		if (s[i] == ' ') break;
		if ((s[i] >= '0' && s[i] <= '9') || s[i] == '/') {
			if (s[i] == '/') y--;
			continue;
		}
		if (s[i] == 'P' && !possible_slice_num) possible_slice_num = y;
		if (s[i] == 'p') black_slice_num = y;
		if (s[i] <= 'Z') white[wc++] = s[i]; // capital
		else black[bc++] = s[i];
		if (bc+wc > MAX_MEN || bc > MAX_MEN - 1 || wc > MAX_MEN - 1) {
			local_env->men_count = 8;
			return false;
		}
	}
	if (bc < 1 || wc < 1) return false;
	local_env->white_pieces = wc;
	for (i = 0; i < bc; i++) white[wc++] = black[i];
	white[wc] = '\0';
	for (i = 0; i < wc; i++)
		switch(white[i]) {
		case 'P': local_env->pieces[i] = WPAWN; break;
		case 'p': local_env->pieces[i] = BPAWN; break;
		case 'Q': case 'q': local_env->pieces[i] = QUEEN; break;
		case 'K': case 'k': local_env->pieces[i] = KING; break;
		case 'R': case 'r': local_env->pieces[i] = ROOK; break;
		case 'B': case 'b': local_env->pieces[i] = BISHOP; break;
		case 'N': case 'n': local_env->pieces[i] = KNIGHT; break;
		default: return false;
		}

	local_env->men_count = wc;
	// pieces_to_canonical reorders pieces and inverts pawn colors (in case of invert_color)
	pieces_to_canonical(local_env->pieces, local_env->men_count, &local_env->white_pieces, map, ztb_invert_color);
	// positions without KING
	if (local_env->pieces[0] != KING || local_env->pieces[local_env->white_pieces] != KING) return false;
	if ((*ztb_invert_color) && black_slice_num) possible_slice_num = 7 - black_slice_num;
	if (possible_slice_num) local_env->pslice_number = possible_slice_num;
	else local_env->pslice_number = 0;
	
	local_env->full_color_symmetry = has_full_color_symmetry_local(local_env);
	return true;
}

void form_fen_local(position *pos, bool wtm, char *fen, short_pieces_env *local_env) {
	char tbname[80], board[64];
	int i, j, ep, sq, empty, x, y;

	get_tb_name_without_slice_local(tbname, local_env);
	for (i = 0; i < 64; i++) board[i] = 0;
	ep = -1;
	for (i = 0; i < local_env->men_count; i++) {
		sq = pos->piece_pos[i];
		sq = ((sq >> 4) << 3) | (sq & 0x7);
		if (local_env->pieces[i] == WPAWN && sq < 8) {
			sq += 24;
			ep = sq - 8;
		} else if (local_env->pieces[i] == BPAWN && sq > 55) {
			sq -= 24;
			ep = sq + 8;
		}

		if (i < local_env->white_pieces) board[sq] = tbname[i] & 0xdf; else board[sq] = tbname[i];
	}
	j = 0;
	for (y = 7; y >= 0; y--) {
		empty = 0;
		for (x = 0; x < 8; x++) {
			sq = (y<<3) | x;
			if (board[sq]) {
				if (empty) {
					fen[j++] = empty + '0';
					empty = 0;
				}
				fen[j++] = board[sq];
			} else empty++;
		}
		if (empty)
			fen[j++] = empty + '0';
		if (y)
			fen[j++] = '/';
	}
	fen[j++] = ' ';
	fen[j++] = wtm ? 'w' : 'b';
	fen[j++] = ' ';
	fen[j++] = '-'; // castlings
	fen[j++] = ' ';
	if (ep != -1) {
		fen[j++] = (ep & 0x7) + 'a';
		fen[j++] = (ep >> 3) + '1';
	} else {
		fen[j++] = '-';
	}
	fen[j] = '\0';
}

/*
	piCount - array 0..9 of current piece count:
	wp, wn, wb, wr, wq, bp, bn, bb, br, bq

	psq* structure - positions of pieces in 0..63 format:
	0..5 - pawns positions
	6..11 - knights positions
	12..17 - bishops positions
	18..23 - rooks positions
	24..29 - queens positions
	30 - king position

	sqEnP - position of En passant square (16..23 or 40..47)
*/

bool load_lomonosov_tb_position_local(int side, unsigned int *psqW, unsigned int *psqB, int *piCount, int sqEnP, 
	bool *ztb_invert_color, short_pieces_env *local_env, color_position *local_pos) {
	int i, j, index;
	bool invert_color = false, f_color_symmetry = false;
	int possible_slice_num = 0, black_slice_num = 7, piece_y;
	int spieces[] = {WPAWN, KNIGHT, BISHOP, ROOK, QUEEN, KING};

	local_env->men_count = sum_array(piCount, 10) + 2; // pieces + two kings
	if (local_env->men_count > 8)
		return false;
	local_env->white_pieces = sum_array(piCount, 5) + 1; // pieces + king
	if (local_env->men_count < 2 || local_env->men_count > MAX_MEN  || local_env->white_pieces >= local_env->men_count) return false;

	// check, is revert needed?
	if (piCount[0] == 0 && piCount[5] != 0) invert_color = true; // slicing by white pawn only
	else if (piCount[0] != 0 && piCount[5] == 0) invert_color = false; // do not invert
	else { // majority is white
		if (local_env->white_pieces < ((local_env->men_count + 1) >> 1)) 
			invert_color = true; 
		else if (local_env->white_pieces == (local_env->men_count >> 1))
			for (i = 4; i >= 0; --i) {
				if (piCount[i] < piCount[i+5]) { 
					invert_color = true;
					break;
				}
				if (piCount[i] > piCount[i+5]) {
					invert_color = false;
					break;
				}
			}
	}
	if (side) { // side != 0 => btm
		// check for full_color_symmetry
		f_color_symmetry = (local_env->white_pieces == local_env->men_count - local_env->white_pieces) && !piCount[0] && !piCount[5];
		if (f_color_symmetry)
			for (i = 0; i < 5; ++i)
				if (piCount[i] != piCount[i + 5]) {
					f_color_symmetry = false;
					break;
				}
		if (f_color_symmetry) invert_color = true; 
	}

	// load black pieces
	index = invert_color ? 0 : local_env->white_pieces;
	local_env->pieces[index] = KING;
	local_pos->cur_pos.piece_pos[index] = sq64_to_lomonosov(psqB[KING_INDEX]);
	++index;
	for (i = 4; i >= 0; --i) {
		for (j = 0; j < piCount[i + 5]; ++j) {
			if (i == 0) { 
				local_env->pieces[index] = invert_color ? WPAWN : BPAWN; // pawn 
				piece_y = psqB[i * C_PIECES + j] >> 3;
				if (black_slice_num > piece_y) black_slice_num = piece_y;
			} else local_env->pieces[index] = spieces[i];
			local_pos->cur_pos.piece_pos[index] = sq64_to_lomonosov(psqB[i * C_PIECES + j]);
			++index;
		}			
	}
	// load white pieces
	index = invert_color ? index : 0;
	local_env->pieces[index] = KING;
	local_pos->cur_pos.piece_pos[index] = sq64_to_lomonosov(psqW[KING_INDEX]);
	++index;
	for (i = 4; i >= 0; --i) {
		for (j = 0; j < piCount[i]; ++j) {
			if (i == 0) {
				local_env->pieces[index] = invert_color ? BPAWN : WPAWN; // pawn
				piece_y = psqW[i * C_PIECES + j] >> 3;
				if (possible_slice_num < piece_y) possible_slice_num = piece_y;
			} else local_env->pieces[index] = spieces[i];
			local_pos->cur_pos.piece_pos[index] = sq64_to_lomonosov(psqW[i * C_PIECES + j]);
			++index;
		}			
	}
	
	if (invert_color) local_env->white_pieces = local_env->men_count - local_env->white_pieces;
	local_env->pslice_number = invert_color ? 7 - black_slice_num : possible_slice_num;
	local_pos->cur_wtm = (side == 0);

	if (0 < sqEnP && sqEnP < 64) { // enpassant
		if (!local_pos->cur_wtm) sqEnP += 8;
		else sqEnP -= 8;
		sqEnP = sq64_to_lomonosov(sqEnP);
		unsigned char w_pawn = invert_color ? BPAWN : WPAWN;
		unsigned char b_pawn = invert_color ? WPAWN : BPAWN;
		int found = -1;
		for (i = 0; i < local_env->men_count; ++i)
			if (local_pos->cur_pos.piece_pos[i] == sqEnP)
				if (local_env->pieces[i] == w_pawn) {
					local_pos->cur_pos.piece_pos[i] -= 48;
					found = WPAWN;
					break;
				} else if (local_env->pieces[i] == b_pawn) {
					local_pos->cur_pos.piece_pos[i] += 48;
					found = BPAWN;
					break;
				}
		if (found < 0 || found != (local_pos->cur_wtm ? WPAWN : BPAWN)) return false;
	}
		
	local_env->full_color_symmetry = f_color_symmetry;
	*ztb_invert_color = invert_color;
	return true;
}

void set_cur_table_not_exist_local(unsigned long index, int table_type) {
	if (index <= TABLE_MAX_INDEX) {
		set_bit_by_index(not_exist_tables[table_type], index, 1);
	}
}

int get_cur_table_not_exist_local(unsigned long index, int table_type) {
	if (index <= TABLE_MAX_INDEX) {
		return get_bit_by_index(not_exist_tables[table_type], index);
	}
	else
		return 1;
}

void init_types_vector() {
	for (int table_type = MIN_TYPE; table_type <= MAX_TYPE; table_type++) {
		types_vector[table_type].clear();
		types_vector[table_type].push_back(table_type);
	}
	// DL type - order: TL, DL
	for (int table_type = DL; table_type <= MAX_TYPE; table_type += DTZ50_BEGIN) {
		types_vector[table_type].insert(types_vector[table_type].begin(), TL + DTZ50_BEGIN * (table_type >= DTZ50_BEGIN));
	}
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

int get_standard_eval(int table_type) {
	if (DTM_TYPE(table_type))
		return -1;
	else
		return 0;
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
		if (*eval == 0) //mate
			*eval = -1;
		else
			*eval = get_position_sign(*eval) * (*eval);
		break;
	case ZML: case ZPL:
		if (*eval == 0) //mate
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

#ifdef LOG_MEMORY

void log_memory() {
	bool log = false;
	unsigned long long printed_hits;
	rwlock_wrlock(hidden_locker);
	if (logging_memory) {
		if (tbhits <= 10000 && tbhits % 1000 == 0)
			log = true;
		if (tbhits <= 100000 && tbhits % 5000 == 0)
			log = true;
		if (tbhits % 50000 == 0)
			log = true;
		printed_hits = tbhits;
	}
	rwlock_unlock(hidden_locker);
	if (log) {
		rwlock_wrlock(log_locker);
		char log_filename[MAX_PATH];
		sprintf(log_filename, "%s%s", path_to_logs, "memory_log.log");
		FILE *f = fopen(log_filename, "a");

		if (!f) {
			int last_error = GetLastError();
			printf("log_memory: don't open logfile. LastError = %d\n", last_error);
			ABORT(1);
		}
		time_t t_begin;
		time(&t_begin);
		fprintf(f, "%s", ctime(&t_begin));
		fprintf(f, "TBHits = %lld\n", printed_hits);
		fprintf(f, "%s\n", memory_allocation_done ? "Full memory allocation is done" : "Full memory allocation isn't done");
		fprintf(f, "Number load from file = %lld\n", global_cache.get_number_load_from_file());
		fprintf(f, "Number load from cache = %lld\n", global_cache.get_number_load_from_cache());
		fprintf(f, "Procent of loaded from cache = %.2lf%%\n", (double)(tbhits - global_cache.get_number_load_from_file()) * 100.0 / tbhits);
		rwlock_rdlock(hidden_locker);
		unsigned long long printed_hidden_size = cur_hidden_size;
		unsigned long long printed_hidden_max = max_hidden_size;
		rwlock_unlock(hidden_locker);
		unsigned long long cache_size = global_cache.get_size();
		fprintf(f, "cache_size = %lld Bytes = %lld Mb (%lld Mb)\n", cache_size, cache_size >> 20, (total_cache_size - printed_hidden_max) >> 20);
		fprintf(f, "cur_hidden_size = %lld Bytes = %lld Mb (%lld Mb)\n", printed_hidden_size, printed_hidden_size >> 20, printed_hidden_max >> 20);
		unsigned long long real_hidden_size = get_indexers_size() + get_bufferizers_size_all() + get_rd_bufferizers_size() + get_piece_offsets_size();
		fprintf(f, "real_hidden_size = %lld bytes = %lld Mb\n", real_hidden_size, real_hidden_size >> 20);
		unsigned long handles_count = 0;
		GetProcessHandleCount(GetCurrentProcess(), &handles_count);
		fprintf(f, "handle's count = %d\n", handles_count);
		int cnt = 0;
		for (int i = MIN_TYPE; i <= MAX_TYPE; i++) {
			cnt = 0;
			rwlock_rdlock(cache_file_buf_locker);
			for (unordered_map<unsigned long, bufferizer_list<cache_file_bufferizer> *>::iterator it = cache_file_bufferizers[i].begin();
				it != cache_file_bufferizers[i].end(); it++) {
				bufferizer_list<cache_file_bufferizer> *list = it->second;
				while (list != NULL) {
					cnt++;
					list = list->next;
				}
			}
			rwlock_unlock(cache_file_buf_locker);
			if (cnt > 0)
				fprintf(f, "cache_file_bufferizer's[%d] count = %d\n", i, cnt);
		}
		cnt = 0;
		rwlock_rdlock(read_file_buf_locker);
		for (unordered_map<unsigned long, bufferizer_list<read_file_bufferizer> *>::iterator it = read_file_bufferizers.begin();
			it != read_file_bufferizers.end(); it++) {
			bufferizer_list<read_file_bufferizer> *list = it->second;
			while (list != NULL) {
				cnt++;
				list = list->next;
			}
		}
		rwlock_unlock(read_file_buf_locker);
		fprintf(f, "read_file_bufferizer's count = %d\n", cnt);
		rwlock_rdlock(tb_indexers_locker);
		cnt = tb_indexers.size();
		rwlock_unlock(tb_indexers_locker);
		fprintf(f, "tb_indexer's count = %d\n", cnt);

		fclose(f);
		rwlock_unlock(log_locker);
	}
}

#endif

#define PL_UPDATE_FLAG 0x8000

int get_value_local(int *eval, int table_type, short_pieces_env *local_env, color_position *local_pos,
	unsigned long local_index, char *table_path) {
	TBINDEX tbind;
	cache_file_bufferizer *filebuf;

	if (local_env->men_count == 2) { // kk
		if (DTM_TYPE(table_type))
			*eval = -1;
		else
			*eval = 0;
		local_env->indexer = NULL;
		return PROBE_OK;
	}

	/*If current table not exist (known in earlier queries) then exit*/
	if (get_cur_table_not_exist_local(local_index, table_type)) {
		get_tb_name_local(probe_missing_table_name, local_env);
		local_env->indexer = NULL;
		return PROBE_NO_TABLE;
	}

	/*Create indexator*/
	rwlock_rdlock(probe_locker);
	unordered_map<unsigned long, custom_tb_indexer *>::iterator it;
	unsigned long index;
	if (!MUTABLE_TYPE(table_type))
		// same indexers for all colors and all table's types
		index = local_index >> 1;
	else
		// different indexer for any color and any table's type
		index = (table_type << 16) | local_index;
	local_env->indexer = NULL;
	rwlock_rdlock(tb_indexers_locker);
	if (caching_file_bufferizers && (it = tb_indexers.find(index)) != tb_indexers.end()) {
		local_env->indexer = it->second;
	}
	rwlock_unlock(tb_indexers_locker);
	if (local_env->indexer == NULL) {
		adjust_indexer_local(local_env, MUTABLE_TYPE(table_type));
		if (MUTABLE_TYPE(table_type)) {
			// set right order. Read blocks
			filebuf = begin_read_table_file_cache_local(local_pos->cur_wtm, FB_NO_SEEK, FB_TO_THE_END, table_type, local_env, local_index, table_path);
			if (!filebuf) {
				delete local_env->indexer;
				rwlock_unlock(probe_locker);
				get_tb_name_local(probe_missing_table_name, local_env);
				return PROBE_NO_TABLE;
			}
			pieces_block *right_order = filebuf->get_permutations_blocks();
			bool *white_king = (bool *)&right_order[8];
			((mutable_indexer *)local_env->indexer)->set_order(right_order, *white_king);
			mutex_lock(cache_bufferizer_mutexer);
			filebuf->unlock();
			mutex_unlock(cache_bufferizer_mutexer);
			if (!caching_file_bufferizers)
				delete filebuf;
		}
		if (caching_file_bufferizers) {
			rwlock_wrlock(tb_indexers_locker);
			if ((it = tb_indexers.find(index)) != tb_indexers.end()) {
				delete local_env->indexer;
				local_env->indexer = it->second;
			}
			else {
				tb_indexers.insert(make_pair(index, local_env->indexer));
				rwlock_wrlock(hidden_locker);
				cur_hidden_size += local_env->indexer->get_size();
#ifdef LOG_HIDDEN
				log_cur_hidden_size("indexer", local_env->indexer->get_size());
#endif
				rwlock_unlock(hidden_locker);
			}
			rwlock_unlock(tb_indexers_locker);
		}
	}

	tbind = local_env->indexer->encode(&local_pos->cur_pos, local_pos->cur_wtm);
	if (!caching_file_bufferizers)
		delete local_env->indexer;
	rwlock_unlock(probe_locker);
	if (tbind == TB_INVALID_INDEX) {
		return PROBE_INVALID_POSITION;
	}

	//Probe captures in don't care tables
	char search_results[3] = {0, 0, 0};
	if (table_type == DL || table_type == ZDL || table_type == WL || table_type == ZWL) {
		unsigned char local_board[128];
		clear_board_local(local_board);
		put_pieces_on_board_local(local_env, local_pos, local_board);
		if (!gen_forward_moves_capture_local(local_env, local_pos, local_board, search_results, table_type, probe_forward_capture_local)) {
			return PROBE_NO_TABLE;
		}
		if (search_results[0]) { // has win capture
			if (*eval != WIN_CAPTURE)
				*eval = 1;
			return PROBE_OK;
		}
	}

	/*Open file or return if file not exist*/
	rwlock_rdlock(probe_locker);
	filebuf = begin_read_table_file_cache_local(local_pos->cur_wtm, FB_NO_SEEK, FB_TO_THE_END, table_type, local_env, local_index, table_path);
	if (!filebuf) {
		rwlock_unlock(probe_locker);
		get_tb_name_local(probe_missing_table_name, local_env);
		return PROBE_NO_TABLE;
	}

	bool pl_update = false;
	if (table_type == PL && filebuf->dont_care())
		pl_update = true;

	/*Read value from file position, calculated  by tbind*/
	tbfile_entry tbe;
	tbfile_tern_entry tbte;
	switch (table_type) {
	case ML: case ZML: case PL: case ZPL:
		tbe = filebuf->get_value(tbind);
		mutex_lock(cache_bufferizer_mutexer);
		filebuf->unlock();
		mutex_unlock(cache_bufferizer_mutexer);
		rwlock_unlock(probe_locker);
		break;
	case TL: case ZTL: case DL: case ZDL: case WL: case ZWL:
		tbte = filebuf->get_ternary_value(tbind);
		mutex_lock(cache_bufferizer_mutexer);
		filebuf->unlock();
		mutex_unlock(cache_bufferizer_mutexer);
		rwlock_unlock(probe_locker);
		break;
	}

	/*Transform file entry*/
	switch (table_type) {
	case ML: case ZML:
		if (tbe)
			*eval = tbe - 1;
		else 
			*eval = -1;
		break;
	case PL: case ZPL:
		*eval = tbe;
		break;
	case TL: case ZTL:
		*eval = tbte;
		break;
	case DL: case ZDL: case WL: case ZWL:
		// win with win capture have already returned
		if (search_results[1] && (tbte == -1 || tbte == 0))
			*eval = 0;
		else
			*eval = tbte;
		break;
	}

	if (pl_update)
		*eval |= PL_UPDATE_FLAG;

	if (!caching_file_bufferizers)
		delete filebuf;

	rwlock_wrlock(hidden_locker);
	tbhits++;
	rwlock_unlock(hidden_locker);
#ifdef LOG_MEMORY
	log_memory();
#endif
	/* old tables format - *.lmw and *.lmb
	tbfile_double_entry tbe;
	tbe = ztb_get_entry(tbind, cur_wtm);
	if (tbe == TBFILE_NO_TABLE) {
		return PROBE_NO_TABLE;
	}
	if (TBFILE_HAS_VALUE(tbe)) {
		cur_moves = TBFILE_GET_VALUE(tbe);
		*eval = cur_moves;
	} else 
		*eval = -1;*/

	return PROBE_OK;
}

int get_simple_value_from_load_position_local(int *eval, int table_type, short_pieces_env *local_env, color_position *local_pos,
	int wl_value) {
	unsigned char local_board[128];
	clear_board_local(local_board);
	put_pieces_on_board_local(local_env, local_pos, local_board);
	if (wl_value == -1) {
		// Mate
		if (is_check_local(local_env, &local_pos->cur_pos, local_pos->cur_wtm) && 
			!gen_forward_moves_all_local(local_env, local_pos, local_board, NULL, 0, NULL)) {
			*eval = 0;
			return PROBE_OK;
		}
	}
	if (wl_value == 1) {
		char search_results[3] = {0, 0, 0};
		// Win in 1 move
		gen_forward_moves_check_local(local_env, local_pos, local_board, search_results, probe_check_for_mate_local);
		if (search_results[0]) {
			*eval = 1;
			return PROBE_OK;
		}
		// Win by pawn move (for dtz50)
		if (DTZ50_TYPE(table_type)) {
			if (!gen_forward_moves_pawn_local(local_env, local_pos, local_board, search_results, table_type - PL + WL, probe_forward_capture_local))
				return PROBE_NO_TABLE;
			if (search_results[0]) {
				*eval = 1;
				return PROBE_OK;
			}
		}
	}
	return PROBE_OK_WITHOUT_VALUE;
}

int get_explicit_value_from_load_position_local(int *eval, int table_type, short_pieces_env *local_env, color_position *local_pos,
	unsigned long local_index) {
	vector<int>::iterator it_types;
	list<char *>::iterator it_paths;
	int result = PROBE_NO_TABLE;
	for (it_types = types_vector[table_type].begin(); it_types != types_vector[table_type].end(); it_types++) {
		for (it_paths = table_paths.begin(); it_paths != table_paths.end(); it_paths++) {
			result = get_value_local(eval, *it_types, local_env, local_pos, local_index, *it_paths);
			if (result != PROBE_NO_TABLE)
				return result;
		}
		if (!known_not_exist)
			set_cur_table_not_exist_local(local_index, *it_types);
	}
	return result;
}

bool get_any_color_existance_local(int table_type, short_pieces_env *local_env, unsigned long local_index) {
	if (local_env->men_count == 2)
		return true;
	if (local_index & 1)
		local_index -= 1;
	vector<int>::iterator it_types;
	list<char *>::iterator it_paths;
	for (it_types = types_vector[table_type].begin(); it_types != types_vector[table_type].end(); it_types++) {
		if (known_not_exist) {
			if (!get_cur_table_not_exist_local(local_index, *it_types) || !get_cur_table_not_exist_local(local_index, *it_types))
				return true;
		} else {
			char tbname[MAX_PATH];
			for (it_paths = table_paths.begin(); it_paths != table_paths.end(); it_paths++) {
				get_output_tb_filename_local(tbname, tt_to_ft_map[*it_types], local_env, *it_paths);
				if (!access(tbname, 0))
					return true;
				get_output_tb_filename_local(tbname, tt_to_ft_map[*it_types] + 1, local_env, *it_paths);
				if (!access(tbname, 0))
					return true;
			}
			set_cur_table_not_exist_local(local_index, *it_types);
			set_cur_table_not_exist_local(local_index + 1, *it_types);
		}
	}
	return false;
}

int get_value_from_own_color_local(int *eval, int table_type, short_pieces_env *local_env, color_position *local_pos,
	unsigned long local_index) {
	if (table_type % DTZ50_BEGIN != PL)
		return get_explicit_value_from_load_position_local(eval, table_type, local_env, local_pos, local_index);
	int tmp_eval = DTZ50_TYPE(table_type) ? WIN_CAPTURE : 0;
	int result = get_explicit_value_from_load_position_local(&tmp_eval, table_type + WL - PL, local_env, local_pos, local_index);
	if (result != PROBE_OK || tmp_eval == 0) {
		*eval = -1;
		return result;
	} else if (tmp_eval == WIN_CAPTURE) { // only for dtz50
		*eval = 1;
		return result;
	}
	bool win = tmp_eval == 1;
	if (REJECT_PROBE(win, *eval)) { // we need only in lose or win forward positions
		*eval = REJECT_SOME(win);
		return PROBE_OK;
	}
	result = get_simple_value_from_load_position_local(eval, table_type, local_env, local_pos, tmp_eval);
	if (result != PROBE_OK_WITHOUT_VALUE) // simple value
		return result;
	int pl_update = tmp_eval;
	result = get_explicit_value_from_load_position_local(eval, table_type, local_env, local_pos, local_index);
	if (*eval & PL_UPDATE_FLAG)
		*eval ^= PL_UPDATE_FLAG; // remove flag
	else
		pl_update = 0;
	if (result == PROBE_OK)
		*eval = (*eval) * 2 + win;
	else
		*eval = REJECT_SOME(win);
	// Update pl-value to the max or min by captures
	if (pl_update != 0) {
		char search_results[4] = {0, 0, 0, 0}; // {return flag from gen, wl-value, 2-bytes for tbe}
		search_results[1] = win;
		*((tbfile_entry *)&search_results[2]) = *eval;
		unsigned char local_board[128];
		clear_board_local(local_board);
		put_pieces_on_board_local(local_env, local_pos, local_board);
		if (!gen_forward_moves_capture_local(local_env, local_pos, local_board, search_results, table_type, probe_forward_capture_pl_local)) {
			return PROBE_NO_TABLE;
		}
		*eval = *((tbfile_entry *)&search_results[2]);
	}
	return result;
}

int get_value_from_alien_color_local(int *eval, int table_type, short_pieces_env *local_env, color_position *local_pos) {
	int search_values[6] = {0, -2, -2, -2, 0, 0}; // {return from gen_moves, 3 init values, pawn move flag, initial eval (may be need only in win or lose)}
	// may be we know from WL that it's win or lose
	search_values[5] = *eval;
	unsigned char local_board[128];
	clear_board_local(local_board);
	put_pieces_on_board_local(local_env, local_pos, local_board);
	if (!gen_forward_moves_all_local(local_env, local_pos, local_board, (char *)search_values, table_type, probe_forward_move))
		return PROBE_NO_TABLE;
	bool dtm = DTM_TYPE(table_type);
	if (search_values[1] != -2) // win (move to lose exist)
		*eval = dtm ? (search_values[1] + NEXT_VALUE(0, table_type >= DTZ50_BEGIN)) : 1;
	else if (search_values[2] != -2) // draw (move to draw exist and no moves to win)
		*eval = dtm ? (-1) : 0;
	else if (search_values[3] != -2) // lose (move to win exist and no moves to win or draw)
		*eval = dtm ? (search_values[3] + NEXT_VALUE(1, table_type >= DTZ50_BEGIN) - 1) : -1;
	else // no moves
		if (is_check_local(local_env, &local_pos->cur_pos, local_pos->cur_wtm)) // checkmate
			*eval = dtm ? 0 : -1;
		else // stalemate
			*eval = dtm ? -1 : 0;
	return PROBE_OK;
}

int get_value_from_load_position_local(int *eval, int table_type, short_pieces_env *local_env, color_position *local_pos,
	unsigned long local_index) {
	if (!get_any_color_existance_local(table_type, local_env, local_index))
		return PROBE_NO_TABLE;
	int result = get_value_from_own_color_local(eval, table_type, local_env, local_pos, local_index);
	if (result == PROBE_NO_TABLE)
		result = get_value_from_alien_color_local(eval, table_type, local_env, local_pos);
	return result;
}

int get_value_from_position_local(int side, unsigned int *psqW, unsigned int *psqB, int *piCount, int sqEnP, int *eval,
	int table_type,	short_pieces_env *local_env, color_position *local_pos, unsigned long *local_index) {
	int result = parse_position(side, psqW, psqB, piCount, sqEnP, local_env, local_pos);
	if (result != PROBE_OK)
		return result;
	*local_index = get_table_index_local(local_pos->cur_wtm, local_env);
	return get_value_from_load_position_local(eval, table_type, local_env, local_pos, *local_index);
}

int get_value_from_fen_local(const char *fen, int *eval, int table_type, short_pieces_env *local_env, color_position *local_pos,
	unsigned long *local_index) {
	int result = parse_fen(fen, local_env, local_pos);
	if (result != PROBE_OK)
		return result;
	*local_index = get_table_index_local(local_pos->cur_wtm, local_env);
	return get_value_from_load_position_local(eval, table_type, local_env, local_pos, *local_index);
}

int parse_position(int side, unsigned int *psqW, unsigned int *psqB, int *piCount, int sqEnP, 
	short_pieces_env *local_env, color_position *local_pos, bool *was_invert) {
	bool invert_color;
	if (!load_lomonosov_tb_position_local(side, psqW, psqB, piCount, sqEnP, &invert_color, local_env, local_pos)) { // sets flag ztb_invert_color
		if (local_env->men_count >= 8)
			return PROBE_NO_TABLE;
		else
			return PROBE_NO_LOAD_FEN;
	}
	if (invert_color) { // pieces are already inverted by load_position
		local_pos->cur_wtm = !local_pos->cur_wtm;
		local_pos->cur_pos.as_number ^= RANKS;
	}
	if (!is_legal_position_local(local_env, &local_pos->cur_pos, local_pos->cur_wtm))
		return PROBE_INVALID_POSITION;
	if (was_invert)
		*was_invert = invert_color;
	return PROBE_OK;
}

int parse_fen(const char *fen, short_pieces_env *local_env, color_position *local_pos, bool *was_invert) {
	bool invert_color;
	if (!load_pieces_from_fen_local(fen, &invert_color, local_env) || !load_fen_local(fen, &invert_color, local_env, local_pos)) { // sets flag ztb_invert_color
		if (local_env->men_count >= 8)
			return PROBE_NO_TABLE;
		else
			return PROBE_NO_LOAD_FEN;
	}
	if (invert_color) { // pieces are already inverted by load_fen
		local_pos->cur_wtm = !local_pos->cur_wtm;
		local_pos->cur_pos.as_number ^= RANKS;
	}
	if (!is_legal_position_local(local_env, &local_pos->cur_pos, local_pos->cur_wtm))
		return PROBE_INVALID_POSITION;
	if (was_invert)
		*was_invert = invert_color;
	return PROBE_OK;
}
