#ifndef EGTBFILE_H_
#define EGTBFILE_H_

#define TB_NO_EXTENSION   0
#define TB_STATS          1
#define TB_LONGEST_MATE   2
#define TB_IS_BUILDING    3
#define TB_DATA_WTM       4
#define TB_DATA_BTM       5
#define TB_DATA_WTT       6
#define TB_DATA_BTT       7
#define TB_DATA_WMT       8
#define TB_DATA_BMT       9
#define TB_DATA_WTTD	  10
#define TB_DATA_BTTD	  11
#define TB_DATA_ZWTM	  12
#define TB_DATA_ZBTM	  13
#define TB_DATA_ZWTT	  14
#define TB_DATA_ZBTT	  15
#define TB_DATA_ZWMT	  16
#define TB_DATA_ZBMT	  17
#define TB_DATA_ZWTTD	  18
#define TB_DATA_ZBTTD	  19
#define TB_DATA_WTTM	  20
#define TB_DATA_BTTM	  21
#define TB_DATA_ZWTTM	  22
#define TB_DATA_ZBTTM	  23
#define TB_Z_STATS		  24
#define TB_Z_LONGEST_MATE 25
#define TB_STATS_FENS	  26
#define TB_Z_STATS_FENS	  27
#define TB_Z_IS_BUILDING  28
#define TB_Z_LONGEST_MTM  29
#define TB_DATA_WPL		  30
#define TB_DATA_BPL		  31
#define TB_DATA_WWL		  32
#define TB_DATA_BWL		  33
#define TB_DATA_ZWPL	  34
#define TB_DATA_ZBPL	  35
#define TB_DATA_ZWWL	  36
#define TB_DATA_ZBWL	  37

extern const char *tbext[];
extern const char tt_to_ft_map[14];

unsigned char get_men_count(const char *filename);
bool compare_extension(char *ext, int *table_type, int *ext_len);
int get_externsion_length(int table_type);

#endif /* EGTBFILE_H_ */
