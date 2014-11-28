#ifndef EGMAINTYPES_H_
#define EGMAINTYPES_H_

/*** table types: ***/
// endgame table:
#define ML		0
#define WL		1
#define TL		2
#define PL		3
#define DL		4
// dtz50 table:
#define ZML		5
#define ZWL		6
#define ZTL		7
#define ZPL		8
#define ZDL		9

/*** probing results ***/
#define PROBE_OK 0
#define PROBE_NO_TABLE 1
#define PROBE_INVALID_POSITION 2
#define PROBE_NO_LOAD_FEN 3
#define PROBE_UNKNOWN_TB_TYPE 4
#define PROBE_OK_WITHOUT_VALUE 5
#define PROBE_MATE 6
#define PROBE_NOT_FINISHED 7

#include "eginttypes.h"

#ifndef DISABLE_PARALLEL_TB
#define PARALLEL_TB
#endif

#ifndef LOMONOSOV_FULL
#define ABORT(x) { exit(x); }
#endif

#ifdef _MSC_VER
#include <windows.h>
#endif

// pieces
#define BPAWN    0   // pawns first, this is used in egiterations
#define WPAWN    1
#define KNIGHT   2
#define KING     3
#define BISHOP   4   // long-distance pieces must be last
#define ROOK     5
#define QUEEN    6

#define MAX_MEN 8

// constants for lomonosov_position
#define KING_INDEX 30 // index of king place in piece array
#define  C_PIECES  6    /* Maximum # of pieces of one color OTB */

// color index
#define IDX_WHITE 0
#define IDX_BLACK 1

// board stores indexes in pieces array, not pieces
#define EMPTY   0xff
#define BOX 	0x88

// indexer symmetry types
#define ST_8_FOLD      1
#define ST_PAWN        2

#define RANKS 0x7070707070707070LL
#define FILES 0x0707070707070707LL

#define RANKS_EXT 0xF0F0F0F0F0F0F0F0LL // to keep BOX values when performing diagonal flip
#define FILES_EXT 0x0F0F0F0F0F0F0F0FLL

#define TBFILE_NO_TABLE 0xffff
#define TBFILE_NO_TERN_TABLE 0xfe

// compression methods
#define TB_BINARY_BITS_MASK        0xf000 // mask for bits for encoding of one table value
#define TB_BINARY_MIN_MASK         0x0fff // mask for min value of tablebase

#define TB_COMP_METHOD_MASK        0x000f
#define TB_LZMA_SIMPLE                  1
#define TB_LZ4_SIMPLE					2
#define TB_LZ4_HC						3
#define TB_LZHAM_SIMPLE					4
#define TB_RE_PAIR					    5

// compression & format flags
#define TB_PERMUTATIONS			   0x0010
#define TB_FIX_COMP_SIZE		   0x0020
#define TB_RE_PAIR_PARAMS		   0x0040
#define TB_MD5_PROTECTED           0x8000
#define TB_TERNARY                 0x4000
#define TB_BINARY                  0x2000
#define FREE_BITS				   0x0b00

#define TB_TABLE_VERSION_MASK      0x0100
#define TB_VERSION_ONE             0x0000 // *.lmw and *.lmb
#define TB_VERSION_TWO             0x0100 // *.mlw and *.mlb optimized indexer and supporting of binary tables

#define TB_DONT_CARE_BIT		   0x0200
#define TB_DTZ50				   0x0400

#define RE_PAIR_NOTCOMPR_HEADER_FLAG	0x8000
#define RE_PAIR_HEADER_SIZE_MASK		0x7fff

/* Position storage.
 *
 * piece_pos stores coordinates of each piece in the following format:
 * 0YYY0XXX
 * "a1" field corresponds to 0x00, a8 - to 0x07.
 * Names of the pieces are defined in "pieces" array. */

typedef union {
	uint8_t piece_pos[8];
	uint64_t as_number; // beware of big-endian and little-endian!
} position;

/////////// compatibility with the non-POSIX system
#ifdef WIN32
#define os_lseek64 _lseeki64
#else
#define os_lseek64 lseek
#ifndef O_BINARY // ?
#define O_BINARY (0)
#endif
#endif

// bounds of table types (for iteration)
#define DTZ50_BEGIN ZML
#define MIN_TYPE ML
#define MAX_TYPE ZDL
#define TYPES_COUNT 10

// table types used in check folder
#define LM		10
#define LT		11
#define ZLM		12
#define ZLT		13

#define MUTABLE_TYPE(table_type) (table_type == WL || table_type == ZWL || table_type == PL || table_type == ZPL)
#define DTM_TYPE(table_type) (table_type == ML || table_type == ZML || table_type == PL || table_type == ZPL)
#define DTZ50_TYPE(table_type) (table_type >= DTZ50_BEGIN)
#ifdef LOMONOSOV_FULL
#define UNKNOWN_TB_TYPE(table_type) (table_type < MIN_TYPE || table_type > MAX_TYPE)
#else
#define UNKNOWN_TB_TYPE(table_type) (!MUTABLE_TYPE(table_type))
#endif

// index in endgame table
typedef uint64_t TBINDEX;

#define TB_INVALID_INDEX 0xffffffffffffffffLL

class custom_tb_indexer {
	public:
		uint64_t position_mask; // this field is different for little-endian and big-endian

		custom_tb_indexer() { position_mask = 0LL; }
		virtual ~custom_tb_indexer() { }
		virtual TBINDEX encode(position *p, bool white_to_move) = 0;
		virtual bool decode(TBINDEX index, bool white_to_move, position *p) = 0;
		virtual bool is_symmetrical(position *p, bool white_to_move) = 0; // is reasonable after 'decode'
		virtual TBINDEX tbsize(bool wtm) = 0;

		virtual int symmetry_type() = 0;

		virtual int get_size() = 0;
		virtual void get_tb_name(char *tbname) = 0;
};

typedef struct {
	unsigned char *pieces;
	int men_count, white_pieces;
	custom_tb_indexer *indexer;
	bool full_color_symmetry;
	unsigned char pslice_number;
} short_pieces_env;

typedef uint16_t tbfile_entry; // 16 bits
typedef int8_t tbfile_tern_entry; // 8 bits
#define TBFILE_ENTRY_SIZE 2 // equals to TB_MAIN_ENTRY_SIZE; sizeof(uint16_t)
#define TBFILE_TERNARY_ENTRY_SIZE 1

#define MAX_PATH 1024 // maximum length of absolute file name + '\0'

#endif /* EGMAINTYPES_H_ */
