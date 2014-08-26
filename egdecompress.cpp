#include "egdecompress.h"
#ifdef USE_LZ4
#include "lz4/lz4.h"
#endif
#ifdef USE_LZHAM
#include "lzhamdecomp/lzham_decomp.h"
#endif
#include "egmaintypes.h" // here TB_LZMA_SIMPLE is defined
#include "egtbfile.h"
#include "egpglobals.h" // big_endian()
#ifdef LOMONOSOV_FULL
#include "lzma/LzmaLib.h"
#include "egindex.h"
#include "egglobals.h"
#include "tern_nota.h"
#endif
#include <time.h>
#include <assert.h>

//#define PRINT_WL_WITH_ERROR

#define INITIAL_PIECE_NUMBER 0xffffffffffffffffLL

using namespace Re_pair;

unsigned short read_short_LE(unsigned char *buf) {
	return (unsigned short)buf[0]+((unsigned short)buf[1] << 8);
}

unsigned int read_int_LE(unsigned char *buf) {
	unsigned int res;

	res = (unsigned int)buf[0] + ((unsigned int)buf[1] << 8) + ((unsigned int)buf[2] << 16) + ((unsigned int)buf[3] << 24);
	return res;
}

unsigned long long read_int64_LE(unsigned char *buf) {
	return (unsigned long long)read_int_LE(buf)+((unsigned long long)read_int_LE(buf+4) << 32);
}

#ifdef LOMONOSOV_FULL
#include "md5.h"
bool check_md5(unsigned char *data, unsigned int size, unsigned char *digest) {
	unsigned char mdbuf[MD5_DIGEST_LENGTH];

	MD5Digest(data, size, mdbuf);
	return memcmp(digest, mdbuf, MD5_DIGEST_LENGTH) == 0;
}
#endif

#ifdef USE_LZHAM
lzham_decompress_state_ptr lzham_state = NULL;
lzham_decompress_params *lzham_params = NULL;
#endif

bool compressed_file_bufferizer::new_src_file(unsigned int file_number) {
	char *newfilename = (char*)malloc(strlen(org_file_name)+20);
	if (file_number)
		sprintf(newfilename,"%s.%d", org_file_name, file_number);
	else
		strcpy(newfilename, org_file_name);
	src_file = new read_file_bufferizer();
	if (!src_file->begin_read(newfilename, 0, FB_TO_THE_END)) {
		//printf("\ncompressed_file_bufferizer::new_src_file cannot open file %s!\n", newfilename);
		free(newfilename);
		delete src_file;
		src_file = NULL;
		return false;
	}
	free(newfilename);
	load_virtual_files_shift();
	return true;
}

void compressed_file_bufferizer::calc_virtual_file_number() {
	unsigned char start_file = 0;
	unsigned char end_file = virtual_files_count;
	while (end_file - start_file > 1) {
		current_virtual_file_number = (start_file + end_file) / 2;
		if (virtual_files_shift[current_virtual_file_number] > cur_file_pos_read) {
			end_file = current_virtual_file_number;
		} else
			start_file = current_virtual_file_number;
	}
	current_virtual_file_number = start_file;
}

// attention! We must have correct src_file!
void compressed_file_bufferizer::load_virtual_files_shift() {
	if (opened_ && (arch_type & TB_FIX_COMP_SIZE) && (f_men_count >= 7 || !(arch_type & TB_TERNARY))) {
		unsigned int pcs_table_size;
		if (current_file_number < files_count - 1)
			pcs_table_size = pieces_per_file;
		else
			pcs_table_size = pieces_in_last_file;
		virtual_files_count = (pcs_table_size + VIRTUAL_FILE_BLOCKS_COUNT - 1) / VIRTUAL_FILE_BLOCKS_COUNT;
		// read from file
		if (virtual_files_count > 1) {
			src_file->set_buf_size(128);
			if (!current_file_number) src_file->seek(header_size);
			else src_file->seek(0);
			if (not_caching && virtual_files_shift)
				free(virtual_files_shift);
			virtual_files_shift = (file_offset *)malloc((virtual_files_count + 1) * sizeof(file_offset));
			file_offset *tmp = (file_offset *)malloc((virtual_files_count - 1) * sizeof(file_offset));
			src_file->read((char *)tmp, (virtual_files_count - 1) * sizeof(file_offset));
			virtual_files_shift[0] = file_starts[current_file_number];
			for (int i = 1; i < virtual_files_count; i++)
				virtual_files_shift[i] = file_starts[current_file_number] + read_int64_LE((unsigned char *)&tmp[i - 1]);
			virtual_files_shift[virtual_files_count] = file_starts[current_file_number + 1];
			free(tmp);
		}
	}
}

