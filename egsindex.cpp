#include <stdio.h>
#include "egmaintypes.h"
#include "egsindex.h"
#include "egpglobals.h"
#ifdef LOMONOSOV_FULL
#include "egtypes.h"
#include "egglobals.h"
#endif

#define SWAP(a,b,t) { t = a; a = b; b = t; }

unsigned int two_pieces_enum[64][64], three_pieces_enum[64][64][64];
unsigned char two_pieces_decode[64*63][2], three_pieces_decode[64*63*62][3];
static unsigned int two_pawns_enum[56][56], three_pawns_enum[56][56][56];
static unsigned char two_pawns_decode[56*55][2], three_pawns_decode[56*55*54][3];

unsigned short enpass_pawns_cnt;
unsigned int enpass_x_pawns_encode[8][8];
unsigned char enpass_x_pawns_decode[14][2];
unsigned char enpass_y_pawns[2][2];

// enumeration for kings
// king under attack must be in a1-d1-d4 triangle (pawnless) or a1-d8 rectangle (pawns)
// for pawnless, the opposite king must be in a1-a8-h8 if the king under attack is placed on a1-d4 diagonal
unsigned short pawnless_kings_cnt, pawn_kings_cnt;
short_array64 pawnless_kings_encode[64], pawn_kings_encode[64];
uchar_pair *pawnless_kings_decode, *pawn_kings_decode;

unsigned char board_to_bb[160]; // convert 0x88 coordinate into bb. BOX is converted into 0
unsigned char bb_to_board[128];

signed int symm_triangle_coeff[137];

#define INVAL_KINGS 0xffff

void init_simple_indexer() {
	int x, y, x2, y2, a, b, c, e, f, pslice;

	// 64 <--> 128 board
	for (x=0; x<8; x++)
		for (y=0; y<8; y++) {
			board_to_bb[x + (y << 4)] = x + (y << 3);
			bb_to_board[x + (y << 3)] = x + (y << 4);
		}

	// symmetrical enum: 2 pieces
	e = 0;
	for (a = 0; a < 64; a++)
		for (b = 0; b < a; b++) {
			two_pieces_enum[a][b] = e;
			two_pieces_enum[b][a] = e;
			two_pieces_decode[e][0] = a;
			two_pieces_decode[e][1] = b;
			e++;
		}
	// symmetrical enum: 2 pawns
	e = 0;
	for (pslice = 1; pslice <= 6; pslice++)
		for (c = pslice*8; c < (pslice+1)*8; c++)
			for (a = 8; a < c; a++) {
				two_pawns_enum[a][c] = e;
				two_pawns_enum[c][a] = e;
				two_pawns_decode[e][0] = a;
				two_pawns_decode[e][1] = c;
				e++;
			}
	// symmetrical enum: 3 pieces
	e = 0;
	for (a = 0; a < 64; a++)
		for (b = 0; b < a; b++)
			for (c = 0; c < b; c++) {
				three_pieces_enum[a][b][c] = e;
				three_pieces_enum[a][c][b] = e;
				three_pieces_enum[b][a][c] = e;
				three_pieces_enum[b][c][a] = e;
				three_pieces_enum[c][a][b] = e;
				three_pieces_enum[c][b][a] = e;
				three_pieces_decode[e][0] = a;
				three_pieces_decode[e][1] = b;
				three_pieces_decode[e][2] = c;
				e++;
			}
	// symmetrical enum: 3 pawns
	e = 0;
	for (pslice = 1; pslice <= 6; pslice++)
		for (c = pslice*8; c < (pslice+1)*8; c++)
			for (a = 8; a < c; a++)
				for (b = 8; b < a; b++) {
					three_pawns_enum[a][b][c] = e;
					three_pawns_enum[a][c][b] = e;
					three_pawns_enum[b][a][c] = e;
					three_pawns_enum[b][c][a] = e;
					three_pawns_enum[c][a][b] = e;
					three_pawns_enum[c][b][a] = e;
					three_pawns_decode[e][0] = a;
					three_pawns_decode[e][1] = b;
					three_pawns_decode[e][2] = c;
					e++;
				}

	// enpass pawns
	e = 0;
	for (int s = -1; s <= 1; s += 2) {
		for (a = (s < 0 ? 1 : 0); ((a > a+s) ? a : (a+s)) < 8; a++) {
			enpass_x_pawns_encode[a][a+s] = e;
			enpass_x_pawns_decode[e][0] = a;
			enpass_x_pawns_decode[e][1] = a+s;
			e++;
		}
	}
	enpass_pawns_cnt = e;
	enpass_y_pawns[true][WPAWN] = 64;
	enpass_y_pawns[true][BPAWN] = 112;
	enpass_y_pawns[false][WPAWN] = 0;
	enpass_y_pawns[false][BPAWN] = 48;

	e = f = 0;
	for (a = 0; a < 64; a++)
		for (b = 0; b < 64; b++)
			pawnless_kings_encode[a][b] = pawn_kings_encode[a][b] = INVAL_KINGS;

	e = f = 0;
	for (a = 0; a < 64; a++) {
		y = a >> 3;
		x = a & 7;
		for (b = 0; b < 64; b++) {
			x2 = b & 7;
			y2 = b >> 3;
			if (x2 > x+1 || x > x2+1 || y2 > y+1 || y > y2+1) {
				// pawn
				if (x < 4) pawn_kings_encode[a][b] = f++;
				// pawnless
				if (y <= x && x < 4)
					if (x != y || x2 >= y2)
						pawnless_kings_encode[a][b] = e++;
			}
		}
	}
	pawnless_kings_cnt = e;
	pawn_kings_cnt = f;
	pawnless_kings_decode = (uchar_pair*)malloc(pawnless_kings_cnt*sizeof(uchar_pair));
	pawn_kings_decode = (uchar_pair*)malloc(pawn_kings_cnt*sizeof(uchar_pair));
	for (a = 0; a < 64; a++)
		for (b = 0; b < 64; b++) {
			e = pawnless_kings_encode[a][b];
			if (e != INVAL_KINGS) {
				pawnless_kings_decode[e][0] = a;
				pawnless_kings_decode[e][1] = b;
			}
			e = pawn_kings_encode[a][b];
			if (e != INVAL_KINGS) {
				pawn_kings_decode[e][0] = a;
				pawn_kings_decode[e][1] = b;
			}
		}

	for (a = 0; a < 137; a++) symm_triangle_coeff[a] = 0;
	e = 1;
	for (a = 0; a < 8; a++)
		for (b = a+1; b < 8; b++) {
			symm_triangle_coeff[(a << 4) + b] = e;
			symm_triangle_coeff[(b << 4) + a] = -(signed int)e;
			e <<= 1;
		}
}

