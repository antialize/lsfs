//-*- mode: c++; tab-width: 4; indent-tabs-mode: t; c-file-style: "stroustrup";

//Note that when a filesystem is mounted with write premissions,
//it can be mounted read only, however a file can be pulled from under your feed 
//while it is open, so learn to live with it..

//When filesystem is mounted readonly... create inotify
#include "lsfs.hh"
#include <cstring>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <cassert>
#include <iostream>

namespace {
#define THROW_PE(FORMAT, ...) throw lsfs::InternalError(__LINE__,__FILE__,true, FORMAT, ##__VA_ARGS__, NULL)
#define THROW_E(FORMAT, ...) throw lsfs::InternalError(__LINE__,__FILE__,false, FORMAT, ##__VA_ARGS__, NULL)

#define THROW_ERRNO(FORMAT, ...) throw lsfs::ErrnoException(errno);
#define THROW_ERRNOG(err, FORMAT, ...) throw lsfs::ErrnoException(err);

    struct fdw {
	int x;
	fdw(int _): x(_) {};
	~fdw() {if(x != -1) close(x);}
	operator int() {return x;}
	void release() {x=-1;}
    };
    const uint64_t magic = 0xCAFEBABEDEADBEEF;

    template <typename T> 
    struct ptrw {
	T * p;
	ptrw(T * _=NULL): p(_) {};
	~ptrw() {if(p != NULL) delete p;}
	operator T*() {return p;}
	T * operator=(T * _) {
	    if(p != NULL) delete p;
	    p = _;
	}
	void release() {p=NULL;}
    };
        
#pragma pack(push, 1)
    struct header_t {
	uint64_t magic;
	uint64_t version;
	uint64_t writemounted;
	uint64_t writing;
	uint64_t files;
	uint64_t maxfiles;
	uint64_t maxchunks;
    };
    
    struct chunk_t {
	uint64_t start;
	uint64_t end;
    };
    
    struct file_t {
	char name[1024];
	uint64_t chunkCount;
	chunk_t chunks[0];
    };
#pragma pack(pop)
}

namespace lsfs {
    
    void FS::mount(const std::string & path, bool readOnly, bool ignorewm) {
	this->path = path;
	filelist.clear();
	files.clear();
	
	fdw fd = getFd();
	header_t header;
	if(lseek(fd,0,SEEK_SET) == -1) THROW_ERRNO("lseek");
	if(read(fd,&header,sizeof(header_t)) != sizeof(header_t)) THROW_ERRNOG(EINVAL,"read");
	if(header.magic != magic || header.version != 1) 
	    THROW_ERRNOG(EINVAL,"Wrong header or magic");
	
	maxchunks = header.maxchunks;
	maxfiles = header.maxfiles;
	filesize = sizeof(file_t) + sizeof(chunk_t) * header.maxchunks;
	
	freespace_t used;
	
	uint8_t buf[filesize];
	file_t * f = reinterpret_cast<file_t*>(buf);

	for(size_t i=0; i < header.files; ++i) {
	    if(read(fd,buf,filesize) != filesize) THROW_ERRNOG(EINVAL,"Error reading file table"); 
	    File * file = new File();
	    file->name = f->name;
	    file->usage = 1;
	    file->index = i;
	    for(size_t j=0; j < f->chunkCount; ++j) {
		file->chunks.push_back( std::make_pair( f->chunks[j].start, f->chunks[j].end) );
		used.insert( std::make_pair( f->chunks[j].start, f->chunks[j].end) );
	    }
	    filelist.insert(file->name);
	    files[file->name] = file;
	}
	off_t size = lseek(fd,0,SEEK_END);
	if(size == -1) THROW_ERRNO("lseek failed");
	used.insert( std::make_pair(0,sizeof(header_t) + maxfiles*filesize));
	used.insert( std::make_pair(size,size) );
	
	freespace_t::iterator o=used.begin();
	freespace_t::iterator i=o;
	std::cout << "Free space:" << std::endl;
	for(++i; i != used.end(); ++i) {
	    if(o->second != i->first) {
		std::cout << "   " << o->second << " " << i->first << std::endl;
		freespace.insert( std::make_pair(o->second, i->first) );
	    }
	    o=i;
	}
    }
    
