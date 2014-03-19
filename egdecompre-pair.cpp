#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <queue>
#include "eginttypes.h"
#include "egdecompre-pair.h"

namespace Re_pair {

void HuffmanCode::BuildCanonicalCode() {
	length_base_code_.resize(kMaxHuffmanBits+1);
	length_base_canonical_symbol_.resize(kMaxHuffmanBits+1);

	std::vector<size_t> num_of_lengths(kMaxHuffmanBits+1, 0);
	for (size_t i = 0; i < num_symbols_; i++)
		num_of_lengths[code_length_[i]]++;
	length_base_canonical_symbol_[1] = num_of_lengths[0];
	length_base_code_[1] = 0; // 00000...
#ifdef RE_PAIR_VERBOSE
		printf("Huffman lengths count:");
#endif
	for (size_t i = 2; i <= kMaxHuffmanBits; i++) {
		length_base_canonical_symbol_[i] = length_base_canonical_symbol_[i-1] + num_of_lengths[i-1];
		length_base_code_[i] = (length_base_code_[i-1] + static_cast<HuffmanBits>(num_of_lengths[i-1]))	<< 1;
#ifdef RE_PAIR_VERBOSE
		printf(" %d", num_of_lengths[i-1]);
#endif
	}
#ifdef RE_PAIR_VERBOSE
		printf(".\n");
#endif

	// now sort symbols of the same length and fill the map
	sym_to_canonical_map_.resize(num_symbols_);
	std::priority_queue<HuffmanCodeItem> canonical_items;
	for (size_t i = 0; i < num_symbols_; i++)
		canonical_items.push(HuffmanCodeItem(i, code_length_[i]));
	for (size_t i = 0; i < num_symbols_; i++) {
		HuffmanCodeItem it = canonical_items.top();
		canonical_items.pop();
		sym_to_canonical_map_[it.symbol()] = i;
	}
}

#ifdef _M_X64
#define BITS_PER_STEP 64
#define BITS_PER_STEP_MASK 0x3f
#else
#define BITS_PER_STEP 32
#define BITS_PER_STEP_MASK 0x1f
#endif

void HuffmanCodeDecompressor::BuildFromCodeLength(const char *code_length, size_t num_symbols) {
	num_symbols_ = num_symbols;
	code_length_.resize(num_symbols_);
	min_len = 64;
	max_len = 0;
	for (size_t i = 0; i < num_symbols_; i++) {
		code_length_[i] = code_length[i];
		if (code_length_[i] > 0 && code_length_[i] < min_len)
			min_len = code_length_[i];
		if (code_length_[i] > max_len)
			max_len = code_length_[i];
	}
	BuildCanonicalCode();

	// For decompress (length_base_code_, length_base_canonical_symbol_, canonical_to_sym_map_) only are needed
	canonical_to_sym_map_.resize(num_symbols_);
	for (size_t i = 0; i < num_symbols_; i++)
		canonical_to_sym_map_[sym_to_canonical_map_[i]] = i;
	code_length_.clear();
	code_length_.resize(0);
	sym_to_canonical_map_.clear();
	sym_to_canonical_map_.resize(0);
	HuffmanBits old_base_code;
	for (size_t len = 0; len <= kMaxHuffmanBits; len++) {
		old_base_code = length_base_code_[len];
		length_base_code_[len] <<= (64 - len);
		if (length_base_code_[len] == 0 && old_base_code > 0)
			length_base_code_[len] = (HuffmanBits)(-1);
	}
}

inline Symbol HuffmanCodeDecompressor::GetNextSymbol() {
	HuffmanBits code = (*data_) << bit_shift;
	HuffmanLength len = min_len;
	if ((bit_shift + len) >= BITS_PER_STEP) {
		++data_;
		code |= ((*data_) >> (BITS_PER_STEP - bit_shift));
	}
	while (code >= length_base_code_[len + 1] && length_base_code_[len + 1] < (HuffmanBits)(-1)) {
		++len;
		if (len > kMaxHuffmanBits)
			return kInvalidSymbol;
		if (((bit_shift + len) & BITS_PER_STEP_MASK) == 0) {
			++data_;
			code |= ((*data_) >> len);
		}
	}
	bit_shift = (bit_shift + len) & BITS_PER_STEP_MASK;
	return canonical_to_sym_map_[((code - length_base_code_[len]) >> (64 - len)) + length_base_canonical_symbol_[len]];
}

bool RePairDecompressor::QuickDecompressPiece(char *in, char *out, size_t needed_byte) {
	if (!one_byte_mode_)
		needed_byte >>= 1;
	huffman_code_.InitBlock(in);
	size_t cur_size = 0;
	Symbol symbol;
	while (cur_size <= needed_byte) {
		if ((symbol = huffman_code_.GetNextSymbol()) == kInvalidSymbol) {
			return false;
		}
		cur_size += symbol_lengths_[symbol];
	}
	cur_size -= symbol_lengths_[symbol];
	while (symbol_lengths_[symbol] > 1) {
		if (symbol_lengths_[meanings_[symbol].first] + cur_size <= needed_byte) {
			cur_size += symbol_lengths_[meanings_[symbol].first];
			symbol = meanings_[symbol].second;
		}
		else
			symbol = meanings_[symbol].first;
	}
	if (one_byte_mode_)
		*out = meanings_[symbol].first;
	else
		*((unsigned short *)out) = meanings_[symbol].first;
	return true;
}

bool RePairDecompressor::DecompressPiece(char *in, char *out, size_t block_size) {
	if (!one_byte_mode_) {
		printf("RePairDecompressor::DecompressPiece: cannot decompress two byte entries!\n");
		return false;
	}
	uint8_t *data_ = (uint8_t *)out;
	memset(data_, 0, block_size);
	huffman_code_.InitBlock(in);
	size_t cur_size = 0;
	Symbol *stack = (Symbol *)malloc(block_size * sizeof(Symbol));
	size_t stack_cnt = 0;
	SymbolMeaning *m;
	while (cur_size < block_size) {
		if ((stack[0] = huffman_code_.GetNextSymbol()) == kInvalidSymbol) {
			free(stack);
			return false;
		}
		stack_cnt = 1;
		while (stack_cnt > 0) {
			m = &meanings_[stack[stack_cnt - 1]];
			if (m->second == kInvalidSymbol) {
				if (cur_size == block_size) {
					free(stack);
					return false;
				}
				out[cur_size] = m->first;
				--stack_cnt;
				++cur_size;
			} else {
				stack[stack_cnt - 1] = m->second;
				stack[stack_cnt] = m->first;
				++stack_cnt;
			}
		}
	}
	free(stack);
	return true;
}

bool RePairDecompressor::GetRange(char *in, size_t needed_byte, size_t *start, size_t *end) {
	if (!one_byte_mode_)
		needed_byte >>= 1;
	huffman_code_.InitBlock(in);
	size_t cur_size = 0;
	Symbol symbol;
	while (cur_size <= needed_byte) {
		if ((symbol = huffman_code_.GetNextSymbol()) == kInvalidSymbol) {
			return false;
		}
		cur_size += symbol_lengths_[symbol];
	}
	*end = cur_size;
	*start = cur_size - symbol_lengths_[symbol];
	return true;
}

}
