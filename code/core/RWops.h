#pragma once

#include <inttypes.h>
#include <stdio.h>

#include <memory>

typedef long RW_ssize_t; // NOLINT

// follows the same specifications as stdio functions.
// except you can't have non-blocking data, some functions return bool, and no feof/ferror.
class RWops
{
public:
    //returns the name of the stream.
	virtual const char* name() = 0;
	virtual size_t read(void *ptr, size_t size, size_t nmemb) = 0;
	virtual size_t write(const void *ptr, size_t size, size_t nmemb) = 0;
	virtual bool seek(RW_ssize_t offset, int whence) = 0;
	virtual RW_ssize_t tell() = 0;
	virtual RW_ssize_t size() = 0;
    // assert will occur if you call any functions after close(), including a double close().
	// if you don't call close() before the destructor, it will print a warning into slog.
	virtual bool close() = 0;
	virtual ~RWops() = default;
};

typedef std::unique_ptr<RWops> Unique_RWops;

//will print an error so that you can pass it into RWops_Stdio
FILE* serr_wrapper_fopen(const char* path, const char* mode);

// loads a file into a string using fopen.
// only accepts "r" and "rb"
bool slurp_stdio(std::string& out, const char* path, const char* mode = "rb");

class RWops_Stdio : public RWops
{
public:
    //I could replace stream_info with fstat filename, but I like customization.
	std::string stream_name;
    //the fp will be closed with fclose(), and size() will use fstat.
    FILE* fp;
    RWops_Stdio(FILE* stream, std::string file);

	const char* name() override;
	size_t read(void *ptr, size_t size, size_t nmemb) override;
	size_t write(const void *ptr, size_t size, size_t nmemb) override;
	bool seek(RW_ssize_t offset, int whence) override;
	RW_ssize_t tell() override;
	RW_ssize_t size() override;
    bool close() override;
	~RWops_Stdio() override;
};


Unique_RWops Unique_RWops_OpenFS(std::string path, const char* mode);
Unique_RWops Unique_RWops_FromFP(FILE* fp, std::string name = std::string());

//this will not allocate during writing
Unique_RWops Unique_RWops_FromMemory(char* memory, size_t size, bool readonly = false, std::string name = std::string());

