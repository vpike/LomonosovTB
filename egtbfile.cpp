#include <string.h>
#include "egmaintypes.h"
#include "egtbfile.h"

const char *tbext[] = { "", "tbs", "dtm", "bld", "lmw", "lmb", "tlw", "tlb", "mlw", "mlb", "dlw", "dlb",
	"zlmw", "zlmb", "ztlw", "ztlb", "zmlw", "zmlb", "zdlw", "zdlb", "ltw", "ltb", "zltw", "zltb", "ztbs", "dtz", "zbld", "dtm50",
	"plw", "plb", "wlw", "wlb", "zplw", "zplb", "zwlw", "zwlb"};
const char tt_to_ft_map[14] = { TB_DATA_WMT, TB_DATA_WWL, TB_DATA_WTT, TB_DATA_WPL, TB_DATA_WTTD, // ML, WL, TL, PL, DL
	TB_DATA_ZWMT, TB_DATA_ZWWL, TB_DATA_ZWTT, TB_DATA_ZWPL, TB_DATA_ZWTTD, // ZML, ZWL, ZTL, ZPL, ZDL
	TB_DATA_WTM, TB_DATA_WTTM, TB_DATA_ZWTM, TB_DATA_ZWTTM };

unsigned char get_men_count(const char *filename) {
	unsigned char cnt = 0;
	int i = strlen(filename) - 1;
	// skip volume number
	if (filename[i] >= '0' && filename[i] <= '9') {
		while (filename[i] != '.')
			--i;
		--i;
	}
	// skip extension
	while (filename[i] != '.')
		--i;
	--i;
	for (char j = 0; j < 2; j++) {
		while (filename[i] != 'k') {
			if (filename[i] < '0' || filename[i] > '9')
				++cnt;
			--i;
		}
		// king
		++cnt;
		--i;
	}
	return cnt;
}

bool compare_extension(char *ext, int *table_type, int *ext_len) {
	int type;
	bool result = false;
	int len = strlen(ext);
	for (type = MIN_TYPE; type <= MAX_TYPE + 4; type++) {
		*ext_len = get_externsion_length(type);
		result = len == (*ext_len) && (!strncmp(ext, tbext[tt_to_ft_map[type]], *ext_len) || !strncmp(ext, tbext[tt_to_ft_map[type] + 1], *ext_len));
		if (result) {
			*table_type = type;
			return true;
		}
	}
	return false;
}

int get_externsion_length(int table_type) {
	return strlen(tbext[tt_to_ft_map[table_type]]);
}
