#include <list>
#include "eginttypes.h"
#include "egmaintypes.h"
#include "egpglobals.h"

// move generators

// we use 0x88 board

const signed char pawn_data[2][8] = {
//  move	promot.rank 	first rank  enemy  enemy enpass	 enpass.rank  after enpass.  my enpass.
	{-16,	0x00, 			0x60,  		WPAWN, 	0x00,        0x30,        0x20,          0x70}, // black pawn
	{ 16,	0x70, 			0x10,  		BPAWN, 	0x70,        0x40,        0x50,          0x00}  // white pawn
};

const int board_vec[] = { 1, -1, 16, -16, 0, // rook
		            1, -1, 16, -16, // queen & king
		            17, 15, -15, -17, 0, // & bishop, black pawn
		            18, -14, 14, -18, 31, 33, -31, -33, 0, // knight
		            17, 15, 0 }; // white pawn


const int vec_start[] = { 11, 23, 14, 5, 9, 0, 5 };

// bitboards: which cells are attacked. coordinates are BOX (requires more memory, but should be faster. optimize?)
// piece_attacks[0] stores black pawn, [1] - white pawn.
long long bb_piece_attacks[7][128];
long long bb_places[129]; // just 1 bit set, last is used for BOX king place
signed int bb_not_block_index[] = { -1, -1, -1, -1, 0, 1, 2 };
long long bb_piece_not_blocks[3][128][128]; // [index of attack (bb_not_block_index[piece])][attacking_pos][blocking_pos]

#define brd88to64(x) ((x & 7) | ((x & 0xf0) >> 1))

void init_bitboards() {
	int i, j, k, v, n, b, bi, j0;

	for (i=0; i<128; i = (i+9) & ~8) {
		bb_places[i] = (long long)1 << brd88to64(i);
	}
	bb_places[128] = 0LL;

	// fill bb_piece_attacks
	for (i=0; i<7; i++) {
		for (j0=0; j0<128; j0 = (j0+9) & ~8) {
			bb_piece_attacks[i][j0] = 0;
			j = adjust_enpassant_pos(j0,i);
			k = vec_start[i];
			while ((v = board_vec[k++])) {
				n = j+v;
				while (!(n & 0x88)) {
					bb_piece_attacks[i][j0] |= bb_places[n];
					n += v;
					if (i <= KING) break;
				}
			}

			bi = bb_not_block_index[i];
			if (bi >= 0) { // piece can be blocked, fill table
				for (b = 0; b < 128; b = (b+9) & ~8) {
					bb_piece_not_blocks[(unsigned int)bi][j0][b] = 0;
					k = vec_start[i];
					while ((v = board_vec[k++])) {
						n = j+v;
						while (!(n & 0x88)) {
							bb_piece_not_blocks[(unsigned int)bi][j0][b] |= bb_places[n];
							if (n == b) break; // block!
							n += v;
						}
					}
				}
			}
		}
	}
}

bool caching_file_bufferizers;
std::list<char *> table_paths;
char path_to_logs[MAX_PATH] = "";
unsigned long long tbhits = 0;
bool logging_memory = false;
unsigned long min_block_size[TYPES_COUNT] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
char max_pieces_count[TYPES_COUNT] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

#ifdef PARALLEL_TB
#include <pthread.h>
pthread_rwlock_t paths_lock;
pthread_rwlock_t read_file_buf_lock;
pthread_rwlock_t cache_file_buf_lock;
pthread_rwlock_t tb_indexers_lock;
pthread_rwlock_t piece_offsets_lock;
pthread_rwlock_t hidden_lock;
pthread_rwlock_t probe_lock;
pthread_rwlock_t requests_lock;
pthread_rwlock_t log_lock;
pthread_rwlock_t not_exist_tables_lock;
pthread_mutex_t auto_clean_count_mutex;
pthread_mutex_t cache_bufferizer_mutex;
pthread_mutex_t read_bufferizer_mutex;
pthread_mutex_t block_counter_mutex;
#endif

