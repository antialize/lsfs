//-*- mode: c++; tab-width: 4; indent-tabs-mode: t; c-file-style: "stroustrup"; -*-
#ifndef __LSFS_HH__
#define __LSFS_HH__

#include <cerrno>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <exception>
#include <map>
#include <set>
#include <stdint.h>
#include <string>
#include <vector>
namespace lsfs {

	class FS;

	class InternalError: public std::exception {
    private:
		char buff[2048];
    public:
		int number;
		inline InternalError(int line, const char * file, bool en, const char * format, ...) {
			va_list ap;
			va_start(ap, format);
			int i=snprintf(buff, 2048, "%s: %d\n",file,line);
			if(en && errno) i+=snprintf(buff+i, 2048-i, "%d: %s\n",errno,strerror(errno));
			vsnprintf(buff+i, 2048-i, format, ap);
			va_end(ap);
			number=ENOMSG; //Todo change this to something better
		}
		const char* what() const throw () {return buff;}
    };

	class ErrnoException: public std::exception {
	public:
		int number;
		ErrnoException(int _): number(_) {};
		const char * what() const throw () {return "Numbered exception";}
	};
		
    class File {
    public:
		std::string name;
		uint64_t usage;
		std::vector<std::pair<uint64_t,uint64_t> > chunks;
		uint64_t index;
		uint64_t size();
		friend class FS;
	};
	
    class Handle {
    private:
		File * file;
		int fd;
		friend class FS;
		FS * fs;
		uint64_t cl;
		uint64_t chunk;
		void allocate(uint64_t size);
		void seekset();
    public:
		void close();
		Handle();
		Handle(const Handle & h);
		~Handle();
		void seek(uint64_t where);
		uint64_t read(uint8_t * buf, uint64_t size);
		void write(const uint8_t * buf, uint64_t size); 
		uint64_t size();
		uint64_t tell();
		void truncate(uint64_t size);
    };

	typedef std::map<std::string, File *> files_t;
	typedef std::set<std::string> filelist_t;
	typedef std::set<std::pair<uint64_t,uint64_t> > freespace_t;
    class FS {
    private:
		friend class Handle;
		files_t files;
		filelist_t filelist;
		freespace_t freespace;
		std::string path;
		uint64_t _size;
		
		int inotifyfd;
		bool readonly;
		uint64_t maxfiles;
		uint64_t maxchunks;
		bool writing;

		size_t filesize;

		void unuse(File * file);
		void writeHeader(int fd);
		void writeFile(int fd, File * file);
		int getFd();
		void compressFreeSpace();
    public:
		static void create(const std::string & path, uint64_t maxfiles=1000, uint64_t maxchunks=128);
		void mount(const std::string & path, bool readOnly, bool ignorewm=false);
		void umount();
		inline const std::set<std::string> & ls() {return filelist;}
		Handle * open(const std::string & name, bool readOnly=true, Handle * f=NULL);
		void unlink(const std::string & name);
		uint64_t size(const std::string & name);
		static void defrag(const std::string & path);
    };
}

#endif //__LSFS_HH__
