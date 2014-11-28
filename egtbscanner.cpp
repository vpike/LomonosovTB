#include <stdio.h>
#include <time.h>
#include <sys/stat.h>
#include <dirent.h>
#include "egtbscanner.h"
#include "egmaintypes.h"
#include "egpglobals.h"
#include "egtbfile.h"

using namespace std;

// < 150 symbols
void get_clock_str(tm *clock, char *str) {
	str[0] = '\0';
	char time_path[20];
	sprintf(time_path, "%d", clock->tm_year);
	strcat(str, time_path);
	sprintf(time_path, "%d", clock->tm_yday);
	strcat(str, time_path);
	sprintf(time_path, "%d", clock->tm_hour);
	strcat(str, time_path);
	sprintf(time_path, "%d", clock->tm_min);
	strcat(str, time_path);
	sprintf(time_path, "%d", clock->tm_sec);
	strcat(str, time_path);
}

//create TB_ini.txt in current directory whis information about exist files and write this in not_exist_tables
//return 0 if error and lenght max pieces if success
void find_not_files() {
	char str[MAX_PATH], cur_path[MAX_PATH];
	rwlock_rdlock(paths_locker);
	rwlock_wrlock(not_exist_tables_locker);
	reset_not_exist_tables(false);
	rwlock_unlock(not_exist_tables_locker);
	// cur_path = current directory (is it true?)
	strcpy(cur_path, "TB_error.txt");
	if (!access(cur_path, 0)) unlink(cur_path);
	// cur_path = current directory
	strcpy(cur_path, "TB_ini.txt");
	FILE *TB_file = fopen(cur_path, "w");
	list<char *>::iterator it_paths = table_paths.begin();
	int table_type;
	while (it_paths != table_paths.end()) {
		strcpy(str, *it_paths);
		str[strlen(str) - 1] = '\0';
		struct stat attrib;
		if (stat(str, &attrib)) {
			fclose(TB_file);
			rwlock_unlock(paths_locker);
			return;
		}
		tm *clock = gmtime((time_t *)&(attrib.st_mtime));
		strcat(str, "\\*.* /");
		char clock_str[150];
		get_clock_str(clock, clock_str);
		strcat(str, clock_str);
		int len = strlen(str);
		str[len] = '/';
		str[len+1] = '\0';
		fprintf(TB_file, "%s\n", str);
		dirent *drnt;
		DIR *dir = opendir(*it_paths);
		int extension_length;
		while ((drnt = readdir(dir)) != NULL) {
			if (!strcmp(drnt->d_name, ".") || !strcmp(drnt->d_name, "..") || drnt->d_type == DT_DIR)
				continue;
			strcpy(str, drnt->d_name);
			char *ex = strchr(str, '.');
			if (ex) ex++;
			if (ex && compare_extension(ex, &table_type, &extension_length) && table_type <= MAX_TYPE) {
				// it's used only in the rehashing of global_cache. But the rehashing is disabled.
				//calculate_min_block_size(*it_paths, f.cFileName, table_type);
				ex = &ex[extension_length];
				while (*ex == '.' || (*ex <= '9' && *ex >= '0')) // it may be not first volume (for example *.1)
					ex = &ex[1];
				if (*ex == '\0') { // if it's first volume
					int size_bg = strcspn(&str[1], "Kk") + 1, size_en = strcspn(str, ".");
					int pieces_cnt = size_en;
					if (strcspn(str, "234567") != strlen(str))
						pieces_cnt--;
					if (max_pieces_count[table_type] < pieces_cnt)
						max_pieces_count[table_type] = pieces_cnt;
					if (size_bg == size_en - size_bg) { // may be full color symmetry
						char tmp_str1[10], tmp_str2[10];
						strncpy(tmp_str1, str, size_bg);
						tmp_str1[size_bg] = '\0';
						strncpy(tmp_str2, &str[size_bg], size_bg);
						tmp_str2[size_bg] = '\0';
						if (!strcmp(tmp_str1, tmp_str2)) { // full color symmetry
							new_set_cur_table_not_exist(str, table_type);
							int str_color = strlen(str) - 1;
							while (str[str_color] != 'w' && str[str_color] != 'b')
								str_color--;
							if (str[str_color] == 'w')
								str[str_color] = 'b';
							else
								str[str_color] = 'w';
							new_set_cur_table_not_exist(str, table_type);
						} else
							new_set_cur_table_not_exist(str, table_type);
					} else {
						new_set_cur_table_not_exist(str, table_type);
					}
				}
			}
		}
		closedir(dir);
		it_paths++;
	}
	rwlock_unlock(paths_locker);
	strncpy(str, "//", 2);
	for (table_type = MIN_TYPE; table_type <= MAX_TYPE; table_type++)
		str[table_type+2] = '0' + max_pieces_count[table_type];
	str[table_type+2] = '\0';
	fprintf(TB_file, "%s\n", str);
	rwlock_rdlock(not_exist_tables_locker);
	for (table_type = MIN_TYPE; table_type <= MAX_TYPE; table_type++) {
		for (unsigned int i = 0; i < NOT_EXIST_TABLES_SIZE; ++i) {
			if (fwrite(&not_exist_tables[table_type][i], sizeof(char), 1, TB_file) != 1) {
				rwlock_unlock(not_exist_tables_locker);
				fclose(TB_file);
				return;
			}
		}
	}
	rwlock_unlock(not_exist_tables_locker);
	for (table_type = MIN_TYPE; table_type <= MAX_TYPE; table_type++)
		fwrite(&min_block_size[table_type], sizeof(unsigned long), 1, TB_file);
	fclose(TB_file);
	known_not_exist = true;
}