void *paths_locker = NULL;
void *read_file_buf_locker = NULL;
void *cache_file_buf_locker = NULL;
void *tb_indexers_locker = NULL;
void *piece_offsets_locker = NULL;
void *hidden_locker = NULL;
void *probe_locker = NULL;
void *requests_locker = NULL;
void *log_locker = NULL;
void *not_exist_tables_locker = NULL;
void *auto_clean_count_mutexer = NULL;
void *cache_bufferizer_mutexer = NULL;
void *read_bufferizer_mutexer = NULL;
void *block_counter_mutexer = NULL;

#if defined(PARALLEL_TB) || defined(SMP_MODE)
#include <pthread.h>
#endif

void rwlock_rdlock(void *rwlock) {
#if defined(PARALLEL_TB) || defined(SMP_MODE)
	if (!rwlock) return;
	pthread_rwlock_rdlock((pthread_rwlock_t *)rwlock);
#endif
}

void rwlock_wrlock(void *rwlock) {
#if defined(PARALLEL_TB) || defined(SMP_MODE)
	if (!rwlock) return;
	pthread_rwlock_wrlock((pthread_rwlock_t *)rwlock);
#endif
}

void rwlock_unlock(void *rwlock) {
#if defined(PARALLEL_TB) || defined(SMP_MODE)
	if (!rwlock) return;
	pthread_rwlock_unlock((pthread_rwlock_t *)rwlock);
#endif
}

void mutex_lock(void *mutex) {
#if defined(PARALLEL_TB) || defined(SMP_MODE)
	if (!mutex) return;
	pthread_mutex_lock((pthread_mutex_t *)mutex);
#endif
}

void mutex_unlock(void *mutex) {
#if defined(PARALLEL_TB) || defined(SMP_MODE)
	if (!mutex) return;
	pthread_mutex_unlock((pthread_mutex_t *)mutex);
#endif
}

void cond_wait(void *cond, void *mutex) {
#if defined(PARALLEL_TB) || defined(SMP_MODE)
	if (!mutex || !cond) return;
	pthread_cond_wait((pthread_cond_t *)cond, (pthread_mutex_t *)mutex);
#endif
}

void cond_signal(void *cond) {
#if defined(PARALLEL_TB) || defined(SMP_MODE)
	if (!cond) return;
	pthread_cond_broadcast((pthread_cond_t *)cond);
#endif
}

struct tb_index_tree {
	unsigned long index;
	unsigned char pieces_count;
	tb_index_tree *childs;
};

#define MAX_NON_KINGS 5

static tb_index_tree tb_index_tree_root[MAX_NON_KINGS + 1];

tb_index_tree *create_tb_index_tree_childs(unsigned char pieces_count, int depth, unsigned long *cur_ind) {
	if (depth == 0 || pieces_count == 0)
		return NULL;
	tb_index_tree *tree;
	tree = (tb_index_tree *)malloc((pieces_count + 1) * sizeof(tb_index_tree));
	for (unsigned char i = 0; i <= pieces_count; i++) {
		tree[i].index = 0;
		tree[i].childs = NULL;
		tree[i].pieces_count = pieces_count;
		tree[i].childs = create_tb_index_tree_childs(pieces_count-i, depth-1, cur_ind);
		if (depth == 1) {
			tree[i].index = *cur_ind;
			(*cur_ind)++;
		}
	}
	if (depth > 1) {
		tree[pieces_count].index = *cur_ind;
		(*cur_ind)++;
	}
	return tree;
}

void create_tb_index_tree() {
	unsigned long cur_ind = 1;
	for (unsigned char i = 0; i <= MAX_NON_KINGS; i++) {
		tb_index_tree_root[i].index = 0;
		tb_index_tree_root[i].childs = NULL;
		tb_index_tree_root[i].pieces_count = MAX_NON_KINGS;
		tb_index_tree_root[i].childs = create_tb_index_tree_childs(MAX_NON_KINGS - i, 2*MAX_NON_KINGS - 1, &cur_ind);
	}
	tb_index_tree_root[MAX_NON_KINGS].index = cur_ind;
}

void delete_tb_index_tree_childs(tb_index_tree *tree) {
	if (tree->childs == NULL)
		return;
	for (unsigned char i = 0; i <= tree->childs->pieces_count; i++)
		delete_tb_index_tree_childs(&tree->childs[i]);
	free(tree->childs);
	tree->childs = NULL;
}