    uint64_t File::size() {
	uint64_t res=0;
	for(size_t i=0; i < chunks.size(); ++i)
	    res += chunks[i].second - chunks[i].first;
	return res;
    }

    void Handle::close() {
	if(fd != -1) ::close(fd);
	fd=-1;
	if(file != NULL) fs->unuse(file);
	file = NULL;
    }

    Handle::Handle(): file(NULL),fd(-1), fs(NULL) {};
    Handle::~Handle() {close();}

    inline void Handle::seekset() {
	if(chunk == (uint64_t)-1) return; 
	if(lseek(fd,file->chunks[chunk].first+cl, SEEK_SET) == -1) THROW_ERRNO("lseek");
    }
    
    void Handle::seek(uint64_t where) {
	if(where == 0 && file->chunks.empty()) return;
	cl = where;
	for(chunk=0; chunk < file->chunks.size(); ++chunk) {
	    size_t s = file->chunks[chunk].second - file->chunks[chunk].first;
	    if(s < cl) {cl -= s; continue;}
	    seekset();
	    return;
	}
	THROW_ERRNOG(EINVAL,"Bad location");
    }


    void Handle::allocate(uint64_t size) {
	if(size == 0) return;
	std::cout << ">> Allocate" << std::endl;
	if(file->chunks.size() > 0) { //Try to expand the last chunk
	    uint64_t p = file->chunks.back().second;
	    freespace_t::iterator i = fs->freespace.lower_bound( std::make_pair(p,p) );
	    if(i != fs->freespace.end() && i->first == p) { 
		//Great we can extend the last chunk;
		uint64_t start=i->first;
		uint64_t end=i->second;
		fs->freespace.erase(i);
		uint64_t s = std::min(end-start,size);
		file->chunks.back().second += s;
		size -= s;
		if(start+s != end) fs->freespace.insert(std::make_pair(start+s,end));
	    }
	}
	while(size > 0) {
	    std::cout << " .." << std::endl;
	    freespace_t::iterator best=fs->freespace.begin();
	    for(freespace_t::iterator i=best; i != fs->freespace.end(); ++i)
		if(i->second - i->first > best->second - best->first) best=i;
	    if(best == fs->freespace.end()) {
		fs->writeFile(fd, file);
		seekset();
		THROW_ERRNOG(ENOSPC, "No space left on device");
	    }
	    uint64_t start=best->first;
	    uint64_t end=best->second;
	    fs->freespace.erase(best);
	    uint64_t s = std::min(end-start,size);
	    file->chunks.push_back(std::make_pair(start, start+s) );
	    size -= s;
	    if(start+s != end) fs->freespace.insert(std::make_pair(start+s,end));
	}
	fs->writeFile(fd, file);
	seekset();
	std::cout << "<< Allocate" << std::endl;
    }
    
    void Handle::truncate(uint64_t size) {
	std::cout << ">>truncate " << fd << " " << size<< std::endl;
	size_t c=0;
	while(size > 0 && c < file->chunks.size()) {
	    std::pair<uint64_t, uint64_t> & cc = file->chunks[c];
	    size_t s = cc.second - cc.first;
	    c++;
	    if(size > s) {size-=s; continue;}
	    if(size == 0) break;
	    fs->freespace.insert( std::make_pair(cc.first+size, cc.second) );
	    cc.second = cc.first+size;
	    break;
	}
	for(int i=c;i < file->chunks.size(); ++i) {
	    std::pair<uint64_t, uint64_t> & cc = file->chunks[i];
	    fs->freespace.insert( std::make_pair(cc.first, cc.second) );
	}
	file->chunks.resize(c);
	fs->compressFreeSpace();

	if(size > 0) allocate(size);
	else {
	    fs->writeFile(fd, file);
	    seekset();
	}
	chunk = (uint64_t)-1;
	cl = 0;
	if(file->chunks.size() > 0) {
	    if(lseek(fd,file->chunks[0].first,SEEK_SET) == -1) THROW_PE("lseek");
	    chunk = 0;
	}
	std::cout << "<<truncate" << std::endl;
    }