unsigned int compressed_file_bufferizer::load_piece_table() {
	unsigned long long start_piece, total_pieces;
	unsigned int pcs_table_size, i;
	// how many pieces does the current file contain?
	if (arch_type & TB_FIX_COMP_SIZE) {
		if (current_file_number < files_count - 1)
			pcs_table_size = pieces_per_file;
		else
			pcs_table_size = pieces_in_last_file;
	} else {
		start_piece = current_file_number * pieces_per_file;
		total_pieces = (total_file_length-1) / uncompr_piece_size +1;
		if (start_piece+pieces_per_file <= total_pieces) pcs_table_size = pieces_per_file;
		else pcs_table_size = total_pieces - start_piece;
	}

	// read from file
	choose_src_file();
	src_file->set_buf_size(1 << 18);
	file_offset seek_to = 0;
	if (!current_file_number)
		seek_to += header_size;
	if (virtual_files_count > 1) {
		seek_to += (virtual_files_count - 1) * sizeof(file_offset) + (VIRTUAL_FILE_BLOCKS_COUNT*sizeof_ps) * current_virtual_file_number;
		if (current_virtual_file_number == virtual_files_count - 1)
			pcs_table_size -= VIRTUAL_FILE_BLOCKS_COUNT * current_virtual_file_number;
		else
			pcs_table_size = VIRTUAL_FILE_BLOCKS_COUNT;
	}

	unsigned int *piece_sizes = (unsigned int*)malloc(pcs_table_size*sizeof(unsigned int));
	if (piece_offsets == NULL)
		piece_offsets = (file_offset *)malloc((pcs_table_size+1)*sizeof(file_offset));

	src_file->seek(seek_to);
	src_file->read((char*)piece_sizes, pcs_table_size*sizeof_ps);

#ifdef LOMONOSOV_FULL
	if (arch_type & TB_MD5_PROTECTED) {
		unsigned char digest[MD5_DIGEST_LENGTH];
		src_file->read((char*)digest, MD5_DIGEST_LENGTH);
		if (!check_md5((unsigned char*)piece_sizes, pcs_table_size*sizeof_ps, digest)) {
			printf("\ndecompressor: file %s, part %d, pieces table is corrupted!\n", org_file_name, current_file_number);
			ABORT(15);
		}
	}
#endif
	piece_offsets[0] = src_file->current_file_pos();
	if (!not_caching) {
		mutex_lock(read_bufferizer_mutexer);
		src_file->unlock();
		mutex_unlock(read_bufferizer_mutexer);
		src_file = NULL;
	}

	bool buf_size_changed = false;
	for (int j = pcs_table_size-1; j >= 0; j--) { // reverse order - for compilers where sizeof(unsigned int)>4
		if (sizeof_ps == 4) piece_sizes[j] = read_int_LE((unsigned char*)piece_sizes + j*sizeof_ps);
		else if (sizeof_ps == 2) piece_sizes[j] = read_short_LE((unsigned char*)piece_sizes + j*sizeof_ps);
		else if (sizeof_ps == 1) piece_sizes[j] = ((unsigned char *)piece_sizes)[j];
		else {
			printf("\ndecompressor: Unknown sizeof piece size %d\n", sizeof_ps);
			ABORT(15);
		}
		if (!(arch_type & TB_FIX_COMP_SIZE) && piece_sizes[j] > uncompr_piece_size) {
			printf("\ndecompressor: piece_size (%d) > uncompr.size (%d), file %s, part %d ! Probably corrupted file.\n",
					piece_sizes[j], uncompr_piece_size, org_file_name, current_file_number);
			ABORT(15);
		}
		if ((arch_type & TB_FIX_COMP_SIZE) && max_size_of_buf < piece_sizes[j]) {
			max_size_of_buf = piece_sizes[j];
			buf_size_changed = true;
		}
	}
	if (arch_type & TB_FIX_COMP_SIZE)
		piece_offsets[0] = file_starts[current_file_number];
	if (virtual_files_count > 1)
		piece_offsets[0] = virtual_files_shift[current_virtual_file_number];
	for (i = 1; i <= pcs_table_size; i++)
		piece_offsets[i] = piece_offsets[i-1] + piece_sizes[i-1];
	piece_table_loaded = true;
	free(piece_sizes);
	if (buf_size_changed && !probe_one_exact) {
		set_buf_size(max_size_of_buf);
	}
	return pcs_table_size+1;
}

void compressed_file_bufferizer::choose_src_file() {
	if (!src_file) {
		if (!new_src_file(current_file_number)) {
			char *newfilename;
			newfilename = (char*)malloc(strlen(org_file_name)+20);
			if (current_file_number)
				sprintf(newfilename,"%s.%d", org_file_name, current_file_number);
			else
				strcpy(newfilename, org_file_name);
			printf("\ncompressed_read: cannot open file %s!\n", newfilename);
			free(newfilename);
			ABORT(15);
		}
	}
}

unsigned long long compressed_file_bufferizer::get_piece_number() {
	unsigned int file_number = 0;
	unsigned long long piece_number = 0;
	if (arch_type & TB_FIX_COMP_SIZE) {
		unsigned int start_file = 0;
		unsigned int end_file = files_count;
		while (end_file - start_file > 1) {
			file_number = (start_file + end_file) / 2;
			if (file_starts[file_number] > cur_file_pos_read) {
				end_file = file_number;
			} else
				start_file = file_number;
		}
		file_number = start_file;
	} else {
		piece_number = cur_file_pos_read / uncompr_piece_size;
		file_number = piece_number / pieces_per_file;
		piece_number -= file_number * pieces_per_file;
	}
	if (!not_caching) {
		src_file = NULL;
	}
	if (src_file && not_caching && file_number != current_file_number) {
		delete src_file;
		src_file = NULL;
	}
	if (file_number != current_file_number) {
		current_file_number = file_number;
		piece_table_loaded = false;
		choose_src_file(); // virtual_files_shift loaded here
		if (!not_caching) {
			mutex_lock(read_bufferizer_mutexer);
			src_file->unlock();
			mutex_unlock(read_bufferizer_mutexer);
			src_file = NULL;
		}
	}
	unsigned char old_virtual_file_number = current_virtual_file_number;
	calc_virtual_file_number();
	if (current_virtual_file_number != old_virtual_file_number)
		piece_table_loaded = false;
	// 1) src_file works on a file that contains needed piece
	// 2) if piece_table_loaded = false, then we are looking at the beginning of pieces table
	if (!piece_table_loaded) {
		pieces_in_file = load_piece_table() - 1;
	}

	// detemine piece number
	if (arch_type & TB_FIX_COMP_SIZE) {
		unsigned long long start_piece = 0;
		unsigned long long end_piece = pieces_in_file;
		while (end_piece - start_piece > 1) {
			piece_number = (start_piece + end_piece) / 2;
			if (piece_offsets[piece_number] > cur_file_pos_read)
				end_piece = piece_number;
			else
				start_piece = piece_number;
		}
		piece_number = start_piece;
	}
	return piece_number;
}