void clear_tb_index_tree() {
	for (unsigned char i = 0; i <= MAX_NON_KINGS; i++)
		delete_tb_index_tree_childs(&tb_index_tree_root[i]);
}

unsigned long get_table_index_from_pieces_count(unsigned char *pieces_count) {
	tb_index_tree *tree = tb_index_tree_root;
	tb_index_tree *temp_tree;
	for (unsigned char i = 0; i < 10; i++) {
		if (pieces_count[i] > tree[0].pieces_count)
			return 0;
		if (tree[pieces_count[i]].childs == NULL)
			return tree[pieces_count[i]].index;
		tree = tree[pieces_count[i]].childs;
	}
	return 0;
}

bool is_big_endian() {
	const uint16_t i = 1;
	const char* const s = (const char*)&i;
	return *s == 0;
}

bool known_not_exist = false;
char probe_missing_table_name[16];
unsigned char not_exist_tables[TYPES_COUNT][NOT_EXIST_TABLES_SIZE];

unsigned char get_bit_by_index(unsigned char *arr, int index) {
	rwlock_rdlock(not_exist_tables_locker);
	unsigned char val = arr[index / 8];
	rwlock_unlock(not_exist_tables_locker);
	val >>= index % 8;
	return val & 1;
}
 
// for best perfomance sets only "1"
void set_bit_by_index(unsigned char *arr, int index, unsigned char value) {
	value <<= index % 8;
	rwlock_wrlock(not_exist_tables_locker);
	arr[index / 8] |= value;
	rwlock_unlock(not_exist_tables_locker);
}

// for values "1" and "0"
void new_set_bit_by_index(unsigned char *arr, int index, unsigned char value) {
	rwlock_wrlock(not_exist_tables_locker);
	if (value != 0) {
		value <<= index % 8;
		arr[index / 8] |= value;
	}
	else {
		value = 1;
		value <<= index % 8;
		value = ~value;
		arr[index / 8] &= value;
	}
	rwlock_unlock(not_exist_tables_locker);
}

#define pawn 0
#define knight 1
#define king 2
#define bishop 3
#define rook 4
#define queen 5
//"<path>\kqkr.mlw" or "kqrk.mlw" or "kqkr"
unsigned long get_table_index_by_filename(const char *table_name) {
	int i, str_len = strlen(table_name), volume_number = 1, table_color = 1;
	i = str_len;
	// find filename
	while (i > 0 && table_name[i] != '\\')
		--i;
	while (table_name[i] != 'k' && i < str_len)
	++i;
	++i;
	// now i is at start of white pieces
	unsigned long index;
	unsigned char pieces_count[10] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
	bool black_pieces = false;
	while (table_name[i] != '.' && i < str_len) {
		int piece = -1;
		switch (table_name[i]) {
			case 'p': piece = pawn; break;
			case 'n': piece = knight; break;
			case 'k': black_pieces = !black_pieces; break;
			case 'b': piece = bishop; break;
			case 'r': piece = rook; break;
			case 'q': piece = queen; break;
			default: volume_number = table_name[i] - '0'; break;
		}
		if (piece >= 0) {
			if (piece > KING - 1) piece--;
			piece = piece + 5 * black_pieces;
			pieces_count[piece]++;
		}
		++i;
	}
	// find in ext table color type
	while (i < str_len) {
		if (table_name[i] == 'w')
			table_color = 1;
		else if (table_name[i] == 'b')
			table_color = 0;
		++i;
	}
	--volume_number;
	index = get_table_index_from_pieces_count(pieces_count);
	index = index * 7 + volume_number;
	index = index * 2 + table_color;
	return index;
}

//mark cur position as exist
void new_set_cur_table_not_exist(const char *path, int ternary_table) {
	unsigned long index = get_table_index_by_filename(path);
	if (index <= TABLE_MAX_INDEX) {
		new_set_bit_by_index(not_exist_tables[ternary_table], index, 0);
	}
}

