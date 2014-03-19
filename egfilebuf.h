#ifndef EGFILEBUF_H_
#define EGFILEBUF_H_

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef _MSC_VER
#include <unistd.h>
#else
#include <io.h>
#endif
#include "egmaintypes.h"

#ifdef WIN32
#define WIN32_FILE
#endif

//#define FILE_VIA_MPI

// basic buffered reading plain files
#ifdef FILE_VIA_MPI
typedef MPI_Offset file_offset;
#else
typedef unsigned long long file_offset;
#endif

#define FB_TO_THE_END 0xffffffffffffffffLL
#define FB_NO_SEEK 0xffffffffffffffffLL

class read_file_bufferizer {
protected:
#ifdef FILE_VIA_MPI
	MPI_File fh;
#else
#ifdef WIN32_FILE
	HANDLE fh;
#else
	int fh; // handle of file
#endif
#endif
	bool no_file;
	bool opened_;
	char *buffer;
	unsigned long buf_size, buf_pos, bytes_in_buffer;
	file_offset cur_file_pos_read, cur_file_pos_write, end_file_pos;
	virtual void read_buffer();
	bool write_mode;
public:
	char *read_file_name;
	bool not_caching;
#ifdef PARALLEL_TB
	bool locked;
#endif
	bool trylock() {
#ifdef PARALLEL_TB
		if (locked)
			return false;
		else {
			locked = true;
			return true;
		}
#else
		return true;
#endif
	}
	void unlock();
	file_offset total_file_length; // read-only, filled in begin_read()
	read_file_bufferizer() {
#ifdef PARALLEL_TB
		locked = false;
#endif
		no_file = true; buffer = NULL; 
		not_caching = true;
		opened_ = false;
		read_file_name = NULL;
	}
	virtual ~read_file_bufferizer() { 
		if (!no_file) { 
#ifndef WIN32_FILE
			if (write_mode) flush();
#endif
#ifdef FILE_VIA_MPI
			MPI_File_close(&fh);
#else
#ifdef WIN32_FILE
			CloseHandle(fh);
#else
			close(fh);
#endif
#endif
		} 
		if (buffer) free(buffer); 
		if (read_file_name) free(read_file_name);
	}

	virtual bool begin_read(const char *filename, file_offset start_pos, file_offset length);

	virtual void read(char *data, unsigned long size);
	bool end_of_file() { return buf_pos >= bytes_in_buffer && cur_file_pos_read >= end_file_pos; }
	
	virtual file_offset current_file_pos() { return cur_file_pos_read - bytes_in_buffer + buf_pos; }
	virtual void seek(file_offset new_pos);
#ifndef WIN32_FILE
	void flush();
#endif

	virtual void set_buf_size(unsigned long value);

	int get_size() {
		return sizeof(read_file_bufferizer) + buf_size;
	}
};

#ifndef WIN32_FILE
// buffered read-write of plain files 
class plain_file_bufferizer: public read_file_bufferizer {
public:
	bool begin_write(const char *filename, file_offset start_pos, file_offset length);
	void write(char *data, unsigned long size);
};
#endif

#endif /*EGFILEBUF_H_*/