void mutable_indexer::get_tb_name(char *tbname) {
	int i, j;
	j = 0;
	for (i = 0; i < men_count_local; i++) {
		if (i == white_pieces_local && pieces_local[i-1] == WPAWN && pslice_number_local)
			tbname[j++] = pslice_number_local+1+'0';
		tbname[j++] = piece_name_local[pieces_local[i]];
	}
	tbname[j]   = '\0';
}

void mutable_indexer::sort_position(position *p, bool enpass) {
	unsigned char long_end = long_block_end;
	if (enpass && (long_end == white_pieces_local || long_end == men_count_local))
		long_end -= 1;
	for (int i = 0; i < long_end - long_block_start - 3; ++i) {
		int j, max = -1, max_j;
		unsigned char tc;
		for (j = long_block_start; j < long_end - i; ++j)
			if (p->piece_pos[j] > max) {
				max = p->piece_pos[j];
				max_j = j;
			}
		SWAP(p->piece_pos[long_end - 1 - i], p->piece_pos[max_j], tc);
	}
}

bool mutable_indexer::is_sort(position *p, bool enpass) {
	char long_end = long_block_end;
	if (enpass && (long_end == white_pieces_local || long_end == men_count_local))
		long_end -= 1;
	for (int i = 0; i < long_end - long_block_start - 3; ++i) {
		for (int j = long_block_start; j < long_block_start + 3; ++j)
			if (p->piece_pos[long_end - 1 - i] < p->piece_pos[j])
				return false;
	}
	return true;
}

void mutable_indexer::encode_piece_block(position *p, int i, pieces_block *bl, int *size, int *code) {
	pieces_block block = bl[i];
	int one_count = 0;
	switch (block.length) {
	case 1:
		*code = board_to_bb[p->piece_pos[block.start]];
		for (int j = 0; j < i; j++)
			if (pieces_local[bl[j].start] > WPAWN && bl[j].length == 1) {
				if (p->piece_pos[bl[j].start] < p->piece_pos[block.start]) *code -= 1;
				one_count++;
			}
		*size = 64 - one_count;
		break;
	case 2:
		*code = two_pieces_enum[board_to_bb[p->piece_pos[block.start]]][board_to_bb[p->piece_pos[block.start+1]]];
		*size = two_pieces_enum[63][62] + 1;
		break;
	case 3:
		*code = three_pieces_enum [board_to_bb[p->piece_pos[block.start]]] [board_to_bb[p->piece_pos[block.start+1]]] [board_to_bb[p->piece_pos[block.start+2]]];
		*size = three_pieces_enum[63][62][61] + 1;
		break;
	default:
		printf("mutable_indexer::encode_piece_block: Error! Block from %d pieces\n", block.length);
		ABORT(10);
	}
}