//read TB_ini.txt in current directory with the information about existing files
//return 0 if error and lenght max pieces if success
void explicit_get_max_pieces_count() {
	char path_file[MAX_PATH];
	// path_file = current directory
	strcpy(path_file, "TB_error.txt");
	if (!access(path_file, 0)) {
		find_not_files();
		return;
	}
	// path_file = current directory
	strcpy(path_file, "TB_ini.txt");
	FILE *TB_file = fopen(path_file, "rb");
	if (TB_file == NULL) {
		find_not_files();
		return;
	}
	rwlock_wrlock(not_exist_tables_locker);
	reset_not_exist_tables(false);
	rwlock_unlock(not_exist_tables_locker);
	char str[MAX_PATH];
	bool fl = true;
	rwlock_rdlock(paths_locker);
	list<char *>::iterator it_paths = table_paths.begin();
	while (fl) {
		if (fgets(str, MAX_PATH, TB_file)) {
			if (strncmp(str, "//", 2) != 0) {
				int separ = strcspn(str, " ");
				char *time_path_from_TB = &str[separ + 2];
				str[separ - 3] = '\0';
				if (it_paths == table_paths.end() || strcmp(str, *it_paths)) {
					fclose(TB_file);
					rwlock_unlock(paths_locker);
					find_not_files();
					return;
				}
				str[separ - 4] = '\0';
				it_paths++;
				separ = strcspn(time_path_from_TB, "/");
				time_path_from_TB[separ] = '\0';
				struct tm* clock;
				struct stat attrib;
				if (stat(str, &attrib)) {
					fclose(TB_file);
					rwlock_unlock(paths_locker);
					find_not_files();
					return;
				}
				clock = gmtime((time_t *)&(attrib.st_mtime));
				char time_path[150];
				get_clock_str(clock, time_path);
				if (strcmp(time_path, time_path_from_TB)) {
					fclose(TB_file);
					rwlock_unlock(paths_locker);
					find_not_files();
					return;
				}
			} else {
				for (int table_type = MIN_TYPE; table_type <= MAX_TYPE; table_type++)
					max_pieces_count[table_type] = str[2+table_type] - '0';
				fl = false;
			}
		} else {
			fclose(TB_file);
			rwlock_unlock(paths_locker);
			find_not_files();
			return;
		}
	}
	if (it_paths != table_paths.end()) {
		fclose(TB_file);
		rwlock_unlock(paths_locker);
		find_not_files();
		return;
	}
	rwlock_unlock(paths_locker);
	rwlock_wrlock(not_exist_tables_locker);
	fl = false;
	for (int table_type = MIN_TYPE; table_type <= MAX_TYPE && !fl; table_type++) {
		unsigned int index = 0;
		while (index < NOT_EXIST_TABLES_SIZE && !fl) {
			if (fread(&not_exist_tables[table_type][index], sizeof(char), 1, TB_file) != 1)
				fl = true;
			++index;
		}
	}
	rwlock_unlock(not_exist_tables_locker);
	for (int table_type = MIN_TYPE; table_type <= MAX_TYPE && !fl; table_type++) {
		fl = (fread(&min_block_size[table_type], sizeof(unsigned long), 1, TB_file) != 1);
	}
	fclose(TB_file);
	if (fl)
		find_not_files();
	else
		known_not_exist = true;
}

void scan_tables() {
	explicit_get_max_pieces_count();
	if (!known_not_exist)
		reset_not_exist_tables(true);
	// Rehashing seems to be not effective (because of hidden cache takes more memory than cache of block)
	//global_cache.rehash();
}
