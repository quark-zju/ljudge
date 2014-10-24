#include "fs.hpp"
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <list>
#include <mntent.h>
#include <string>
#include <unistd.h>
#include <sys/file.h>
#include <sys/types.h>

using std::string;

extern "C" {
#include "deps/fs/fs.c"
}

const char fs::PATH_SEPARATOR = '/';

int fs::rename (const string& from, const string& to) {
  return fs_rename(from.c_str(), to.c_str());
}

fs::stats * fs::stat (const string& path) {
  return fs_stat(path.c_str());
}

fs::stats * fs::lstat (const string& path) {
  return fs_lstat(path.c_str());
}

int fs::truncate (const string& path, int len) {
  return fs_truncate(path.c_str(), len);
}

int fs::chown (const string& path, int uid, int gid) {
  return fs_chown(path.c_str(), uid, gid);
}

int fs::lchown (const string& path, int uid, int gid) {
  return fs_lchown(path.c_str(), uid, gid);
}

size_t fs::size (const string& path) {
  return fs_size(path.c_str());
}

static string stringize(char * buf) {
  if (!buf) return "";
  string result = buf;
  free(buf);
  return result;
}

string fs::read (const string& path) {
  return stringize(fs_read(path.c_str()));
}

string fs::nread (const string& path, int len) {
  return stringize(fs_nread(path.c_str(), len));
}

int fs::write (const string& path, const char *buffer){
  return fs_write(path.c_str(), buffer);
}

int fs::nwrite (const string& path, const char *buffer, int len){
  return fs_nwrite(path.c_str(), buffer, len);
}

int fs::mkdir (const string& path, int mode){
  return fs_mkdir(path.c_str(), mode);
}

int fs::rmdir (const string& path){
  return fs_rmdir(path.c_str());
}

bool fs::exists (const string& path){
  // fs_exists returns 0 if the file actually exists
  return fs_exists(path.c_str()) == 0;
}


string fs::join(const string& dirname, const string& basename) {
  size_t dirname_len = dirname.length();
  size_t basename_len = basename.length();
  int offset = 0;

  if (dirname_len == 0) return basename;
  else if (dirname[dirname_len - 1] == PATH_SEPARATOR) offset++;
  if (basename_len == 0) return dirname;
  else if (basename[0] == PATH_SEPARATOR) offset++;

  switch (offset) {
    case 0:
      return dirname + PATH_SEPARATOR + basename;
    case 2:
      return dirname + basename.substr(1);
    case 1: default:
      return dirname + basename;
  }
}

string fs::join(const string& path1, const string& path2, const string& path3) {
  return fs::join(fs::join(path1, path2), path3);
}

string fs::dirname(const string& path) {
  size_t pos = path.find_last_of(PATH_SEPARATOR);
  if (pos == string::npos) {
    return "";
  } else {
    return path.substr(0, pos);
  }
}

string fs::basename(const string& path) {
  size_t pos = path.find_last_of(PATH_SEPARATOR);
  if (pos == string::npos) {
    return path;
  } else {
    return path.substr(pos + 1);
  }
}

string fs::extname(const string& path) {
  string name = fs::basename(path);
  size_t pos = name.find_last_of('.');
  if (pos == string::npos) {
    return "";
  } else {
    return name.substr(pos);
  }
}

bool fs::is_dir(const string& path) {
  struct stat buf;
  if (stat(path.c_str(), &buf) == -1) return 0;
  return S_ISDIR(buf.st_mode) ? 1 : 0;
}

bool fs::is_disconnected(const string& path) {
  struct stat buf;
  if (stat(path.c_str(), &buf) == -1) {
    return errno == ENOTCONN;
  }
  return false;
}

int fs::mkdir_p(const string& dir, const mode_t mode) {
  // do nothing if directory exists
  if (is_dir(dir)) return 0;

  // make each dirs
  const char * head = dir.c_str();
  int nmkdir = 0;
  for (const char * p = head; *p; ++p) {
    if (*p == '/' && p > head) {
      int e = mkdir(dir.substr(0, p - head).c_str(), mode);
      if (e == 0) ++nmkdir;
    }
  }
  int e = mkdir(dir.c_str(), mode);

  if (e < 0 /* && errno != EEXIST */) return -1;
  return nmkdir;
}

int fs::rm_rf(const string& path) {
  // TODO use more efficient implement like coreutils/rm

  // try to remove single file or an empty dir
  if (unlink(path.c_str()) == 0) return 0;
  if (rmdir(path.c_str()) == 0) return 0;

  // try to list path contents
  struct dirent **namelist = 0;
  int nlist = scandir(path.c_str(), &namelist, 0, alphasort);

  for (int i = 0; i < nlist; ++i) {
    const char * name = namelist[i]->d_name;
    // skip . and ..
    if (strcmp(name, ".") != 0 && strcmp(name, "..") != 0) fs::rm_rf(path + "/" + name);
    free(namelist[i]);
  }

  if (namelist) free(namelist);

  // try remove empty dir again
  if (rmdir(path.c_str()) == 0) return 0;

  // otherwise something must went wrong
  return -1;
}

bool fs::is_absolute(const string& path) {
  return path.length() > 0 && path.data()[0] == PATH_SEPARATOR;
}

bool fs::is_accessible(const string& path, int mode, const string& work_dir) {
  int dirfd = AT_FDCWD;
  bool result = false;
  if (!work_dir.empty() && !is_absolute(path)) {
    dirfd = open(work_dir.c_str(), O_RDONLY);
    if (dirfd == -1) goto cleanup;
  }
  result = (faccessat(dirfd, path.c_str(), mode, 0) == 0);

cleanup:
  if (dirfd != -1 && dirfd != AT_FDCWD) close(dirfd);
  return result;
}

bool fs::is_mounted(const string& path) {
  bool result = false;
  FILE *fp = setmntent("/proc/mounts", "r");
  if (!fp) goto cleanup;

  for (struct mntent *ent; (ent = getmntent(fp));) {
    if (string(ent->mnt_dir) == path) {
      result = true;
      break;
    }
  }

cleanup:
  if (fp) fclose(fp);
  return result;
}

bool fs::touch(const string& path) {
  FILE *fp = fopen(path.c_str(), "a");
  if (!fp) return false;
  fclose(fp);
  return true;
}

std::list<string> fs::scandir(const string& path) {
  std::list<string> result;

  struct dirent **namelist = 0;
  int nlist = scandir(path.c_str(), &namelist, 0, alphasort);
  for (int i = 0; i < nlist; ++i) {
    const char * name = namelist[i]->d_name;
    // skip . and ..
    if (strcmp(name, ".") != 0 && strcmp(name, "..") != 0) {
      result.push_back(name);
    }
    free(namelist[i]);
  }
  if (namelist) free(namelist);

  return result;
}

fs::ScopedFileLock::ScopedFileLock(const string& path) {
  int fd = open(path.c_str(), O_RDONLY);
  if (fd < 0) return;
  if (flock(fd, LOCK_EX) == 0) {
    this->fd_ = fd;
  } else {
    close(fd);
    this->fd_ = -1;
  }
}

fs::ScopedFileLock::~ScopedFileLock() {
  int fd = this->fd_;
  if (fd < 0) return;
  flock(fd, LOCK_UN);
  close(fd);
}