void mutable_indexer::decode_piece_block(position *p, pieces_block block, int *one_count, TBINDEX *index) {
	int size, code;
	switch (block.length) {
	case 1:
		size = 64 - *one_count;
		p->piece_pos[block.start] = (*index % size);
		(*one_count)--;
		break;
	case 2:
		size = two_pieces_enum[63][62] + 1;
		code = *index % size;
		p->piece_pos[block.start] = bb_to_board[two_pieces_decode[code][0]];
		p->piece_pos[block.start + 1] = bb_to_board[two_pieces_decode[code][1]];
		break;
	case 3:
		size = three_pieces_enum[63][62][61] + 1;
		code = *index % size;
		p->piece_pos[block.start] = bb_to_board[three_pieces_decode[code][0]];
		p->piece_pos[block.start + 1] = bb_to_board[three_pieces_decode[code][1]];
		p->piece_pos[block.start + 2] = bb_to_board[three_pieces_decode[code][2]];
		break;
	default:
		printf("mutable_indexer::decode_piece_block: Error! Block from %d pieces\n", block.length);
		ABORT(10);
	}
	*index = *index / size;
}

void mutable_indexer::to_symmetrical(position *p) {
	int i, kings;
	unsigned long long t;
	bool first;
	signed int sm;

	if ((p->piece_pos[wait_king] & 0x07) > 3)
		p->as_number = p->as_number ^ FILES;
	if ((p->piece_pos[wait_king] & 0x70) > 0x30)
		p->as_number = p->as_number ^ RANKS;
	sm = symm_triangle_coeff[p->piece_pos[wait_king]];
	i = move_king;
	first = true;
	kings = 0;
	while (sm == 0 && kings < 2) {
		if (!(p->piece_pos[i] & BOX)) {
            sm = symm_triangle_coeff[p->piece_pos[i]];
            while (i != move_king && i < men_count_local-1 && pieces_local[i] == pieces_local[i+1]) {
                i++;
                sm += symm_triangle_coeff[p->piece_pos[i]];
            }
		}
		if (first) { i = wait_king+1; first = false; } else i++;
		while (i == 0 || i == white_pieces_local) { i++; kings++; }
		if (i == men_count_local) { i = 1; kings++; }
	}

	if (sm < 0) {
		t = p->as_number;
		p->as_number = (p->as_number & FILES_EXT) << 4;
		t = (t & RANKS_EXT) >> 4;
		p->as_number |= t;
	}
}

TBINDEX mutable_indexer::encode(position *p, bool white_to_move) {
	TBINDEX index = 0;
	int x, y, i, code, size;
	position p2 = *p;
	to_symmetrical(&p2);
	sort_position(&p2, false);
	//encode blocks
	for (i = 0; i < blocks_count; i++) {
		if (blocks[i].start == 0) {
			code = pawnless_kings_encode[board_to_bb[p2.piece_pos[wait_king]]][board_to_bb[p2.piece_pos[move_king]]];
			size = pawnless_kings_cnt;
		} else
			encode_piece_block(&p2, i, blocks, &size, &code);
		index = index * size + code;
	}
	return index;
}

void mutable_indexer::restore_real_position(position *p, pieces_block *bl, unsigned char bl_count) {
	unsigned char cells[8];
	unsigned char cnt = 0;
	int i, j;
	for (i = 0; i < bl_count; i++) {
		if (pieces_local[bl[i].start] > WPAWN && bl[i].length == 1) {
			for (j = 0; j < cnt; j++)
				if (cells[j] <= p->piece_pos[bl[i].start])
					++p->piece_pos[bl[i].start];
			for (j = cnt; j >= 0; j--)
				if (j > 0 && cells[j-1] > p->piece_pos[bl[i].start])
					cells[j] = cells[j-1];
				else {
					cells[j] = p->piece_pos[bl[i].start];
					break;
				}
			++cnt;
		}
	}
	for (i = 0; i < bl_count; i++) {
		if (pieces_local[bl[i].start] > WPAWN && bl[i].length == 1) {
			p->piece_pos[bl[i].start] = bb_to_board[p->piece_pos[bl[i].start]];
		}
	}
}