size_t compressed_file_bufferizer::read_compressed_buffer(char **compressed_buffer, size_t *comp_size, unsigned long long *piece_number) {
	if (*piece_number == INITIAL_PIECE_NUMBER) *piece_number = get_piece_number();
	
	// now read needed piece and decompress it
	unsigned int piece_size = 0;
	unsigned int compr_size = 0;
	if (arch_type & TB_FIX_COMP_SIZE) {
		piece_size = uncompr_piece_size;
		compr_size = piece_size;
	} else {
		piece_size = piece_offsets[*piece_number+1] - piece_offsets[*piece_number];
		compr_size = piece_size;
		if ((arch_type & TB_COMP_METHOD_MASK) == TB_RE_PAIR)
			compr_size = ((piece_size + 7) >> 3) << 3;
	}
	char *compr_buffer = (char*)malloc(compr_size);
	if (compressed_buffer == NULL) {
		printf("\ndecompressor: cannot allocate memory for compressed_buffer! file = %s, part %s, piece_size = %d\n",
			org_file_name, src_file->read_file_name, piece_size);
		ABORT(15);
	}
	choose_src_file();
	if (arch_type & TB_FIX_COMP_SIZE)
		src_file->set_buf_size(buf_size);
	else
		src_file->set_buf_size(buf_size / 8);
	if (arch_type & TB_FIX_COMP_SIZE)
		src_file->seek(get_header_size() + (*piece_number + (unsigned long long)current_virtual_file_number * VIRTUAL_FILE_BLOCKS_COUNT) * uncompr_piece_size);
	else
		src_file->seek(piece_offsets[*piece_number]);
	src_file->read(compr_buffer, piece_size);
	if (!not_caching) {
		mutex_lock(read_bufferizer_mutexer);
		src_file->unlock();
		mutex_unlock(read_bufferizer_mutexer);
		src_file = NULL;
	}
	
	if ((arch_type & TB_COMP_METHOD_MASK) == TB_RE_PAIR && !is_big_endian()) {
		char tmp;
		for (size_t i = 0; i < piece_size; i += 8) {
			tmp = compr_buffer[i];
			compr_buffer[i] = compr_buffer[i+7];
			compr_buffer[i+7] = tmp;
			tmp = compr_buffer[i+1];
			compr_buffer[i+1] = compr_buffer[i+6];
			compr_buffer[i+6] = tmp;
			tmp = compr_buffer[i+2];
			compr_buffer[i+2] = compr_buffer[i+5];
			compr_buffer[i+5] = tmp;
			tmp = compr_buffer[i+3];
			compr_buffer[i+3] = compr_buffer[i+4];
			compr_buffer[i+4] = tmp;
		}
	}

	size_t org_size = 0;
	if (arch_type & TB_FIX_COMP_SIZE) {
		org_size = piece_offsets[(*piece_number) + 1] - piece_offsets[*piece_number];
	} else {
		org_size = uncompr_piece_size;
		// if it's the last piece, it may have different size
		file_offset piece_start_offset = (*piece_number + pieces_per_file * current_file_number) * uncompr_piece_size;
		if (piece_start_offset + uncompr_piece_size >= total_file_length)
			org_size = total_file_length - piece_start_offset;
	}
#ifdef LOMONOSOV_FULL
	if (arch_type & TB_MD5_PROTECTED) {
		piece_size -= MD5_DIGEST_LENGTH;
		if (!check_md5((unsigned char*)compr_buffer, piece_size, (unsigned char*)compr_buffer + piece_size)) {
			printf("\ndecompressor: file %s, part %s, piece %lld is corrupted!\n", org_file_name, src_file->read_file_name, piece_number);
			ABORT(15);
		}
	}
#endif
	*comp_size = compr_size;
	*compressed_buffer = compr_buffer;
	return org_size;
}

