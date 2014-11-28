#ifndef EGDECOMPRE_PAIR_H_
#define EGDECOMPRE_PAIR_H_

#include <vector>

#define MAX_DICT_SIZE 4096
#define ENTRY_SIZE (one_byte_mode_ ? 1 : 2)

namespace Re_pair {

typedef uint16_t Symbol;
typedef uint64_t Frequency;
typedef uint16_t PairLength; // as kMaxSymbolLength < 65536

static const Symbol kInvalidSymbol = static_cast<Symbol>(-1);

struct SymbolMeaning {
	Symbol first;
	Symbol second;
};

class HuffmanCode {
  public:
	typedef uint64_t HuffmanBits;
	typedef uint8_t HuffmanLength;
	static const int kMaxHuffmanBits = 56; // cannot be higher than 8*(sizeof(HuffmanBits)-1)
  protected:
	size_t num_symbols_;

	// lengths of encoded symbols (0 = the only symbol or unused symbol)
	std::vector<HuffmanLength> code_length_;

	// base Huffman code for the given length (all codes of the same length are consecutive)
	std::vector<HuffmanBits> length_base_code_;

	// how many shorter canonical symbols there are
	std::vector<Symbol> length_base_canonical_symbol_;

	// map[symbol] -> sorted (canonical Huffman) symbol number
	std::vector<Symbol> sym_to_canonical_map_;

	// builds canonical Huffman code for given code_length_ values
	void BuildCanonicalCode();

	class HuffmanCodeItem {
	public:
		HuffmanCodeItem(Symbol symbol, HuffmanLength length): symbol_(symbol), length_(length) {}
		bool operator < (const HuffmanCodeItem &second) const {
			return length_ > second.length_ || (length_ == second.length_ && symbol_ > second.symbol_);
		}
		Symbol symbol() const { return symbol_; }
	private:
		Symbol symbol_;
		HuffmanLength length_;
	};
};

#define BITS_PER_STEP 64
#define BITS_PER_STEP_MASK 0x3f

class HuffmanCodeDecompressor : HuffmanCode {
private:
	char *data_;
	// map[canonical] -> symbol (opposite to sym_to_canonical_map_)
	std::vector<Symbol> canonical_to_sym_map_;
	HuffmanLength min_len;
	HuffmanLength max_len;

	// current information for reading
	uint8_t bit_shift;

	uint64_t get_data() {
		uint64_t res;
		memcpy(&res, data_, BITS_PER_STEP / 8);
		return res;
	}
	void skip_data() {
		data_ += BITS_PER_STEP / 8;
	}
public:
	void BuildFromCodeLength(const char *code_length, size_t num_symbols);
	// class needs to init before every decompressing
	void InitBlock(const char *data) {
		data_ = (char *)data;
		bit_shift = 0;
	}
	Symbol GetNextSymbol();
};

class RePairDecompressor {
private:
	SymbolMeaning *meanings_;
	PairLength *symbol_lengths_;
	HuffmanCodeDecompressor huffman_code_;
	size_t dict_size_;
	bool one_byte_mode_;
public:
	RePairDecompressor(char *meanings, char *huffman_lengths, size_t dict_size, bool one_byte_mode) {
		dict_size_ = dict_size;
		one_byte_mode_ = one_byte_mode;
		meanings_ = (SymbolMeaning *)malloc(dict_size_ * sizeof(SymbolMeaning));
		memcpy(meanings_, meanings, 4*dict_size);
		symbol_lengths_ = (PairLength *)malloc(dict_size_ * sizeof(PairLength));
		for (size_t i = 0; i < dict_size; i++) {
			if (meanings_[i].second == kInvalidSymbol)
				symbol_lengths_[i] = 1;
			else {
				if (meanings_[i].first >= i || meanings_[i].second >= i) {
					printf("RePairDecompressor::RePairDecompressor: wrong order of symbols meanings_[%d] = (%d, %d)\n",
						i, meanings_[i].first, meanings_[i].second);
					exit(1);
				}
				symbol_lengths_[i] = symbol_lengths_[meanings_[i].first] + symbol_lengths_[meanings_[i].second];
			}
		}
		huffman_code_.BuildFromCodeLength(huffman_lengths, dict_size_);
	}
	~RePairDecompressor() {
		free(meanings_);
		free(symbol_lengths_);
	}
	bool QuickDecompressPiece(char *in, char *out, size_t needed_byte);
	bool DecompressPiece(char *in, char *out, size_t block_size);
	bool GetRange(char *in, size_t needed_byte, size_t *start, size_t *end);
	size_t get_dict_size() {
		return dict_size_;
	}
	int get_size() {
		return dict_size_ * 8 +		// 4 Bytes (meanings_) +  2 Bytes (symbol_lengths_) + 2 Bytes (huffman_code_.canonical_to_sym_map_) +
							560;	// 560 = 56 * (8 + 2) in hyffman_code_
	}
};

}

#endif /* EGDECOMPRE_PAIR_H_ */
