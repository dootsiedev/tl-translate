// This is an independent project of an individual developer. Dear PVS-Studio, please check it.
// PVS-Studio Static Code Analyzer for C, C++, C#, and Java: https://pvs-studio.com
#include "../core/global.h"
#include "BS_stream.h"
#include "../core/RWops.h"

void BS_ReadStream::Read()
{
	if(current_ < bufferLast_)
	{
		++current_;
	}
	else if(!eof_)
	{
		count_ += readCount_;
		readCount_ = file->read(buffer_, 1, bufferSize_);
		bufferLast_ = buffer_ + readCount_ - 1;
		current_ = buffer_;

		if(readCount_ < bufferSize_)
		{
			buffer_[readCount_] = '\0';
			++bufferLast_;
			eof_ = true;
		}
	}
	else
	{
		error_ = true;
	}
}

size_t BS_ReadStream::Size() const
{
	// probably should use file->size(), but I don't use this function...
	RW_ssize_t old_cur = file->tell();
	if(old_cur < 0)
	{
		return 0;
	}
	if(!file->seek(0, SEEK_END))
	{
		return 0;
	}

	RW_ssize_t end_pos = file->tell();
	if(end_pos < 0)
	{
		return 0;
	}

	if(!file->seek(old_cur, SEEK_SET))
	{
		return 0;
	}

	return end_pos;
}

bool BS_ReadStream::Rewind()
{
	current_ = buffer_;
	count_ = 0;
	eof_ = false;
	error_ = false;
	readCount_ = 0;
	bufferLast_ = 0;
	if(!file->seek(0, SEEK_SET))
	{
		return false;
	}
	Read();
	return true;
}

void BS_WriteStream::Flush()
{
	if(current_ != buffer_)
	{
		size_t result = file->write(buffer_, 1, current_ - buffer_);
		if(result < static_cast<size_t>(current_ - buffer_))
		{
			error = true;
		}
		current_ = buffer_;
	}
}