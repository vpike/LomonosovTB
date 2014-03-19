#ifndef EGSINDEX_H_
#define EGSINDEX_H_

#include "egmaintypes.h"

typedef unsigned char uchar;
typedef uchar uchar_pair[2];
typedef unsigned short short_array64[64];

extern unsigned int two_pieces_enum[64][64], three_pieces_enum[64][64][64];
extern unsigned char two_pieces_decode[64*63][2], three_pieces_decode[64*63*62][3];
extern unsigned short enpass_pawns_cnt;
extern unsigned int enpass_x_pawns_encode[8][8];
extern unsigned char enpass_x_pawns_decode[14][2], enpass_y_pawns[2][2];
extern unsigned short pawnless_kings_cnt, pawn_kings_cnt;
extern short_array64 pawnless_kings_encode[64], pawn_kings_encode[64];
extern uchar_pair *pawnless_kings_decode, *pawn_kings_decode;
extern unsigned char board_to_bb[160];
extern unsigned char bb_to_board[128];
extern signed int symm_triangle_coeff[137];

void init_simple_indexer();

static unsigned char piece_name_local[8] = "ppnkbrq";

struct pieces_block {
	unsigned char start: 4;
	unsigned char length: 4;
};

// works only for <= 8 pieces. Pieces are sorted
class mutable_indexer : public custom_tb_indexer {
protected:
	unsigned char pieces_local[8];
	unsigned char men_count_local, white_pieces_local;
	unsigned char pslice_number_local;
	TBINDEX indexer_size;
	// long block (4, 5 or 6 pieces) will partition on smallest and largets (4 = 1 + 3, 5 = 2 + 3, 6 = 3 + 3)
	unsigned char long_block_start, long_block_end;
	unsigned char wait_king, move_king;
	unsigned char blocks_count;
	pieces_block blocks[8];
	void sort_position(position *p, bool enpass);
	bool is_sort(position *p, bool enpass);
	virtual void calculate_size();
	void encode_piece_block(position *p, int i, pieces_block *bl, int *size, int *code);
	void decode_piece_block(position *p, pieces_block block, int *one_count, TBINDEX *index);
	void restore_real_position(position *p, pieces_block *bl, unsigned char bl_count);
public:
	mutable_indexer(unsigned char men_count_, unsigned char white_pieces_, unsigned char pslice_number_, unsigned char *pieces_);
	~mutable_indexer() { }
	TBINDEX encode(position *p, bool white_to_move);
	bool decode(TBINDEX index, bool white_to_move, position *p);
	bool is_symmetrical(position *p, bool white_to_move);
	TBINDEX tbsize(bool wtm) { return indexer_size; }
	virtual void set_order(pieces_block *blocks_, bool white_king = true, unsigned char blocks_count_ = 0);
	int get_order(pieces_block *blocks_) {
		for (int i = 0; i < blocks_count; i++)
			blocks_[i] = blocks[i];
		return blocks_count;
	}
	int symmetry_type() { return ST_8_FOLD; }
	int get_size() { return sizeof(mutable_indexer); }
	void get_tb_name(char *tbname);
	virtual void to_symmetrical(position *p);
};

class pawn_mutable_indexer : public mutable_indexer {
protected:
	TBINDEX enpass_size[2];
	unsigned char enpass_blocks_count;
	pieces_block enpass_blocks[6];
	void to_symmetrical(position *p);
	TBINDEX encode_enpass(position *p, bool white_to_move);
	bool decode_enpass(TBINDEX index, bool white_to_move, position *p);
	bool search_enpass(position *p, bool white_to_move);
	void calculate_size();
	TBINDEX calculate_enpass_size(bool white_to_move);
	void encode_pawn_block(position *p, pieces_block block, bool exact_pslice, int *size, int *code);
	void decode_pawn_block(position *p, pieces_block block, bool exact_pslice, TBINDEX *index);
	void blocks_to_enpass_blocks();
public:
	pawn_mutable_indexer(unsigned char men_count_, unsigned char white_pieces_, unsigned char pslice_number_, unsigned char *pieces_);
	~pawn_mutable_indexer() { }
	TBINDEX encode(position *p, bool white_to_move);
	bool decode(TBINDEX index, bool white_to_move, position *p);
	bool is_symmetrical(position *p, bool white_to_move);
	TBINDEX tbsize(bool wtm) { return indexer_size + enpass_size[!wtm]; }
	void set_order(pieces_block *blocks_, bool white_king = true, unsigned char blocks_count_ = 0);
	int symmetry_type() { return ST_PAWN; }
	int get_size() { return sizeof(pawn_mutable_indexer); }
};

#endif /* EGSINDEX_H_ */
