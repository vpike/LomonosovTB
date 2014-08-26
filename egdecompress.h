#ifndef EGDECOMPRESS_H_
#define EGDECOMPRESS_H_

#include "egfilebuf.h"
#include "egsindex.h"
#include "egdecompre-pair.h"

/* Compressed tables format.
 *
 * First (main) file of the compressed table has the same name as an uncompressed table file.
 * Two first bytes of compressed file are 'C' 'o' , which can never occur in an uncompressed table.
 *
 * So, the first file has the following format:
 *
 * +---+---+------+-----------------------+-----------------+--------------------+----------------++
 * |'C'|'o'| type | size of uncompr.piece | pieces per file | total uncompr size | COMPRESS OPTS. ||
 * +---+---+------+-----------------------+-----------------+--------------------+----------------++ ...
 * |2 bytes|  2+2 |        4 bytes        |  4 bytes        |       8 bytes      | see below      ||
 * +---+---+------+-----------------------+-----------------+--------------------+----------------++
 *
 *     +-------------------++--------++--------++-- - - - - +-------------------------------+
 *     |    PIECES TABLE   || Piece0 || Piece1 ||           | PieceN (N <= pieces per file) |
 * ... +-------------------++--------++--------++-- - - - - +-------------------------------+
 *     | 4*pieces per file ||                                                               |
 *     +-------------------++--------++--------++-- - - - - +-------------------------------+
 *
 * COMPRESS OPTS. are
 * +--------+------------------------+
 * | Length | Compression properties |
 * +--------+------------------------+
 * |   2    |   (Length) bytes       |
 * +--------+------------------------+
 *
 * PIECES TABLE contains 1 4-byte integer value per each compressed piece. This value is a size
 * of the compressed piece in bytes.
 *
 * The second, the third and all other files has the following format:
 *
 *  +-------------------++--------+--------+-- - - - - +--------+
 *  |    PIECES TABLE   || Piece0 | Piece1 |           | PieceN |
 *  +-------------------++--------+--------+-- - - - - +--------+
 *  | 4*pieces per file ||                                      |
 *  +-------------------++--------+--------+-- - - - - +--------+
 *
 * All integers are little-endian.
 * DATA IN PIECES ARE BIG-ENDIAN (little-endians must flip bytes before compressing and after decompressing).
 * This gives a slightly better compression (result is 1% smaller).
 * 
 * For MD5-protected types, MD5 hash (16 bytes) is written after each data piece, and these 16 bytes are already
 * counted in PIECES TABLE.
 * Also, one 16-byte MD5 hash is written after compress opts. (hash of header), and another one - just after PIECES TABLE 
 * (hash of the table).
 * ( MD5 hashes are drawn above as double vertical lines. )
 *
 * TYPE field
 *	Now second two bytes are used only for binary tables
 *  +---------+--------------+-------------+--------+------------+-----------------+---------------+
 *  |MD5 FLAG | TERNARY FLAG | BINARY FLAG |  EMPTY | DTZ50 FLAG | DON'T CARE FLAG | VERSION FIELD |
 *	+---------+--------------+-------------+--------+------------------------------+---------------+
 *  |  1 bit  |     1 bit    |    1 bit    | 2 bits |    1 bit   |      1 bit      |     1 bit     |
 *  +---------+--------------+-------------+--------+------------+-----------------+---------------+
 *
 *  +--------+--------------------+-------------------+-------------+-------------------+-----------------+
 *  |  EMPTY | FIX COMP SIZE FLAG | PERMUTATIONS FLAG | COMP_METHOD |   BITS_FOR_PIECE  |  OFFSET_VALUE   |
 *  +--------+--------------------+-------------------+-------------+-------------------+-----------------+
 *  | 2 bits |        1 bit       |       1 bits      |    8 bits   |      4 bits       |     12 bits     |
 *  *--------+--------------------+-------------------+-------------+-------------------+-----------------+
 * */