void compressed_file_bufferizer::read_buffer() {
	unsigned int i;
	char *compressed_buffer = NULL;
	int lzma_res;
	size_t org_size, comp_size, lzham_in, lzham_out;
#ifdef USE_LZHAM
	lzham_decompress_status_t lzham_decomp_state;
#endif
	char t;

	if (uncompressed_mode) {
		read_file_bufferizer::read_buffer();
		return;
	}
	if (cur_file_pos_read >= end_file_pos) return;

	unsigned long long piece_number = INITIAL_PIECE_NUMBER;
	if ((org_size = read_compressed_buffer(&compressed_buffer, &comp_size, &piece_number)) == 0)
		return;

	file_offset piece_start_offset = (arch_type & TB_FIX_COMP_SIZE) ? piece_offsets[piece_number] : (piece_number * uncompr_piece_size);
	
	switch (arch_type & TB_COMP_METHOD_MASK) {
#ifdef LOMONOSOV_FULL
	case TB_LZMA_SIMPLE:
		lzma_res = LzmaUncompress((unsigned char*)buffer, &org_size, (unsigned char*)compressed_buffer, &comp_size,
			compression_opts, compression_opts_size);
		if (lzma_res != SZ_OK) {
			printf("\nError in LzmaUncompress! File = %s, piece = %lld, code = %d, cur_file_pos_read=%lld, org_size=%lu, comp_size=%lu\n",
				org_file_name, cur_file_pos_read / uncompr_piece_size, lzma_res, cur_file_pos_read, org_size, comp_size);
			free(compressed_buffer);
			ABORT(15);
		}
		break;
#endif
#ifdef USE_LZHAM
	case TB_LZHAM_SIMPLE:
		lzham_state = lzham::lzham_lib_decompress_reinit(lzham_state, lzham_params);
		lzham_in = comp_size;
		lzham_out = org_size;
		lzham_decomp_state = lzham::lzham_lib_decompress(lzham_state, (unsigned char*)compressed_buffer, &lzham_in, (unsigned char*)buffer, &lzham_out, true);
		break;
#endif
#ifdef USE_LZ4
	case TB_LZ4_SIMPLE:
	case TB_LZ4_HC:
		if (LZ4_uncompress((char*)compressed_buffer, (char*)buffer, org_size) <= 0) {
			printf("\nError in Lz4Uncompress! File = %s, piece = %lld, code = %d, cur_file_pos_read=%lld, org_size=%lu\n",
				org_file_name, cur_file_pos_read / uncompr_piece_size, lzma_res, cur_file_pos_read, org_size);
			free(compressed_buffer);
			ABORT(15);
		}
		break;
#endif
	case TB_RE_PAIR:
		if ((!probe_one_exact && !re_pair_decompr->DecompressPiece(compressed_buffer, buffer, org_size)) ||
		(probe_one_exact && !re_pair_decompr->QuickDecompressPiece(compressed_buffer, buffer, cur_file_pos_read - piece_start_offset))) {
			printf("\nError in re_pair_decompr->DecompressPiece! File = %s, piece = %lld, cur_file_pos_read = %lld, org_size = %lu\n",
				org_file_name, cur_file_pos_read / uncompr_piece_size, cur_file_pos_read, org_size);
			free(compressed_buffer);
		}
		break;
	}
	free_compressed_buffer(compressed_buffer);
	if (!(arch_type & TB_BINARY) && !(arch_type & TB_TERNARY) && !(arch_type & TB_DTZ50) && !(arch_type & TB_PERMUTATIONS) && !is_big_endian()) { // LM: flip
		for (i = 0; i < org_size; i += 2) {
			t = buffer[i];
			buffer[i] = buffer[i+1];
			buffer[i+1] = t;
		}
	}
	buf_pos = cur_file_pos_read - piece_start_offset;
	bytes_in_buffer = org_size;

#ifdef LOMONOSOV_FULL
	if ((arch_type & TB_TERNARY) && (arch_type & TB_COMP_METHOD_MASK) == TB_RE_PAIR && bit_shift_of_block) {
		int8_t tern[5] = {0, 0, 0, 0, 0}, tern_count = 0;
		for (i = 0; i < org_size; i++) {
			tern[tern_count] = buffer[i];
			--tern[tern_count];
			tern_count++;
			if (tern_count == 5) {
				buffer[i / 5] = ternary_notation::tern_to_byte(tern);
				tern_count = 0;
			}
		}
		if (tern_count > 0) {
			while (tern_count < 5) {
				tern[tern_count] = 0;
				tern_count++;
			}
			buffer[org_size / 5] = ternary_notation::tern_to_byte(tern);
		}
		buf_pos = buf_pos / 5;
		bytes_in_buffer = (bytes_in_buffer + 4) / 5;
	}
#endif

	cur_file_pos_read += org_size - buf_pos;

#ifdef LOMONOSOV_FULL
	if (arch_type & TB_BINARY) {
	    if (bit_shift_of_block) {
	        unsigned char offs;
	        long tbind;

	        // todo: implement for big-endians?
	        assert(!is_big_endian());

	        if (bits_for_record < 8) { // if bits_for_record = 8, don't convert
	            unsigned int buf = 0, *uint32_buffer = (unsigned int*) buffer, j = 0;
	            unsigned char bit = 0, b;
	            for (i = 0; i < org_size; ++i) {
	                b = buffer[i];
	                buf |= b << bit;
	                bit += bits_for_record;
	                if (bit >= sizeof(unsigned int) * 8) {
	                    uint32_buffer[j++] = buf;
	                    bit &= 0x1f;
	                    buf = b >> (bits_for_record - bit);
	                }
	            }
	            if (bit) uint32_buffer[j] = buf;
	        } else if (bits_for_record > 8 && bits_for_record < 16) {

	            // todo: implement for big-endians?
	            assert(!is_big_endian());

	            unsigned int buf = 0, *uint32_buffer = (unsigned int*) buffer, j = 0;
	            unsigned short *word_buffer_read = (unsigned short*) buffer, b;
	            unsigned char bit = 0;
	            for (i = 0; i < (org_size >> 1); ++i) {
	                b = word_buffer_read[i];
	                buf |= b << bit;
	                bit += bits_for_record;
	                if (bit >= sizeof(unsigned int) * 8) {
	                    uint32_buffer[j++] = buf;
	                    bit &= 0x1f;
	                    buf = b >> (bits_for_record - bit);
	                }
	            }
	            if (bit) uint32_buffer[j] = buf;
	        } else if (bits_for_record > 16)
	            printf("bits_for_record for table is more than 16!\n");
	        if (bits_for_record <= 8) {
	            index_in_buffer = buf_pos;
	            buf_pos = (buf_pos * bits_for_record) >> 3;
	        } else {
	            index_in_buffer = buf_pos >> 1;
	            buf_pos = (buf_pos * bits_for_record) >> 4;
	            if (buf_pos & 1) --buf_pos;
	        }
	    } else { // no bit-shifting, beware of 8-bit tables at big-endians
	        if (is_big_endian() && bits_for_record > 8) { // ML stores 2-byte words in LE, flip it
	            for (i = 0; i < org_size; i += 2) {
	                t = buffer[i];
	                buffer[i] = buffer[i+1];
	                buffer[i+1] = t;
	            }
	        }

	    }
	}
#endif
}

#define CUR_FILE_POS_READ (tern_shift ? (cur_file_pos_read + 4) / 5 : cur_file_pos_read)

void compressed_file_bufferizer::seek(file_offset new_pos) {
	bool tern_shift = ((arch_type & TB_TERNARY) && (arch_type & TB_COMP_METHOD_MASK) == TB_RE_PAIR && bit_shift_of_block);
	if (!probe_one_exact && new_pos >= CUR_FILE_POS_READ - bytes_in_buffer && new_pos < CUR_FILE_POS_READ) {
		if (arch_type & TB_BINARY) {
			if (bits_for_record <= 8) {
				index_in_buffer = new_pos - cur_file_pos_read + bytes_in_buffer;
				buf_pos = (index_in_buffer * bits_for_record) >> 3;
			} else {
				index_in_buffer = (new_pos - cur_file_pos_read + bytes_in_buffer) >> 1;
				buf_pos = ((int) (new_pos - cur_file_pos_read + bytes_in_buffer) * bits_for_record) >> 4;
				if (buf_pos & 1) --buf_pos;
			}
		} else
			buf_pos = bytes_in_buffer - (CUR_FILE_POS_READ - new_pos);
	} else { // clear buffer
#ifndef WIN32_FILE
		if (write_mode) flush();
#endif
		buf_pos = bytes_in_buffer = 0;
		cur_file_pos_read = cur_file_pos_write = new_pos * (tern_shift ? 5 : 1);
		index_in_buffer = 0;
	}
}

