#ifndef EGPGLOBALS_H_
#define EGPGLOBALS_H_

#include <list>
#include "egmaintypes.h"
#include "egfilebuf.h"

#define PD_MOVE 0
#define PD_PROMO_RANK 1
#define PD_FIRST_RANK 2
#define PD_ENEMY 3
#define PD_ENEMY_ENPASS_RANK 4
#define PD_ENPASS_RANK 5
#define PD_AFTER_ENPASS_RANK 6
#define PD_MY_ENPASS_RANK 7
extern const signed char pawn_data[2][8];

extern long long bb_places[129];
extern long long bb_piece_attacks[7][128];
extern signed int bb_not_block_index[];
extern long long bb_piece_not_blocks[3][128][128];
extern const int board_vec[];
extern const int vec_start[];

inline unsigned char adjust_enpassant_pos(unsigned char pos, unsigned char piece) {
	if (piece == WPAWN && pos <= 7) return pos+48;
	if (piece == BPAWN && pos >= 112) return pos-48;
	return pos;
}
void init_bitboards();

extern bool caching_file_bufferizers;
extern std::list<char *> table_paths;
extern char path_to_logs[MAX_PATH];
extern unsigned long long tbhits;
extern bool logging_memory;
extern unsigned long min_block_size[TYPES_COUNT];
extern char max_pieces_count[TYPES_COUNT];

#ifdef PARALLEL_TB
#include <pthread.h>
extern pthread_rwlock_t paths_lock;
extern pthread_rwlock_t read_file_buf_lock;
extern pthread_rwlock_t cache_file_buf_lock;
extern pthread_rwlock_t tb_indexers_lock;
extern pthread_rwlock_t piece_offsets_lock;
extern pthread_rwlock_t hidden_lock;
extern pthread_rwlock_t probe_lock;
extern pthread_rwlock_t requests_lock;
extern pthread_rwlock_t log_lock;
extern pthread_rwlock_t not_exist_tables_lock;
extern pthread_mutex_t auto_clean_count_mutex;
extern pthread_mutex_t cache_bufferizer_mutex;
extern pthread_mutex_t read_bufferizer_mutex;
extern pthread_mutex_t block_counter_mutex;
#endif

extern void *paths_locker;
extern void *read_file_buf_locker;
extern void *cache_file_buf_locker;
extern void *tb_indexers_locker;
extern void *piece_offsets_locker;
extern void *hidden_locker;
extern void *probe_locker;
extern void *requests_locker;
extern void *log_locker;
extern void *not_exist_tables_locker;
extern void *auto_clean_count_mutexer;
extern void *cache_bufferizer_mutexer;
extern void *read_bufferizer_mutexer;
extern void *block_counter_mutexer;

void rwlock_rdlock(void *rwlock);
void rwlock_wrlock(void *rwlock);
void rwlock_unlock(void *rwlock);
void mutex_lock(void *mutex);
void mutex_unlock(void *mutex);
void cond_wait(void *cond, void *mutex);
void cond_signal(void *cond);

void create_tb_index_tree();
void clear_tb_index_tree();
unsigned long get_table_index_from_pieces_count(unsigned char *pieces_count);

bool is_big_endian();

#define VIRTUAL_FILE_BLOCKS_COUNT (2 << 20)

#define TABLE_MAX_INDEX 42100 // 16 bits
#define NOT_EXIST_TABLES_SIZE TABLE_MAX_INDEX / 8 + 1
extern bool known_not_exist;
extern char probe_missing_table_name[];
extern unsigned char not_exist_tables[TYPES_COUNT][NOT_EXIST_TABLES_SIZE];

void reset_not_exist_tables(bool all_exist);
unsigned char get_bit_by_index(unsigned char *arr, int index);
void set_bit_by_index(unsigned char *arr, int index, unsigned char value);
void new_set_cur_table_not_exist(const char *path, int ternary_table);

void pieces_to_canonical(unsigned char *pieces, int men, int *white_pcs,
						int *map_from_source, bool *invert_color);
unsigned int sq64_to_lomonosov(unsigned int sq64);
int sum_array(int * arr, int count);

typedef struct {
	file_offset byte;
	int offset;
} tbfile_ternary_index;

TBINDEX ternary_index_to_tbfile_index(tbfile_ternary_index index);
tbfile_ternary_index tbfile_index_to_ternary_index(TBINDEX tbind, char tern_in_byte);

#endif /* EGPGLOBALS_H_ */