    Handle::Handle(const Handle & h) {
	file = h.file;
	if(file != NULL) file->usage++;
	fd = h.fd==-1?-1:dup(h.fd);
	fs = h.fs;
	cl = h.cl;
	chunk = h.chunk;
    }

    uint64_t Handle::read(uint8_t * buf, uint64_t size) {
	std::cout << ">>Read" << std::endl;
	uint64_t read=0;
	while(size > 0) {
	    if(chunk == (uint64_t)-1) break;
	    uint64_t cr=file->chunks[chunk].second-file->chunks[chunk].first-cl;
	    uint64_t r = std::min(size,cr);
	    if(::read(fd,buf,r) != r) THROW_PE("read");
	    size -= r;
	    buf += r;
	    read += r;
	    if(r < cr) {cl -= r; break;}
	    chunk++;
	    if(chunk == file->chunks.size()) {chunk=(uint64_t)-1; break;}
	    cl = 0;
	    if(lseek(fd,file->chunks[chunk].first,SEEK_SET) == -1) THROW_PE("lseek");
	}
	std::cout << "<<Read" << std::endl;
	return read;
    }
    
    void Handle::write(const uint8_t * buf, uint64_t size) {
	std::cout << ">>Write" << std::endl;
	while(size > 0) {
	    std::cout << "  .." << size << std::endl;
	    if(chunk == (uint64_t)-1) {
		chunk = file->chunks.size();
		uint64_t end=0;
		if(chunk > 0) end=file->chunks[chunk-1].second;
		allocate(size);
		cl=0;
		if(chunk > 0 && file->chunks[chunk-1].second > end ) {
		    --chunk;
		    cl = end-file->chunks[chunk-1].first;
		}
		seekset();
	    }
	    
	    uint64_t cr=file->chunks[chunk].second-file->chunks[chunk].first-cl;
	    uint64_t r = std::min(size,cr);
	    if(::write(fd,buf,r) != r) THROW_PE("write");
	    size -= r;
	    buf += r;
	    if(r < cr) {cl -= r; break;}
	    chunk++;
	    cl = 0;
	    if(chunk == file->chunks.size()) {chunk=(uint64_t)-1; continue;}
	    seekset();
	}
	std::cout << "<<Write" << std::endl;
    }

    uint64_t FS::size(const std::string & name) {
	files_t::iterator i = files.find(name);
	if(i == files.end()) THROW_ERRNOG(ENOENT,"File not found");
	return i->second->size();
    }

    Handle * FS::open(const std::string & name, bool readOnly, Handle * h) {
	ptrw<Handle> nh;
	if(h == NULL) h = nh = new Handle();
	
	files_t::iterator i = files.find(name);
	File * file;
	
	fdw fd = getFd();
	if(i == files.end()) {
	    if(readOnly || this->readonly) THROW_ERRNOG(EROFS, "Readonly file or fs");
	    if(files.size() == maxfiles) THROW_ERRNOG(ENOSPC, "No more free file slots");
	    file = new File();
	    file->usage = 1;
	    file->index = files.size();
	    file->name = name;
	    writeFile(fd, file);
	    files[name] = file;
	    filelist.insert(name);
	    writeHeader(fd);
	} else 
	    file = i->second;

	file->usage++;
	h->fs = this;
	h->fd = fd;
	h->file = file;
	h->chunk = (uint64_t)-1;
	h->cl = 0;
	if(file->chunks.size() > 0) {
	    if(lseek(fd,file->chunks[0].first,SEEK_SET) == -1) THROW_PE("lseek");
	    h->chunk = 0;
	}
	fd.release();
	nh.release();
	return h;
    }

    int FS::getFd() {
	int fd = ::open(path.c_str(),O_NOATIME | O_RDWR);
	if(fd == -1) THROW_ERRNO("Unable to open file '%s'",path.c_str());
	return fd;
    }