tbfile_entry compressed_file_bufferizer::get_value(TBINDEX tbind) {
	TBINDEX index;
	unsigned char offset;
	tbfile_entry value = 0;

	index = tbind;
	if ((bits_for_record > 8 || !(arch_type & TB_VERSION_TWO)) && !(arch_type & TB_DTZ50))
		index <<= 1;
	seek(index);

	if (!(arch_type & TB_VERSION_TWO)) {
		if (arch_type & TB_DTZ50) {
			unsigned char tbe;
			read((char *)&tbe, sizeof(unsigned char));
			return tbe;
		} else {
			unsigned short tbe;
			read((char *)&tbe, sizeof(unsigned short));
			return tbe;
		}
	}

#ifdef LOMONOSOV_FULL
	// TB_BINARY
	if (!bit_shift_of_block) {
		if (bits_for_record <= 8) {
			unsigned char tbe;
			read((char *)&tbe, sizeof(unsigned char));
			value = unshift_binary_value(tbe, offset_value);
		}
		else {
			unsigned short tbe;
			read((char *)&tbe, sizeof(unsigned short));
			value = unshift_binary_value(tbe, offset_value);
		}
		return value;
	}

	// todo: implement for big-endians?
	assert(!is_big_endian());

	// bit_shift_of_block = true
	if (bits_for_record <= 8) {
		unsigned char tbe;
		read((char *)&tbe, sizeof(unsigned char));
		offset = get_byte_binary_offset(index_in_buffer, bits_for_record);
		if (offset + bits_for_record <= 8) {
			tbe >>= offset;
			tbe &= 0xFF >> (8 - bits_for_record);
			value = tbe;
		} else {
			unsigned char tbe2;
			read((char *)&tbe2, sizeof(unsigned char));
			value = (tbe2 << 8) | tbe;
			value >>= offset;
			value &= 0xFFFF >> (16 - bits_for_record);
		}
		value = unshift_binary_value(value, offset_value);
	} else {
		unsigned short tbe;
		read((char *)&tbe, sizeof(unsigned short));
		offset = get_word_binary_offset(index_in_buffer, bits_for_record);
		if (offset + bits_for_record <= 16) {
			tbe >>= offset;
			tbe &= 0xFFFF >> (16 - bits_for_record);
		} else {
			unsigned short tbe2;
			unsigned int tmp;
			read((char *)&tbe2, sizeof(unsigned short));
			tmp = (tbe2 << 16) | tbe;
			tmp >>= offset;
			tmp &= 0xFFFFFFFF >> (32 - bits_for_record);
			tbe = tmp & 0xFFFF;
		}
		value = unshift_binary_value(tbe, offset_value);
	}
#endif
	return value;
}

void compressed_file_bufferizer::read(char *data, unsigned long size) {
	if (probe_one_exact && size > 2) {
		printf("compressed_file_bufferizer::read: probe_one_exact = true and want to read %d!\n", size);
		ABORT(10);
	}
	unsigned long rd;
	do {
		if (buf_pos >= bytes_in_buffer) read_buffer();
		if (buf_pos+size > bytes_in_buffer) rd = bytes_in_buffer-buf_pos; else rd = size;
		if (rd) {
			if (probe_one_exact) {
				memcpy(data, buffer, size);
				rd = size;
			} else {
				memcpy(data, &(buffer[buf_pos]), rd);
				data += rd;
			}
			buf_pos += rd;
			size -= rd;
		}
	} while (size && rd);
}

tbfile_tern_entry compressed_file_bufferizer::get_ternary_value(TBINDEX tbind) {
	tbfile_ternary_index tern_ind = tbfile_index_to_ternary_index(tbind, f_tern_in_byte);
	tbfile_tern_entry tbe;
	
	seek(tern_ind.byte);
	read((char *)&tbe, sizeof(tbe));

	switch (f_tern_in_byte) {
#ifdef LOMONOSOV_FULL
	case 5:
		return ternary_notation::byte_to_tern(tbe)[tern_ind.offset];
	case 2:
		return two_ternary_notation::byte_to_tern(tbe)[tern_ind.offset];
#endif
	case 1:
		if ((arch_type & TB_COMP_METHOD_MASK) == TB_RE_PAIR) {
			--tbe;
		}
		return tbe;
	}
}

void compressed_file_bufferizer::get_range(TBINDEX tbind, TBINDEX *start, TBINDEX *end) {
	if ((arch_type & TB_COMP_METHOD_MASK) != TB_RE_PAIR || !(arch_type & TB_FIX_COMP_SIZE)) {
		printf("compressed_file_bufferizer::get_range: it's only for RE_PAIR & FIX_COMP_SIZE!\n");
		ABORT(1);
	}
	seek(tbind);
	if (cur_file_pos_read >= end_file_pos) {
		printf("compressed_file_bufferizer::get_range: cur_file_pos_read >= end_file_pos!\n");
		ABORT(1);
	}
	char *compressed_buffer = NULL;
	size_t org_size, comp_size;
	unsigned long long piece_number = INITIAL_PIECE_NUMBER;
	if ((org_size = read_compressed_buffer(&compressed_buffer, &comp_size, &piece_number)) == 0) {
		printf("compressed_file_bufferizer::get_range: org_size = 0!\n");
		ABORT(1);
	}
	file_offset piece_start_offset = piece_offsets[piece_number];
	size_t start_t, end_t;
	if (!re_pair_decompr->GetRange(compressed_buffer, cur_file_pos_read - piece_start_offset, &start_t, &end_t)) {
		printf("compressed_file_bufferizer::get_range: error if decompressing block!\n");
		ABORT(1);
	}
	free_compressed_buffer(compressed_buffer);
	*start = piece_start_offset + start_t;
	*end = piece_start_offset + end_t;
}