bool mutable_indexer::decode(TBINDEX index, bool white_to_move, position *p) {
	if (index >= indexer_size) return false;
	int i, j;
	int one_count = 0;
	for (i = 0; i < blocks_count; i++)
		if (pieces_local[blocks[i].start] > WPAWN && blocks[i].length == 1)
			one_count++;
	one_count--;
	for (i = blocks_count - 1; i >= 0; i--) {
		if (blocks[i].start == 0) {
			int code = index % pawnless_kings_cnt;
			p->piece_pos[wait_king] = bb_to_board[pawnless_kings_decode[code][0]];
			p->piece_pos[move_king] = bb_to_board[pawnless_kings_decode[code][1]];
			index = index / pawnless_kings_cnt;
		} else
			decode_piece_block(p, blocks[i], &one_count, &index);
	}
	// restore real number of cells
	restore_real_position(p, blocks, blocks_count);
	// check correct
	for (i = 0; i < men_count_local - 1; i++)
		for (j = i + 1; j < men_count_local; j++)
			if (p->piece_pos[i] == p->piece_pos[j]) return false;
	return true;
}

void mutable_indexer::calculate_size() {
	int size, code;
	position p;
	p.as_number = 0;
	indexer_size = 0;
	for (int i = 0; i < blocks_count; i++) {
		if (blocks[i].start == 0) {
			size = pawnless_kings_cnt;
		} else
			encode_piece_block(&p, i, blocks, &size, &code);
		indexer_size = indexer_size * size + size - 1;
	}
	++indexer_size;
}

bool mutable_indexer::is_symmetrical(position *p, bool white_to_move) {
	if ((p->piece_pos[wait_king] & 0x07) > 3)
		return true;
	if ((p->piece_pos[wait_king] & 0x70) > 0x30)
		return true;

	int i;
	unsigned char kings;
	bool first;
	signed int sm;

	sm = symm_triangle_coeff[p->piece_pos[wait_king]];
	i = move_king;
	first = true;
	kings = 0;
	while (sm == 0 && kings < 2) {
		if (!(p->piece_pos[i] & BOX)) {
            sm = symm_triangle_coeff[p->piece_pos[i]];
            while (i != move_king && i < men_count_local-1 && pieces_local[i] == pieces_local[i+1]) {
                i++;
                sm += symm_triangle_coeff[p->piece_pos[i]];
            }
		}
		if (first) { i = wait_king+1; first = false; } else i++;
		while (i == 0 || i == white_pieces_local) { i++; kings++; }
		if (i == men_count_local) { i = 1; kings++; }
	}
	if (sm < 0)
		return true;
	return !is_sort(p, false);
}

void mutable_indexer::set_order(pieces_block *blocks_, bool white_king, unsigned char blocks_count_) {
	if (blocks_count_ != 0)
		blocks_count = blocks_count_;
	for (int i = 0; i < blocks_count; i++)
		blocks[i] = blocks_[i];
	if (white_king) {
		wait_king = 0;
		move_king = white_pieces_local;
	} else {
		wait_king = white_pieces_local;
		move_king = 0;
	}
}

#define BOUNDS_FOR_COLOR_INDEXER(white,start,stop) \
        { start = ((white) ? 0 : white_pieces_local); \
        	stop = ((white) ? white_pieces_local-1 : men_count_local-1) ; }

void pawn_mutable_indexer::to_symmetrical(position *p) {
	if ((p->piece_pos[wait_king] & 0x07) > 3)
		p->as_number ^= FILES;
}

void pawn_mutable_indexer::encode_pawn_block(position *p, pieces_block block, bool exact_pslice, int *size, int *code) {
	unsigned char pslice = (pieces_local[block.start] == WPAWN) ? pslice_number_local : 6;
	switch (block.length) {
	case 1:
		if (exact_pslice) {
			*code = p->piece_pos[block.start] & 7;
			*size = 8;
		} else {
			*code = board_to_bb[p->piece_pos[block.start]] - 8;
			*size = pslice * 8;
		}
		break;
	case 2:
		*code = two_pawns_enum[board_to_bb[p->piece_pos[block.start]]] [board_to_bb[p->piece_pos[block.start+1]]];
		*size = two_pawns_enum[pslice*8 + 7][pslice*8 + 6] + 1;
		if (exact_pslice && pslice > 1) {
			*code -= two_pawns_enum[pslice*8][8];
			*size -= two_pawns_enum[pslice*8][8];
		}
		break;
	case 3:
		*code = three_pawns_enum [board_to_bb[p->piece_pos[block.start]]] [board_to_bb[p->piece_pos[block.start+1]]] [board_to_bb[p->piece_pos[block.start+2]]];
		*size = three_pawns_enum[pslice*8 + 7][pslice*8 + 6][pslice*8 + 5] + 1;
		if (exact_pslice && pslice > 1) {
			*code -= three_pawns_enum[pslice*8][8][9];
			*size -= three_pawns_enum[pslice*8][8][9];
		}
		break;
	default:
		printf("mutable_indexer::encode_pawn_block: Error! Block from %d pieces\n", block.length);
		ABORT(10);
	}
}

