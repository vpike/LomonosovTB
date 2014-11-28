#include "egfilebuf.h"
#include "egpglobals.h"
#include "egcachecontrol.h"
#ifdef LOMONOSOV_FULL
#include "egtypes.h"
#include "egglobals.h"
#endif

#include <assert.h>

#define TB_FILE_BUF_SIZE 1048576

bool read_file_bufferizer::begin_read(const char *filename, file_offset start_pos, file_offset length) {
	assert(opened_ == false); // do not call twice
#ifdef FILE_VIA_MPI
	if (MPI_File_open(MPI_COMM_SELF, filename, MPI_MODE_RDONLY, MPI_INFO_NULL, &fh) != MPI_SUCCESS)
		return false;
#else
#ifdef WIN32_FILE
	fh = CreateFile(filename, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, NULL, NULL);
	if (fh == INVALID_HANDLE_VALUE) return false;
	unsigned long size_low, size_high;
	size_low = GetFileSize(fh, &size_high);
	total_file_length = ((file_offset)size_high) << 32 | ((file_offset)size_low);
#else
	fh = open(filename, O_RDONLY | O_BINARY);
	if (fh < 0) return false;
#endif
#endif
	opened_ = true;
	buf_size = TB_FILE_BUF_SIZE;
	buffer = (char *)malloc(buf_size);
	buf_pos = bytes_in_buffer = 0;
	cur_file_pos_read = start_pos;
#ifdef FILE_VIA_MPI
	MPI_File_get_size(fh, &total_file_length);
#else

	#if !defined(WIN32) && !defined(__ANDROID__)
	BUILD_BUG_ON(sizeof(off_t) < 8); // lseek MUST use 64-bit file offsets. check _LARGEFILE64_SOURCE and _FILE_OFFSET_BITS=64 defines
	#endif

#ifndef WIN32_FILE
	total_file_length = os_lseek64(fh, 0, SEEK_END);
#endif
#endif
	if (length != (file_offset)FB_TO_THE_END) end_file_pos = start_pos+length;
	else end_file_pos = total_file_length;
	write_mode = false;
	no_file = false;
	read_file_name = (char *)malloc(strlen(filename)+1);
	strcpy(read_file_name, filename);
	return true;
}

#ifndef WIN32_FILE

bool plain_file_bufferizer::begin_write(const char *filename, file_offset start_pos, file_offset length) {
	assert(opened_ == false); // do not call twice
#ifdef FILE_VIA_MPI
	if (MPI_File_open(MPI_COMM_SELF, filename, MPI_MODE_RDWR | MPI_MODE_CREATE, MPI_INFO_NULL, &fh) != MPI_SUCCESS)
		return false;
#else
	fh = open(filename, O_RDWR | O_BINARY);
	if (fh < 0) {
		if (length == 0) fh = open(filename, O_WRONLY | O_CREAT | O_TRUNC | O_BINARY, 0666);
		if (fh < 0) return false;
	}
#endif
	opened_ = true;
	buf_size = TB_FILE_BUF_SIZE;
	buffer = (char *)malloc(buf_size);
	buf_pos = bytes_in_buffer = 0;
	cur_file_pos_read = cur_file_pos_write = start_pos;
	end_file_pos = start_pos+length;
	write_mode = true;
	no_file = false;
	return true;
}

#endif

void read_file_bufferizer::read_buffer() {
	unsigned long to_read;

	buf_pos = 0;
	to_read = buf_size;
	if (to_read > end_file_pos - cur_file_pos_read) to_read = end_file_pos - cur_file_pos_read;
#ifdef FILE_VIA_MPI
	MPI_File_read_at(fh, cur_file_pos_read, buffer, to_read, MPI_BYTE, NULL);
#else
#ifdef WIN32_FILE
	LARGE_INTEGER move;
	move.QuadPart = cur_file_pos_read;
	if (!SetFilePointerEx(fh, move, NULL, FILE_BEGIN)) {
		printf("read_file_bufferizer::read_buffer: cannot set file pointer to the end! file %s, LastError = %d\n", read_file_name, GetLastError());
		ABORT(1);
	}
	unsigned long max_read = to_read;
	if (!ReadFile(fh, buffer, max_read, &to_read, NULL)) {
		printf("read_file_bufferizer::read_buffer: cannot read file to the end! file %s, LastError = %d\n", read_file_name, GetLastError());
		ABORT(1);
	}
#else
	os_lseek64(fh, cur_file_pos_read, SEEK_SET);
	to_read = ::read(fh, buffer, to_read);
#endif
#endif
	cur_file_pos_read += to_read;
	bytes_in_buffer = to_read;
}

void read_file_bufferizer::read(char *data, unsigned long size) {
	unsigned long rd;
	do {
		if (buf_pos >= bytes_in_buffer) read_buffer();
		if (buf_pos+size > bytes_in_buffer) rd = bytes_in_buffer-buf_pos; else rd = size;
		if (rd) {
			memcpy(data, &(buffer[buf_pos]), rd);
			data += rd;
			buf_pos += rd;
			size -= rd;
		}
	} while (size && rd);
}

#ifndef WIN32_FILE

void plain_file_bufferizer::write(char *data, unsigned long size) {
	if (buf_pos+size > buf_size) flush();
	if (!bytes_in_buffer && cur_file_pos_read < end_file_pos) read_buffer();
	if (data) memcpy(&(buffer[buf_pos]), data, size);
	buf_pos += size;
}

void read_file_bufferizer::flush() {
	if (write_mode && !no_file && buf_pos > 0) {
#ifdef FILE_VIA_MPI
		MPI_File_write_at(fh, cur_file_pos_write, buffer, buf_pos, MPI_BYTE, NULL);
#else
		os_lseek64(fh, cur_file_pos_write, SEEK_SET);
		::write(fh, buffer, buf_pos);
#endif
		cur_file_pos_write += buf_pos;
	}
	buf_pos = bytes_in_buffer = 0;
}

#endif

void read_file_bufferizer::seek(file_offset new_pos) {
	file_offset old_pos;
	old_pos = current_file_pos();
	if (new_pos >= old_pos && new_pos < old_pos+bytes_in_buffer-buf_pos)
		buf_pos += new_pos - old_pos;
	else { // clear buffer
#ifndef WIN32_FILE
		if (write_mode) flush();
#endif
		buf_pos = bytes_in_buffer = 0;
		cur_file_pos_read = cur_file_pos_write = new_pos;
	}
}

void read_file_bufferizer::set_buf_size(unsigned long value) {
	if (buf_size == value)
		return;
	if (!not_caching) {
		rwlock_wrlock(hidden_locker);
		cur_hidden_size -= buf_size;
		cur_hidden_size += value;
#ifdef LOG_HIDDEN
		log_cur_hidden_size("set_buf", (int)value - (int)buf_size);
#endif
		rwlock_unlock(hidden_locker);
	}
	buf_size = value; 
	buffer = (char*) realloc(buffer, buf_size);
	if (bytes_in_buffer > buf_size) {
		cur_file_pos_read -= (bytes_in_buffer - buf_size);
		bytes_in_buffer = buf_size;
		buf_pos = 0;
	}
}

void read_file_bufferizer::unlock() {
#ifdef PARALLEL_TB
		if (!locked) {
			printf("bufferizer::unlock: unlock is called, but bufferizer is unlocked!");
			ABORT(0);
		}
		locked = false;
#endif
}