bool compressed_file_bufferizer::begin_read(const char *filename, file_offset start_pos, file_offset length) {
#define CFB_MAX_HEADER_SIZE 256
	unsigned char buf[CFB_MAX_HEADER_SIZE];
	int buf_write_offset, buf_read_offset;
	write_mode = false;
	buf_write_offset = 0;
	buf_read_offset = 0;

	f_men_count = get_men_count(filename);

	assert(opened_ == false); // do not call begin_read twice as the resources are not deallocated until destruction
	org_file_name = (char*)malloc(strlen(filename)+1);
	strcpy(org_file_name, filename);
	current_file_number = 0;
	if (!new_src_file(0)) {
		free(org_file_name);
		org_file_name = NULL;
		return false;
	}
	free(org_file_name);
	org_file_name = NULL;
	src_file->set_buf_size(512);
	src_file->seek(0);

	src_file->read((char*)buf,4);
	buf_write_offset += 4;

	if (buf[0] != 'C' || buf[1] != 'o') {
		arch_type = 0;
		uncompressed_mode = true;
		delete src_file;
		src_file = NULL;
		return read_file_bufferizer::begin_read(filename,start_pos,length);
	}
	uncompressed_mode = false;
	buf_read_offset = 2;
	arch_type = read_short_LE(buf+buf_read_offset);
	buf_read_offset += 2;
	if ((arch_type & TB_COMP_METHOD_MASK) != TB_LZMA_SIMPLE
		&& (arch_type & TB_COMP_METHOD_MASK) != TB_LZ4_SIMPLE
		&& (arch_type & TB_COMP_METHOD_MASK) != TB_LZ4_HC
		&& (arch_type & TB_COMP_METHOD_MASK) != TB_LZHAM_SIMPLE
		&& (arch_type & TB_COMP_METHOD_MASK) != TB_RE_PAIR) {
			printf("\ncompressed_file_bufferizer::begin_read: unknown compress method, arch_type = %x\n", arch_type);
			return false;
	}
	bits_for_record = ((arch_type & TB_TERNARY) | (arch_type & TB_DTZ50)) ? 8 : 16;
	if ((arch_type & TB_TABLE_VERSION_MASK) == TB_VERSION_TWO) {
		src_file->read((char*)buf + buf_write_offset, 2);
		buf_write_offset += 2;
		arch_info = read_short_LE(buf+buf_read_offset);
		buf_read_offset += 2;
		bits_for_record = ((arch_info & TB_BINARY_BITS_MASK) >> 12) + 1;
		offset_value = arch_info & TB_BINARY_MIN_MASK;
	}
	src_file->read((char*)buf + buf_write_offset, 16);
	buf_write_offset += 16;
	uncompr_piece_size = read_int_LE(buf+buf_read_offset);
	buf_read_offset += 4;
	pieces_per_file = read_int_LE(buf+buf_read_offset);
	buf_read_offset += 4;
	total_file_length = read_int64_LE(buf+buf_read_offset);
	buf_read_offset += 8;
	// flip_bytes = ... ; // flipping is performed in read_buffer()
	if (arch_type & TB_PERMUTATIONS) {
		src_file->read((char*)buf + buf_write_offset, 9);
		buf_write_offset += 9;
		memcpy(permutation_blocks, buf + buf_read_offset, 9);
		buf_read_offset += 9;
		// Convert in correct endian. In big-endian pieces_block.start - last 4 bits, pieces_blocks.length - first 4 bits.
		if (is_big_endian()) {
			char tmp;
			for (char i = 0; i < 8; i++) {
				tmp = permutation_blocks[i].start;
				permutation_blocks[i].start = permutation_blocks[i].length;
				permutation_blocks[i].length = tmp;
			}
		}
	}
	if (arch_type & TB_FIX_COMP_SIZE) {
		src_file->read((char *)buf + buf_write_offset, 4);
		buf_write_offset += 4;
		pieces_in_last_file = read_int_LE(buf + buf_read_offset);
		buf_read_offset += 4;
		src_file->read((char *)buf + buf_write_offset, 4);
		buf_write_offset += 4;
		files_count = read_int_LE(buf + buf_read_offset);
		buf_read_offset += 4;
		file_starts = (TBINDEX *)malloc(sizeof(TBINDEX) * (files_count + 1));
		src_file->read((char *)buf + buf_write_offset, 8 * (files_count - 1));
		buf_write_offset += 8 * (files_count - 1);
		file_starts[0] = 0;
		file_starts[files_count] = total_file_length;
		for (int i = 1; i < files_count; i++) {
			file_starts[i] = read_int64_LE(buf + buf_read_offset);
			buf_read_offset += 8;
		}
	}
	// read size of piece size and PARAMS
	if ((arch_type & TB_COMP_METHOD_MASK) == TB_RE_PAIR) {
		src_file->read((char*)buf + buf_write_offset, 1);
		buf_write_offset += 1;
		sizeof_ps = buf[buf_read_offset];
		buf_read_offset += 1;
		if (arch_type & TB_RE_PAIR_PARAMS) {
			src_file->read((char *)buf + buf_write_offset, 2);
			buf_write_offset += 2;
			primary_dc_change = buf[buf_read_offset];
			dc_lower_bound = buf[buf_read_offset+1];
			buf_read_offset += 2;
		}
	}
	src_file->read((char*)buf + buf_write_offset, 2);
	buf_write_offset += 2;
	compression_opts_size = read_short_LE(buf+buf_read_offset);
	buf_read_offset += 2;
	bool compr_re_pair_header = true;
	if (compression_opts_size) {
		if (compression_opts_size & RE_PAIR_NOTCOMPR_HEADER_FLAG) {
			if ((arch_type & TB_COMP_METHOD_MASK) != TB_RE_PAIR) {
				printf("\ncompressed_file_bufferizer::begin_read: very big compression_opts_size = %d", compression_opts_size);
				ABORT(15);
			}
			compression_opts_size &= RE_PAIR_HEADER_SIZE_MASK;
			compr_re_pair_header = false;
		}
		compression_opts = (unsigned char*)malloc(compression_opts_size);
		src_file->read((char*)compression_opts, compression_opts_size);
	}
	if (full_begin_read && (arch_type & TB_COMP_METHOD_MASK) == TB_RE_PAIR) {
		if (compr_re_pair_header) {
#ifdef LOMONOSOV_FULL
			if (false) {
				// decompress dictionary
				unsigned char uncompr_buf[MAX_DICT_SIZE*5+1];
				size_t uncompr_size = MAX_DICT_SIZE;
				size_t lzma_props_size = LZMA_PROPS_SIZE;
				size_t comp_size = compression_opts_size;
				unsigned char lzma_props[LZMA_PROPS_SIZE];
				lzma_props[0] = 0; lzma_props[1] = 0; lzma_props[2] = 0; lzma_props[3] = 16; lzma_props[4] = 0;
				int lzma_res = LzmaUncompress(uncompr_buf, &uncompr_size, compression_opts, &comp_size, lzma_props, lzma_props_size);
				if (lzma_res != SZ_OK && lzma_res != SZ_ERROR_INPUT_EOF) {
					printf("\ncompressed_file_bufferizer::begin_read: Error in LzmaUncompress of Re-Pair header! File = %s, code = %d, compr_size = %d\n",
						filename, lzma_res, compression_opts_size);
					ABORT(15);
				}
				free(compression_opts);
				compression_opts = NULL;
				size_t dict_size = uncompr_size / 5;
				// change bytes if big-endian
				if (sizeof(Symbol) == 2 && is_big_endian()) {
					unsigned char tmp;
					for (size_t i = 0; i < dict_size * 2; i++) {
						tmp = uncompr_buf[i*2];
						uncompr_buf[i*2] = uncompr_buf[i*2 + 1];
						uncompr_buf[i*2 + 1] = tmp;
					}
				}
				re_pair_decompr = new RePairDecompressor((char *)uncompr_buf, (char *)uncompr_buf + dict_size * 4, dict_size, bits_for_record == 8);
			} else {
				free(compression_opts);
				compression_opts = NULL;
			}
#else
			printf("\ncompressed_file_bufferizer::begin_read: cannot decompress re_pair_header");
			ABORT(15);
#endif
		} else {
			if (sizeof(Symbol) != 2) {
				printf("\ncompressed_file_bufferizer::begin_read: decompressor isn't able work with %d-bytes Symbol", sizeof(Symbol));
				ABORT(15);
			}
			unsigned char huffman_lengths[MAX_DICT_SIZE];
			uint16_t meanings[MAX_DICT_SIZE*2];
			size_t dict_size = read_short_LE(compression_opts);
			int rd_byte = 2, rd_bit = 0;
			int tmp_size = dict_size - 1, bits = 0, remain_bits, offset;
			// determine bits count
			while (tmp_size > 0) {
				tmp_size >>= 1;
				++bits;
			}
			uint16_t value;
			unsigned char byte_mask = 0xff;
			for (size_t i = 0; i < dict_size*3; i++) {
				value = 0;
				if (i < dict_size*2)
					remain_bits = bits;
				else
					remain_bits = 6;
				while (remain_bits > 0) {
					offset = 8 - rd_bit;
					// read from byte
					if (offset > remain_bits) {
						value = (value << remain_bits) | ((compression_opts[rd_byte] & (byte_mask >> rd_bit)) >> (offset - remain_bits));
						rd_bit += remain_bits;
					} else {
						value = (value << offset) | (compression_opts[rd_byte] & (byte_mask >> rd_bit));
						rd_bit = 0;
						++rd_byte;
					}
					remain_bits -= offset;
				}
				if (i < dict_size*2)
					meanings[i] = ((value == 0 && (i & 1)) ? kInvalidSymbol : value);
				else
					huffman_lengths[i - dict_size*2] = value;
			}
			free(compression_opts);
			compression_opts = NULL;
			re_pair_decompr = new RePairDecompressor((char *)meanings, (char *)huffman_lengths, dict_size, bits_for_record == 8);
		}
	}
#ifdef LOMONOSOV_FULL
	if (arch_type & TB_MD5_PROTECTED) {
		unsigned char digest[MD5_DIGEST_LENGTH];
		if (compression_opts_size+buf_read_offset > CFB_MAX_HEADER_SIZE) {
			printf("\ncompressed_file_bufferizer: header is too large! file = %s\n", filename);
			ABORT(15);
		}
		memcpy(buf+buf_read_offset, compression_opts, compression_opts_size);
		src_file->read((char*)digest, MD5_DIGEST_LENGTH);
		if (!check_md5(buf, buf_read_offset+compression_opts_size, digest)) {
			printf("%s: header is corrupted!\n", filename);
			ABORT(15);
		}
	}
#endif
#ifdef LOMONOSOV_FULL
	if ((arch_type & TB_TERNARY) && (arch_type & TB_DONT_CARE_BIT) && one_tern_in_byte)
		f_tern_in_byte = 1;
	if ((arch_type & TB_TERNARY) && (arch_type & TB_DONT_CARE_BIT) && two_tern_in_byte)
		f_tern_in_byte = 2;
#endif
#ifdef USE_LZHAM
	if ((arch_type & TB_COMP_METHOD_MASK) == TB_LZHAM_SIMPLE && (!lzham_params || !lzham_state)) {
		if (lzham_params) delete lzham_params;
		lzham_params = new lzham_decompress_params;
		memset(lzham_params, 0, sizeof(lzham_decompress_params));
		lzham_params->m_struct_size = sizeof(lzham_decompress_params);
		lzham_params->m_dict_size_log2 = LZHAM_MIN_DICT_SIZE_LOG2;
		lzham_params->m_output_unbuffered = true;
		lzham_params->m_compute_adler32 = false;
		lzham_state = lzham::lzham_lib_decompress_init(lzham_params);
	}
#endif

	opened_ = true;
	header_size = src_file->current_file_pos();
	max_size_of_buf = uncompr_piece_size;
	buffer = (char*)malloc(uncompr_piece_size);
	buf_size = uncompr_piece_size;
	load_virtual_files_shift();
	add_virtual_info_to_cache();
	if (!not_caching) {
		mutex_lock(read_bufferizer_mutexer);
		src_file->unlock();
		mutex_unlock(read_bufferizer_mutexer);
		src_file = NULL;
	}
	bytes_in_buffer = 0;
	buf_pos = 0;
	cur_file_pos_read = start_pos;
	if (length != (file_offset)FB_TO_THE_END) end_file_pos = start_pos + length;
		else end_file_pos = total_file_length;
	piece_table_loaded = false;
	if (!(arch_type & TB_FIX_COMP_SIZE)) {
		if (total_file_length / uncompr_piece_size + 1 < pieces_per_file)
			pieces_per_file = total_file_length / uncompr_piece_size + 1;
	} else if (files_count == 1)
		pieces_per_file = pieces_in_last_file;
	//piece_sizes = (unsigned int*)malloc(pieces_per_file*sizeof(unsigned int));
	//if (not_caching)
		//piece_offsets = (file_offset *)malloc((pieces_per_file+1)*sizeof(file_offset));
	current_file_number = 0;
	org_file_name = (char*)malloc(strlen(filename)+1);
	strcpy(org_file_name, filename);
	// no_file = false; // it is used in destructor of base class
	return true;
}