void pawn_mutable_indexer::decode_pawn_block(position *p, pieces_block block, bool exact_pslice, TBINDEX *index) {
	unsigned char pslice = (pieces_local[block.start] == WPAWN) ? pslice_number_local : 6;
	int size, code;
	switch (block.length) {
	case 1:
		size = 8;
		if (!exact_pslice)
			size += (pslice - 1)*8;
		p->piece_pos[block.start] = (*index % size) + 8;
		if (exact_pslice)
			p->piece_pos[block.start] += (pslice - 1)*8;
		p->piece_pos[block.start] = bb_to_board[p->piece_pos[block.start]];
		break;
	case 2:
		size = two_pawns_enum[pslice*8 + 7][pslice*8 + 6] + 1;
		if (exact_pslice && pslice > 1)
			size -= two_pawns_enum[pslice*8][8];
		code = *index % size;
		if (exact_pslice && pslice > 1)
			code += two_pawns_enum[pslice*8][8];
		p->piece_pos[block.start] = bb_to_board[two_pawns_decode[code][0]];
		p->piece_pos[block.start + 1] = bb_to_board[two_pawns_decode[code][1]];
		break;
	case 3:
		size = three_pawns_enum[pslice*8 + 7][pslice*8 + 6][pslice*8 + 5] + 1;
		if (exact_pslice && pslice > 1)
			size -= three_pawns_enum[pslice*8][8][9];
		code = *index % size;
		if (exact_pslice && pslice > 1)
			code += three_pawns_enum[pslice*8][8][9];
		p->piece_pos[block.start] = bb_to_board[three_pawns_decode[code][0]];
		p->piece_pos[block.start + 1] = bb_to_board[three_pawns_decode[code][1]];
		p->piece_pos[block.start + 2] = bb_to_board[three_pawns_decode[code][2]];
		break;
	default:
		printf("mutable_indexer::decode_pawn_block: Error! Block from %d pieces\n", block.length);
		ABORT(10);
	}
	*index = *index / size;
}

bool pawn_mutable_indexer::search_enpass(position *p, bool white_to_move) {
	int i, j, pair;
	unsigned char tc, enp;
	// white pawn to the left of black pawn is more priority
	if (enpass_size[1] > 0 && !white_to_move) for ( i = white_pieces_local - 1; i >= 0 && pieces_local[i] == WPAWN; i--) {
		if (p->piece_pos[i] < 8) {
			enp = p->piece_pos[i] + 48;
			pair = -1;
			for (j = men_count_local - 1; j >= white_pieces_local && pieces_local[j] == BPAWN; j--) {
				if (pair < 0 && p->piece_pos[j] == enp - 1)
					pair = j;
				if (p->piece_pos[j] == enp + 1)
					pair = j;
			}
			if (pair < 0) {
				printf("pawn_mutable_indexer::search_enpass: enpassant pawn without pair!, pos = %llx\n", p->as_number);
				ABORT(10);
			}
			SWAP(p->piece_pos[i], p->piece_pos[white_pieces_local - 1], tc);
			SWAP(p->piece_pos[pair], p->piece_pos[men_count_local - 1], tc);
			return true;
		}
	}

	if (enpass_size[0] > 0 && white_to_move) for ( j = men_count_local - 1; j >= 0 && pieces_local[j] == BPAWN; j--) {
		if (p->piece_pos[j] > 103) {
			enp = p->piece_pos[j] - 48;
			pair = -1;
			for (i = white_pieces_local - 1; i >= 0 && pieces_local[i] == WPAWN; i--) {
				if (pair < 0 && enp == p->piece_pos[i] - 1)
					pair = i;
				if (enp == p->piece_pos[i] + 1)
					pair = i;
			}
			if (pair < 0) {
				printf("pawn_mutable_indexer::search_enpass: enpassant pawn without pair!, pos = %llx\n", p->as_number);
				ABORT(10);
			}
			SWAP(p->piece_pos[pair], p->piece_pos[white_pieces_local - 1], tc);
			SWAP(p->piece_pos[j], p->piece_pos[men_count_local - 1], tc);
			return true;
		}
	}
	
	return false;
}