static void invert_uchar_array_colors(unsigned char *arr, int men, int white_pcs) {
	unsigned char tmp_pieces[MAX_MEN];
	int i;

	for (i = 0; i < white_pcs; i++)	tmp_pieces[i] = arr[i];
	for (i = white_pcs; i < men; i++) arr[i - white_pcs] = arr[i];
	for (i = 0; i < white_pcs; i++)	arr[i + (men-white_pcs)] = tmp_pieces[i];
}


static void invert_int_array_colors(int *arr, int men, int white_pcs) {
	int tmp_pieces[MAX_MEN], i;

	for (i = 0; i < white_pcs; i++)
		tmp_pieces[i] = arr[i];
	for (i = white_pcs; i < men; i++)
		arr[i - white_pcs] = arr[i];
	for (i = 0; i < white_pcs; i++)
		arr[i + (men-white_pcs)] = tmp_pieces[i];
}

#define SWAP(a,b,t) { t = a; a = b; b = t; }

static int piece_order[7] = { 0, 0, // pawns
					   1, // knight
					   16, // king
					   2, // bishop
					   4, // rook
					   8} ; // queen

static void reorder_pieces(unsigned char *pieces, int start, int stop, int *map) {
	// bubble, bubble! :)
	int i, j, t;
	char tc;

	for (i = start; i <= stop; i++)
		for (j = start; j < stop; j++)
			if (piece_order[pieces[j]] < piece_order[pieces[j+1]]) {
				SWAP(pieces[j], pieces[j+1], tc);
				SWAP(map[j], map[j+1], t);
			}
}

// reorders pieces array. map_from_source[i] will give the source index in canonical
void pieces_to_canonical(unsigned char *pieces, int men, int *white_pcs,
						int *map_from_source, bool *invert_color) {
	int i, j, t, map_to_source[MAX_MEN];

	for (i = 0; i < men; i++) map_to_source[i] = i;
	*invert_color = false;

	reorder_pieces(pieces, 0, (*white_pcs)-1, map_to_source);
	reorder_pieces(pieces, *white_pcs, men-1, map_to_source);

	if (pieces[*white_pcs - 1] != WPAWN && pieces[men - 1] == BPAWN) *invert_color = true; // slicing by white pawn only
	else if (pieces[*white_pcs - 1] == WPAWN && pieces[men - 1] != BPAWN) *invert_color = false; // do not invert
	else { // majority is white
		if (*white_pcs < ((men+1) >> 1)) *invert_color = true; else
			if (*white_pcs == (men >> 1))
				for (i = 0; i < *white_pcs; i++) {
					if (piece_order[pieces[i]] < piece_order[pieces[i + *white_pcs]])
						*invert_color = true;
					if (piece_order[pieces[i]] != piece_order[pieces[i + *white_pcs]])
						break;
							}
	}
	//if (has_full_color_symmetry) *invert_color = true; // TODO: support full color symmetry
	if (*invert_color) {
		invert_uchar_array_colors(pieces, men, *white_pcs);
		invert_int_array_colors(map_to_source, men, *white_pcs);
		*white_pcs = men - *white_pcs;
		for (i = 0; i < men; i++) // invert pawns color
			if (pieces[i] <= WPAWN) pieces[i] = WPAWN - pieces[i];
	}

	// reverse map
	for (i = 0; i < men; i++) map_from_source[i] = i;

	// bubble again
	for (i = 0; i < men; i++)
		for (j = 0; j < men-1; j++)
			if (map_to_source[j] > map_to_source[j+1]) {
				SWAP(map_to_source[j], map_to_source[j+1], t);
				SWAP(map_from_source[j], map_from_source[j+1], t);
			}
}

unsigned int sq64_to_lomonosov(unsigned int sq64) {
	return ((sq64 >> 3) << 4) | (sq64 & 7);
}

int sum_array(int * arr, int count) {
	int result = 0;
	for (int i = 0; i < count; ++i)
		result += arr[i];
	return result;
}

tbfile_ternary_index tbfile_index_to_ternary_index(TBINDEX tbind, char tern_in_byte) {
	tbfile_ternary_index res;
	res.byte = tbind / tern_in_byte;
	res.offset = tbind % tern_in_byte;
	return res;
}

TBINDEX ternary_index_to_tbfile_index(tbfile_ternary_index index) {
	return index.byte * 5 + index.offset;
}
