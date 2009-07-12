#define FUSE_USE_VERSION 26

#include <fuse.h>
#include <cstring>
#include <lsfs.hh>
#include <string>
#include <iostream>
#include <memory>

#define HANDLE_EXCEPTIONS                        \
  catch(const lsfs::ErrnoException & e) {        \
    return -e.number;                            \
  } catch(const lsfs::InternalError & e) {       \
    std::cerr << e.what() << std::endl;          \
    return -e.number;                            \
  }

lsfs::FS fs;

static int lsfs_mkdir(const char * path, mode_t) {
  try {
    lsfs::Handle h;
    fs.open(std::string(path+1)+"/", false, &h);
    return 0;
  } HANDLE_EXCEPTIONS
}

static int lsfs_rmdir(const char * path) {
  try {
    lsfs::Handle h;
    fs.unlink(std::string(path+1)+"/");
    return 0;
  } HANDLE_EXCEPTIONS
}

static int lsfs_unlink(const char * path) {
  try {
    lsfs::Handle h;
    fs.unlink(std::string(path+1));
    return 0;
  } HANDLE_EXCEPTIONS
}

static int lsfs_getattr(const char *path, struct stat *stbuf)
{
  try {
    memset(stbuf, 0, sizeof(struct stat));
    if(strcmp(path,"/") == 0) {
      stbuf->st_mode = S_IFDIR | 0755;
      stbuf->st_nlink = 2;
      return 0;
    }
    
    const std::set<std::string> &  l = fs.ls();
    std::set<std::string>::const_iterator i = l.lower_bound(path+1);
    if(i == l.end())
      return -ENOENT;
    if(*i == (path+1)) {
      stbuf->st_mode = S_IFREG | 0777;
      stbuf->st_nlink = 1;
      stbuf->st_size = fs.size(path+1);
      return 0;
    }
    size_t x = strlen(path+1);
    if(strncmp(path+1, i->c_str(), x) == 0 && i->c_str()[x] == '/') {
      stbuf->st_mode = S_IFDIR | 0777;
      stbuf->st_nlink = 2;
      return 0;
    }
    return -ENOENT;
  } HANDLE_EXCEPTIONS
}


static int lsfs_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
		       off_t, struct fuse_file_info *)
{
  try {
    std::string prefix=path+1;
    if(prefix != "") prefix.push_back('/');
    const std::set<std::string> &  l = fs.ls();
    
    std::cout << prefix << std::endl;
    
    std::set<std::string> files;
    for(
	std::set<std::string>::const_iterator i = l.lower_bound(prefix);
	i != l.end();
	++i) {
      
      if(i->substr(0,prefix.size()) != prefix) break;
      size_t e=i->find('/',prefix.size());
      if(e != std::string::npos) e -= prefix.size();
      std::string x = i->substr(prefix.size(),e);
      if(x.size()) files.insert(x);
    }
    
    filler(buf, ".", NULL, 0);
    filler(buf, "..", NULL, 0);
    for( std::set<std::string>::const_iterator i = files.begin(); i != files.end(); ++i)
      filler(buf, i->c_str(), NULL, 0);
    
    return 0;
  } HANDLE_EXCEPTIONS
}


int lsfs_open(const char * path, struct fuse_file_info *fi) {
  try {
    lsfs::Handle * h = fs.open(path+1,fi->flags & O_RDONLY);
    fi->fh = reinterpret_cast<size_t>(h);
    return 0;
  } HANDLE_EXCEPTIONS
}

int lsfs_create(const char * path, mode_t, struct fuse_file_info * fi) {
  return lsfs_open(path,fi);
}

int lsfs_read(const char *, char *buf, size_t size, off_t offset, struct fuse_file_info *fi)
{
  std::cout << "Read" << std::endl;
  try {
    lsfs::Handle * h = reinterpret_cast<lsfs::Handle*>(static_cast<size_t>(fi->fh));
    h->seek(offset);
    return h->read(reinterpret_cast<uint8_t*>(buf),size);
  } HANDLE_EXCEPTIONS
}

int lsfs_write(const char *, const char *buf, size_t size, off_t offset, struct fuse_file_info *fi)
{
  std::cout << "Write" << std::endl;
  try {
    lsfs::Handle * h = reinterpret_cast<lsfs::Handle*>(static_cast<size_t>(fi->fh));
    h->seek(offset);
    h->write(reinterpret_cast<const uint8_t*>(buf),size);
    return size;
  } HANDLE_EXCEPTIONS
}

int lsfs_release(const char *, struct fuse_file_info * fi) {
  try {
    lsfs::Handle * h = reinterpret_cast<lsfs::Handle*>(static_cast<size_t>(fi->fh));
    delete h;
    return 0;
  } HANDLE_EXCEPTIONS
} 

int lsfs_utimens(const char *, const struct timespec tv[2]) {return 0;} 

int lsfs_truncate(const char * path, off_t size) {
  try {
    lsfs::Handle h;
    fs.open(path,false,&h);
    h.truncate(size);
    return 0;
  } HANDLE_EXCEPTIONS
}


// int mfs_flush(const char *, struct fuse_file_info * fi) {
//     return 0;
// }

static struct fuse_operations lsfs_oper;

int main(int argc, char *argv[])
{
  memset(&lsfs_oper, 0, sizeof(struct fuse_operations));
  lsfs_oper.readdir = lsfs_readdir;
  lsfs_oper.mkdir = lsfs_mkdir;
  lsfs_oper.rmdir = lsfs_rmdir;
  lsfs_oper.unlink = lsfs_unlink;
  lsfs_oper.getattr = lsfs_getattr;
  lsfs_oper.create = lsfs_create;
  lsfs_oper.open = lsfs_open;
  lsfs_oper.read = lsfs_read;
  lsfs_oper.write = lsfs_write;
  lsfs_oper.release = lsfs_release;
  lsfs_oper.utimens = lsfs_utimens;
  lsfs_oper.truncate = lsfs_truncate;
  struct fuse_args args = FUSE_ARGS_INIT(0, NULL);
  const char * file;
  for(int i = 0; i < argc; i++) {
    if (i == 1)
      file = argv[i];
    else
      fuse_opt_add_arg(&args, argv[i]);
  }
  fs.mount(file,false);
  return fuse_main(args.argc, args.argv, &lsfs_oper, NULL);
}