TBINDEX pawn_mutable_indexer::encode_enpass(position *p, bool white_to_move) {
	sort_position(p, true);
	TBINDEX index = 0;
	int code, size, i;
	// other pieces
	bool exact_pslice = (enpass_y_pawns[white_to_move][white_to_move] >> 4) == pslice_number_local;
	unsigned char white_x_pawn = p->piece_pos[white_pieces_local - 1] & 7;
	unsigned char black_x_pawn = p->piece_pos[men_count_local - 1] & 7;
	unsigned char pc;
	for (i = 0; i < enpass_blocks_count; i++) {
		pc = pieces_local[enpass_blocks[i].start];
		if (enpass_blocks[i].start == white_pieces_local - 1) {
			code = enpass_x_pawns_encode[white_x_pawn][black_x_pawn];
			size = enpass_pawns_cnt;
		} else if (pc <= WPAWN)
			encode_pawn_block(p, enpass_blocks[i], !exact_pslice && (enpass_blocks[i].start + enpass_blocks[i].length) == white_pieces_local - 1, &size, &code);
		else if (pc == KING) {
			code = pawn_kings_encode[board_to_bb[p->piece_pos[wait_king]]][board_to_bb[p->piece_pos[move_king]]];
			size = pawn_kings_cnt;
		} else
			encode_piece_block(p, i, enpass_blocks, &size, &code);
		index = index * size + code;
	}
	index = index + indexer_size;
	return index;
}

TBINDEX pawn_mutable_indexer::encode(position *p, bool white_to_move) {
	position p2 = *p;
	to_symmetrical(&p2);
	//if enpassant
	if (enpass_size[!white_to_move] > 0 && search_enpass(&p2, white_to_move)) return encode_enpass(&p2, white_to_move);
	
	sort_position(&p2, false);
	TBINDEX index = 0;
	int i, size, code;
	unsigned char pc;
	for (i = 0; i < blocks_count; i++) {
		pc = pieces_local[blocks[i].start];
		if (pc <= WPAWN)
			encode_pawn_block(&p2, blocks[i], (blocks[i].start + blocks[i].length) == white_pieces_local, &size, &code);
		else if (pc == KING) {
			code = pawn_kings_encode[board_to_bb[p2.piece_pos[wait_king]]][board_to_bb[p2.piece_pos[move_king]]];
			size = pawn_kings_cnt;
		} else
			encode_piece_block(&p2, i, blocks, &size, &code);
		index = index * size + code;
	}
	return index;
}

bool pawn_mutable_indexer::decode_enpass(TBINDEX index, bool white_to_move, position *p) {
	index = index - indexer_size;
	int i, j;
	int one_count = 0;
	for (i = 0; i < enpass_blocks_count; i++)
		if (pieces_local[enpass_blocks[i].start] > WPAWN && enpass_blocks[i].length == 1)
			one_count++;
	one_count--;
	bool exact_pslice = (enpass_y_pawns[white_to_move][white_to_move] >> 4) == pslice_number_local;
	unsigned char pc;
	for (i = enpass_blocks_count - 1; i >= 0; i--) {
		pc = pieces_local[enpass_blocks[i].start];
		if (enpass_blocks[i].start == white_pieces_local - 1) {
			int code = index % enpass_pawns_cnt;
			p->piece_pos[white_pieces_local - 1] = enpass_y_pawns[white_to_move][WPAWN] | enpass_x_pawns_decode[code][0];
			p->piece_pos[men_count_local - 1] = enpass_y_pawns[white_to_move][BPAWN] | enpass_x_pawns_decode[code][1];
			index = index / enpass_pawns_cnt;
		} else if (pc <= WPAWN)
			decode_pawn_block(p, enpass_blocks[i], !exact_pslice && (enpass_blocks[i].start + enpass_blocks[i].length) == white_pieces_local - 1, &index);
		else if (pc == KING) {
			int code = index % pawn_kings_cnt;
			p->piece_pos[wait_king] = bb_to_board[pawn_kings_decode[code][0]];
			p->piece_pos[move_king] = bb_to_board[pawn_kings_decode[code][1]];
			index = index / pawn_kings_cnt;
		} else
			decode_piece_block(p, enpass_blocks[i], &one_count, &index);
	}
	// restore real number of cells
	restore_real_position(p, enpass_blocks, enpass_blocks_count);
	// check correct
	for (i = 0; i < men_count_local - 1; i++)
		for (j = i + 1; j < men_count_local; j++)
			if (adjust_enpassant_pos(p->piece_pos[i], pieces_local[i]) == adjust_enpassant_pos(p->piece_pos[j], pieces_local[j])) return false;
	return true;
}

