#pragma once

#include <list>
#include <string>
#include <unistd.h>

extern "C" {
#include "deps/fs/fs.h"
}

namespace fs {
  typedef fs_stats stats;

  // proxy to fs.h
  int rename(const std::string& from, const std::string& to);
  stats * stat(const std::string& path);
  stats * lstat(const std::string& path);
  int truncate(const std::string& path, int len);
  int chown(const std::string& path, int uid, int gid);
  int lchown(const std::string& path, int uid, int gid); 
  size_t size(const std::string& path);
  std::string read(const std::string& path);
  std::string nread(const std::string& path, int len);
  int write(const std::string& path, const char *buffer);
  int nwrite(const std::string& path, const char *buffer, int len);
  int mkdir(const std::string& path, int mode);
  int rmdir(const std::string& path);
  bool exists(const std::string& path);

  // additional helper methods
  std::string join(const std::string& dirname, const std::string& basename);
  std::string join(const std::string&, const std::string&, const std::string&);
  std::string dirname(const std::string& path);
  std::string basename(const std::string& path);
  std::string extname(const std::string& path);
  bool is_disconnected(const std::string& path);
  bool is_dir(const std::string& path);
  int mkdir_p(const std::string& dir, const mode_t mode = 0755);
  int rm_rf(const std::string& path);
  bool is_absolute(const std::string& path);
  bool is_accessible(const std::string& path, int mode = R_OK, const std::string& work_dir = "");
  bool touch(const std::string& path);
  bool is_mounted(const std::string& path);
  std::list<std::string> scandir(const std::string& path);
  std::string resolve(const std::string& path);

  extern const char PATH_SEPARATOR;

  class ScopedFileLock {
    public:
      ScopedFileLock(const std::string& path);
      ~ScopedFileLock();
    private:
      int fd_;
  };
}