bool compressed_file_bufferizer::check_corrupted(unsigned long long piece_number) {
	unsigned long long start_piece, total_pieces;
	unsigned int file_number, pcs_table_size, i;
	char *newfilename, *compressed_buffer;
	int lzma_res, j;
	size_t org_size, comp_size, lzham_in, lzham_out;
	file_offset piece_start_offset;
#ifdef USE_LZHAM
	lzham_decompress_status_t lzham_decomp_state;
#endif
	char t;

	if (uncompressed_mode) {
		return false;
	}

	piece_start_offset = piece_number * uncompr_piece_size;
	file_number = piece_number / pieces_per_file;
	piece_number -= file_number * pieces_per_file;
	if (src_file && file_number != current_file_number) {
		if (not_caching)
			delete src_file;
		src_file = NULL;
	}
	if (src_file && !src_file->trylock()) {
		src_file = NULL;
	}
	if (!src_file) {
		if (!new_src_file(file_number)) {
			printf("\ncompressed_read: cannot open file %s!\n", newfilename);
			ABORT(15);
			return false;
		}
		if (file_number != current_file_number) {
			if (!file_number) src_file->seek(header_size);
			else src_file->seek(0);
			piece_table_loaded = false;
			current_file_number = file_number;
		}
	}
	// 1) src_file works on a file that contains needed piece
	// 2) if piece_table_loaded = false, then we are looking at the beginning of pieces table
	if (!piece_table_loaded) {
		load_piece_table();
	}

	unsigned int piece_size = piece_offsets[piece_number+1] - piece_offsets[piece_number];
	compressed_buffer = (char*)malloc(piece_size);
	if (compressed_buffer == NULL) {
		printf("decompressor: cannot allocate memory for compressed_buffer! file = %s, part %d, piece_size = %d\n",
				org_file_name, file_number, piece_size);
		ABORT(15);
	}
	src_file->seek(piece_offsets[piece_number]);
	src_file->read(compressed_buffer, piece_size);
	if (!not_caching) {
		mutex_lock(read_bufferizer_mutexer);
		src_file->unlock();
		mutex_unlock(read_bufferizer_mutexer);
	}

	comp_size = piece_size;

#ifdef LOMONOSOV_FULL
	if (arch_type & TB_MD5_PROTECTED) {
		comp_size -= MD5_DIGEST_LENGTH;
		if (!check_md5((unsigned char*)compressed_buffer, comp_size, (unsigned char*)compressed_buffer + comp_size)) {
			printf("decompressor: file %s, part %d, piece %lld is corrupted!\n", org_file_name, file_number, piece_number);
			free(compressed_buffer);
			return true;
		}
	}
#endif
	free(compressed_buffer);
	return false;
}