// Buffered reading both plain and compressed files.
// In case of plain file, src_file is NULL.
class compressed_file_bufferizer: public read_file_bufferizer {
protected:
	read_file_bufferizer *src_file;
	bool uncompressed_mode;
	bool bit_shift_of_block;
	bool piece_table_loaded;
	bool probe_one_exact;
	bool primary_dc_change;
	char dc_lower_bound;
	// return size of uncompressed buffer
	size_t virtual read_compressed_buffer(char **compressed_buffer, size_t *comp_size, unsigned long long *piece_number);
	void virtual free_compressed_buffer(char *compressed_buffer) { free(compressed_buffer); }
	void read_buffer(/*int *index = 0*/);
	short arch_type;
	short arch_info;
	unsigned int uncompr_piece_size, pieces_per_file, current_file_number, pieces_in_file;
	unsigned int compression_opts_size, pieces_in_last_file;
	unsigned char *compression_opts;
	unsigned int max_size_of_buf;
	file_offset *piece_offsets;
	unsigned int files_count;
	TBINDEX *file_starts;
	char *org_file_name;
	file_offset header_size;
	char f_tern_in_byte;
	unsigned char f_men_count;
	unsigned char virtual_files_count;
	unsigned char current_virtual_file_number;
	file_offset *virtual_files_shift;
	Re_pair::RePairDecompressor *re_pair_decompr;
	pieces_block permutation_blocks[9];
	char sizeof_ps;
	bool virtual new_src_file(unsigned int file_number);
	void calc_virtual_file_number();
	unsigned int virtual load_piece_table();
	void load_virtual_files_shift();
	void choose_src_file();
	unsigned long long get_piece_number();
	file_offset get_header_size() {
		if (!(arch_type & TB_FIX_COMP_SIZE))
			return header_size;
		file_offset head_size = 0;
		if (current_file_number < files_count - 1)
			head_size += pieces_per_file * sizeof_ps;
		else
			head_size += pieces_in_last_file * sizeof_ps;
		head_size += (virtual_files_count - 1) * sizeof(file_offset);
		if (current_file_number == 0)
			head_size += header_size;
		return head_size;
	}
	void virtual add_virtual_info_to_cache() { }
public:
	bool full_begin_read;
	compressed_file_bufferizer() {
		src_file = NULL; 
		compression_opts = NULL;
		org_file_name = NULL;
		piece_offsets = NULL;
		no_file = true;
		bit_shift_of_block = true;
		not_caching = true;
		f_tern_in_byte = 5;
		re_pair_decompr = NULL;
		sizeof_ps = 4;
		probe_one_exact = false;
		file_starts = NULL;
		max_size_of_buf = 0;
		full_begin_read = true;
		virtual_files_count = 1;
		current_virtual_file_number = 0;
		virtual_files_shift = NULL;
	}
	virtual ~compressed_file_bufferizer() {
		if (not_caching && src_file) delete src_file;
		if (compression_opts) free(compression_opts);
		if (org_file_name) free(org_file_name);
		if (not_caching && piece_offsets) free(piece_offsets);
		if (re_pair_decompr) delete re_pair_decompr;
		if (file_starts) free(file_starts);
		if (not_caching && virtual_files_shift) free(virtual_files_shift);
	}
	void read(char *data, unsigned long size);
	bool begin_read(const char *filename, file_offset start_pos, file_offset length/*, int *tb_index = 0*/);
	bool is_compressed() { return !uncompressed_mode; }
	tbfile_entry get_value(TBINDEX tbind);
	tbfile_tern_entry get_ternary_value(TBINDEX tbind);
	void get_range(TBINDEX tbind, TBINDEX *start, TBINDEX *end);
	virtual void seek(file_offset new_pos);
	char *file_name() {
		return org_file_name;
	}
	unsigned short bits_for_record, offset_value;
	unsigned int index_in_buffer;
	unsigned int get_uncompr_piece_size() {
		if (uncompressed_mode)
			return 2048 << 10;
		else
			return uncompr_piece_size;
	}
	unsigned int get_pieces_per_file() {
		return pieces_per_file;
	}
	short get_arch_type() {
		return arch_type;
	}
	short get_arch_info() {
		return arch_info;
	}
	pieces_block *get_permutations_blocks() {
		return &permutation_blocks[0];
	}
	uint16_t get_dict_size() {
		if (re_pair_decompr)
			return re_pair_decompr->get_dict_size();
		else
			return 0;
	}
	// bit shift of binary and pack every 5 ternary to byte for RE_PAIR
	bool get_bit_shift_of_block() { return bit_shift_of_block; }
	void set_bit_shift_of_block(bool bit_shift) {
		bit_shift_of_block = bit_shift;
		if (arch_type & TB_TERNARY) {
			if (bit_shift_of_block)
				f_tern_in_byte = 5;
			else
				f_tern_in_byte = 1;
		}
	}
	void set_probe_one_exact(bool probe_one) {
		probe_one_exact = probe_one;
		if (probe_one_exact)
			set_buf_size(8);
		else
			set_buf_size(max_size_of_buf);
	}
	bool check_corrupted(unsigned long long piece_number);
	bool dont_care() {
		return arch_type & TB_DONT_CARE_BIT;
	}

	int get_size() {
		return sizeof(compressed_file_bufferizer) + buf_size + strlen(org_file_name)+1 + compression_opts_size + 
			(re_pair_decompr != NULL ? re_pair_decompr->get_size() : 0);
	}
	int get_size_with_src() {
		return get_size() +	(src_file != NULL ? src_file->get_size() : 0);
	}
};

extern unsigned long get_table_index(bool wtm);
extern bool cur_wtm;

void decompress_file(const char *filename, const char *output_filename = NULL); // creates <filename>.dec file

#endif /*EGDECOMPRESS_H_*/