bool pawn_mutable_indexer::decode(TBINDEX index, bool white_to_move, position *p) {
	if (index >= indexer_size + enpass_size[!white_to_move]) return false;
	if (index >= indexer_size) return decode_enpass(index, white_to_move, p);
	int i, j;
	int one_count = 0;
	for (i = 0; i < blocks_count; i++)
		if (pieces_local[blocks[i].start] > WPAWN && blocks[i].length == 1)
			one_count++;
	one_count--;
	unsigned char pc;
	for (i = blocks_count - 1; i >= 0; i--) {
		pc = pieces_local[blocks[i].start];
		if (pc <= WPAWN)
			decode_pawn_block(p, blocks[i], (blocks[i].start + blocks[i].length) == white_pieces_local, &index);
		else if (pc == KING) {
			int code = index % pawn_kings_cnt;
			p->piece_pos[wait_king] = bb_to_board[pawn_kings_decode[code][0]];
			p->piece_pos[move_king] = bb_to_board[pawn_kings_decode[code][1]];
			index = index / pawn_kings_cnt;
		} else
			decode_piece_block(p, blocks[i], &one_count, &index);
	}
	// restore real number of cells
	restore_real_position(p, blocks, blocks_count);
	// check correct
	for (i = 0; i < men_count_local - 1; i++)
		for (j = i + 1; j < men_count_local; j++)
			if (p->piece_pos[i] == p->piece_pos[j]) return false;
	return true;
}

void pawn_mutable_indexer::calculate_size() {
	int size, code;
	position p;
	p.as_number = 0;
	indexer_size = 0;
	unsigned char pc;
	for (int i = 0; i < blocks_count; i++) {
		pc = pieces_local[blocks[i].start];
		if (pc <= WPAWN)
			encode_pawn_block(&p, blocks[i], (blocks[i].start + blocks[i].length) == white_pieces_local, &size, &code);
		else if (pc == KING) {
			size = pawn_kings_cnt;
		} else
			encode_piece_block(&p, i, blocks, &size, &code);
		indexer_size = indexer_size * size + size - 1;
	}
	++indexer_size;
}

TBINDEX pawn_mutable_indexer::calculate_enpass_size(bool white_to_move) {
	int size, code;
	position p;
	p.as_number = 0;
	TBINDEX enp_size = 0;
	bool exact_pslice = (enpass_y_pawns[white_to_move][white_to_move] >> 4) == pslice_number_local;
	unsigned char pc;
	for (int i = 0; i < enpass_blocks_count; i++) {
		pc = pieces_local[enpass_blocks[i].start];
		if (enpass_blocks[i].start == white_pieces_local - 1) {
			size = enpass_pawns_cnt;
		} else if (pc <= WPAWN)
			encode_pawn_block(&p, enpass_blocks[i], !exact_pslice && (enpass_blocks[i].start + enpass_blocks[i].length) == white_pieces_local - 1, &size, &code);
		else if (pc == KING) {
			size = pawn_kings_cnt;
		} else
			encode_piece_block(&p, i, enpass_blocks, &size, &code);
		enp_size = enp_size * size + size - 1;
	}

	return enp_size+1;
}

bool pawn_mutable_indexer::is_symmetrical(position *p, bool white_to_move) {
	if ((p->piece_pos[wait_king] & 0x07) > 3)
		return true;
	bool enpass = (pieces_local[men_count_local - 1] == BPAWN) && ((p->piece_pos[white_pieces_local - 1] < 16) || (p->piece_pos[men_count_local - 1] >= 112));
	if (enpass && ((p->piece_pos[white_pieces_local - 1] & 7) > (p->piece_pos[men_count_local - 1] & 7))) {
		unsigned char enp_pc = (p->piece_pos[white_pieces_local - 1] < 16) ? WPAWN : BPAWN;
		unsigned char enp_pos = adjust_enpassant_pos((enp_pc == WPAWN) ? p->piece_pos[white_pieces_local - 1] : p->piece_pos[men_count_local - 1], enp_pc);
		unsigned char pair_end = (enp_pc == WPAWN) ? men_count_local - 2 : white_pieces_local - 2;
		int b = (enp_pc == WPAWN) ? 1 : -1;
		for ( ; pieces_local[pair_end] <= WPAWN; pair_end--)
			if (p->piece_pos[pair_end] == enp_pos + b)
				return true;
	}

	return !is_sort(p, enpass);
}

