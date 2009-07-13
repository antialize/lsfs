#define FUSE_USE_VERSION 26

#include <fuse.h>
#include <cstring>
#include <lsfs.hh>
#include <string>
#include <iostream>
#include <memory>

#define HANDLE_EXCEPTIONS                        \
  catch(const lsfs::ErrnoException & e) {        \
    std::cerr << e.what() << std::endl;          \
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
  try {
    lsfs::Handle * h = reinterpret_cast<lsfs::Handle*>(static_cast<size_t>(fi->fh));
    h->seek(offset);
    return h->read(reinterpret_cast<uint8_t*>(buf),size);
  } HANDLE_EXCEPTIONS
}

int lsfs_write(const char *, const char *buf, size_t size, off_t offset, struct fuse_file_info *fi)
{
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


static struct fuse_operations lsfs_oper;
enum {
  KEY_HELP,
  KEY_VERSION,
};

//#define MYFS_OPT(t, p, v) { t, offsetof(struct myfs_config, p), v }

static struct fuse_opt myfs_opts[] = {
  {"readonly", 0, 1},
  {"-r", 0, 1},
  {"--readonly", 0, 1},
   FUSE_OPT_KEY("-V",             KEY_VERSION),
   FUSE_OPT_KEY("--version",      KEY_VERSION),
   FUSE_OPT_KEY("-h",             KEY_HELP),
   FUSE_OPT_KEY("--help",         KEY_HELP),
   {NULL, -1 ,0}
};


char * dev;
#include <cstdlib>

static int lsfs_opt_proc(void *data, const char *arg, int key, struct fuse_args *outargs)
{
  switch(key)  {
  case KEY_VERSION:
    fprintf(stderr, "lsfs version: 0.1\n");
    fuse_opt_add_arg(outargs, "--version");
    fuse_main(outargs->argc, outargs->argv, &lsfs_oper, NULL);
    exit(0);
  case KEY_HELP:
    fprintf(stderr,
	    "usage: %s device mountpoint [options]\n"
	    "\n"
	    "general options:\n"
	    "    -o opt,[opt...]  mount options\n"
	    "    -h   --help      print help\n"
	    "    -V   --version   print version\n"
	    "\n"
	    "lsfs options:\n"
	    "    -o readonly\n"
	    "    -r NUM           same as '-o readonly'\n"
	    "    --readonly       same as '-o readonly'\n"
	    "\n"
	    , outargs->argv[0]);
    fuse_opt_add_arg(outargs, "-ho");
    fuse_main(outargs->argc, outargs->argv, &lsfs_oper, NULL);
    exit(1);
  case FUSE_OPT_KEY_NONOPT:
    if(dev == NULL) {
      dev = strdup(arg);
      return 0;
    }
    break;
  }
  return 1;
}

size_t readonly;

int main(int argc, char *argv[])
{
  readonly = 0;

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

  struct fuse_args args = FUSE_ARGS_INIT(argc, argv);
  fuse_opt_parse(&args, &readonly, myfs_opts, lsfs_opt_proc);
  fs.mount(dev,readonly);
  return fuse_main(args.argc, args.argv, &lsfs_oper, NULL);
}