    void FS::writeFile(int fd, File * file) {
	uint8_t buf[filesize];
	memset(buf,0,filesize);
	file_t * f = reinterpret_cast<file_t*>(buf);
	strcpy(f->name,file->name.c_str());
	f->chunkCount = file->chunks.size();
	for(size_t i=0; i < file->chunks.size(); ++i) {
	    f->chunks[i].start = file->chunks[i].first;
	    f->chunks[i].end = file->chunks[i].second;
	}
	if(lseek(fd,sizeof(header_t) + filesize*file->index, SEEK_SET) == -1) THROW_PE("lseek");
	if(write(fd,buf,filesize) != filesize) THROW_PE("write");
    }
    
    void FS::create(const std::string & path, uint64_t maxfiles, uint64_t maxchunks) {
	fdw fd = ::open(path.c_str(),O_NOATIME | O_RDWR);
	if(fd == -1) THROW_ERRNO("Unable to open file '%s'",path.c_str());
	off_t size = lseek(fd,0,SEEK_END);
	if(size == -1) THROW_ERRNO("lseek failed");
	header_t header;
	header.magic = magic;
	header.version = 1;
	header.writing = 0;
	header.files = 0;
	header.maxfiles = maxfiles;
	header.maxchunks = maxchunks;
	header.writemounted = 0;
	if(lseek(fd,0,SEEK_SET) == -1) THROW_ERRNO("lseek failed");
	if(write(fd,&header,sizeof(header_t)) != sizeof(header_t)) THROW_ERRNO("write failed");
	size_t s=sizeof(file_t) + sizeof(chunk_t)*maxchunks;
	char buf[s];
	memset(buf,0,s);
	for(int i=0; i< maxfiles; ++i)
	    if(write(fd,buf,s) != s) THROW_ERRNO("write failed");
    }

    void FS::compressFreeSpace() {
	std::cout << ">>compression" << std::endl;
	if(freespace.empty()) return;
	freespace_t::iterator a=freespace.begin();
	freespace_t::iterator b=a;
	freespace_t::iterator i=a;
	for(++i;;++i) {
	    if(i != freespace.end() && i->first == b->second) {
		b=i;
		continue;
	    }
	    if(a != b) {
		uint64_t start=a->first;
		uint64_t end=b->second;
		freespace.erase(a,b);
		freespace.insert( std::make_pair(start,end) );
	    }
	    a = b = i;
	    if(i == freespace.end()) break;
	}
	std::cout << "<<compression" << std::endl;
    }
									       
    void FS::unuse(File * file) {
	file->usage--;
	if(file->usage == 0) {
	    for(size_t i=0; i != file->chunks.size(); ++i)
		freespace.insert(file->chunks[i]);
	    delete(file);
	    compressFreeSpace();
	}
    }

    void FS::writeHeader(int fd) {
	header_t header;
	header.magic = magic;
	header.version = 1;
	header.writing = writing?1:0;
	header.files = files.size();
	header.maxfiles = maxfiles;
	header.maxchunks = maxchunks;
	header.writemounted = readonly?0:1;
	if(lseek(fd,0,SEEK_SET) == -1) THROW_PE("lseek");
	if(write(fd,&header, sizeof(header_t)) == -1) THROW_PE("write"); 
    }

    void FS::unlink(const std::string & name) {
	if(readonly) THROW_ERRNOG(EROFS, "Readonly file or fs");
	files_t::iterator i = files.find(name);
	if(i == files.end()) THROW_ERRNOG(ENOENT,"File not found");
	
	fdw fd = getFd();
	writing = true;
	writeHeader(fd);

	File * file = i->second;
	files.erase(i);
	filelist.erase(name);
	if(file->index != files.size()) {
	    files_t::iterator j = files.begin();
	    for(files_t::iterator i = files.begin(); i != files.end(); ++i)
		if(i->second->index > j->second->index) j=i;
	    
	    assert(j != files.end());
	    j->second->index = file->index;
	    writeFile(fd,j->second);
	    file->index = (uint64_t)-1;
	}
	writing=false;
	writeHeader(fd);
	unuse(file);
    }
}