void pawn_mutable_indexer::set_order(pieces_block *blocks_, bool white_king, unsigned char blocks_count_) {
	mutable_indexer::set_order(blocks_, white_king, blocks_count_);
	if (enpass_size[0] == 0 && enpass_size[1] == 0)
		return;
	blocks_to_enpass_blocks();
}

mutable_indexer::mutable_indexer(unsigned char men_count_, unsigned char white_pieces_,	unsigned char pslice_number_,
	unsigned char *pieces_) {
	memcpy(pieces_local, pieces_, men_count_);
	men_count_local = men_count_;
	white_pieces_local = white_pieces_;
	pslice_number_local = pslice_number_;
	wait_king = 0;
	move_king = white_pieces_local;
	//standart blocks
	blocks_count = 0;
	for (int i = 0; i < men_count_local; ) {
		//kings together
		if (i == white_pieces_local) { ++i; continue; }
		blocks[blocks_count].start = i;
		if ((i == white_pieces_local - 1) || (i == men_count_local - 1) || (pieces_local[i] != pieces_local[i+1])) { ++i; } // no similiar figures
		else if ((i == white_pieces_local - 2) || (i == men_count_local - 2) || (pieces_local[i] != pieces_local[i+2])) { i+=2; } // 2 similiar figures
		else { i+=3; } // 3 similiar figures
		blocks[blocks_count].length = i - blocks[blocks_count].start;
		if (blocks[blocks_count].start == 0)
			blocks[blocks_count].length = 2;
		blocks_count++;
	}
	// long block > 3. It is always the only.
	long_block_start = long_block_end = 0;
	for (int i = 1; i <= men_count_local; ++i) {
		if (i == white_pieces_local || i == men_count_local || pieces_local[i] != pieces_local[i - 1]) {
			long_block_end = i;
			if (long_block_end - long_block_start > 3)
				break;
			long_block_start = i;
		}
	}
	calculate_size();
}

pawn_mutable_indexer::pawn_mutable_indexer(unsigned char men_count_, unsigned char white_pieces_, unsigned char pslice_number_,
	unsigned char *pieces_) : mutable_indexer(men_count_, white_pieces_, pslice_number_, pieces_) {
	if (pieces_local[white_pieces_local - 1] != WPAWN) {
		printf("pawn_mutable_indexer:: create without pawns!");
		ABORT(1);
	}
	calculate_size();
	// about enpass
	enpass_size[0] = enpass_size[1] = 0;
	if (pieces_local[men_count_local - 1] == BPAWN) {
		bool two_white_pawns = white_pieces_local > 1 && pieces_local[white_pieces_local - 2] == WPAWN;
		bool has_enpass_black = (two_white_pawns && pslice_number_local >= 3) || (!two_white_pawns && pslice_number_local == 3);
		bool has_enpass_white = (two_white_pawns && pslice_number_local >= 4) || (!two_white_pawns && pslice_number_local == 4);
		if (has_enpass_white || has_enpass_black) {
			blocks_to_enpass_blocks();
		}
		if (has_enpass_white) enpass_size[0] = calculate_enpass_size(true);
		if (has_enpass_black) enpass_size[1] = calculate_enpass_size(false);
	}
}

void pawn_mutable_indexer::blocks_to_enpass_blocks() {
	enpass_blocks_count = 0;
	for (int i = 0; i < blocks_count; i++) {
		if (blocks[i].start + blocks[i].length == white_pieces_local && pieces_local[blocks[i].start] == WPAWN) {
			if (blocks[i].length > 1) {
				enpass_blocks[enpass_blocks_count].start = blocks[i].start;
				enpass_blocks[enpass_blocks_count++].length = blocks[i].length - 1;
			}
			enpass_blocks[enpass_blocks_count].start = white_pieces_local - 1;
			enpass_blocks[enpass_blocks_count++].length = 2;
		} else if (blocks[i].start + blocks[i].length == men_count_local && pieces_local[blocks[i].start] == BPAWN) {
			if (blocks[i].length > 1) {
				enpass_blocks[enpass_blocks_count].start = blocks[i].start;
				enpass_blocks[enpass_blocks_count++].length = blocks[i].length - 1;
			}
		} else
			enpass_blocks[enpass_blocks_count++] = blocks[i];
	}
}