#define DEC_BUF_SIZE (1<<21) // 2 MB

void decompress_file(const char *filename, const char *output_filename) {
	char *buf, newfilename[200];
	file_offset unpacked;
	unsigned int rd;
	int out_file, cnt; //, i;
	compressed_file_bufferizer fb;
	//char t;

	if (!output_filename) {
		sprintf(newfilename, "%s.dec", filename);
		output_filename = newfilename;
	}
	out_file = open(output_filename, O_WRONLY | O_CREAT | O_TRUNC | O_BINARY, 0666);
	if (out_file < 0) {
		printf("\nCannot write to file %s\n", output_filename);
		exit(16);
	}
	if (!fb.begin_read(filename, 0, FB_TO_THE_END)) {
		printf("\nCannot read from file %s\n", filename);
		exit(16);
	}
	buf = (char*)malloc(DEC_BUF_SIZE);
	unpacked = 0;
	cnt = 0;
#ifndef LOMONOSOV_FULL
	bool compress_show_display = true;
#endif
	while (unpacked < fb.total_file_length) {
		if (compress_show_display && !(cnt & 0x3f))
			printf("\rDecompressing %s: %.1f%%", filename, unpacked*100.0/fb.total_file_length);
		if (unpacked + DEC_BUF_SIZE <= fb.total_file_length) rd = DEC_BUF_SIZE;
		else rd = fb.total_file_length - unpacked;
		fb.read(buf, rd);
		/*if (!is_big_endian()) {
			for (i = 0; i < (rd >> 1); i++) {
				t = buf[i << 1]; buf[i << 1] = buf[(i << 1) + 1]; buf[(i << 1) + 1] = t;
			}
		}*/
		write(out_file, buf, rd);
		unpacked += rd;
		cnt++;
	}
	close(out_file);
	free(buf);
	if (compress_show_display) printf("\n");
}
