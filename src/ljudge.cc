#ifndef _GNU_SOURCE
# define _GNU_SOURCE
#endif

#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cctype>
#include <fcntl.h>
#include <list>
#include <map>
#include <string>
#include <sys/prctl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include <vector>

#include "sha1.hpp"
#include "fs.hpp"
#include "term.hpp"
#include "deps/picojson/picojson.h"
#include "deps/tinyformat/tinyformat.h"

extern "C" {
#include "deps/log.h/log.h"
int debug_level;
}

using std::list;
using std::map;
using std::string;
using std::vector;
using tfm::format;
namespace j = picojson;

#define fatal(...) { fprintf(stderr, __VA_ARGS__); fprintf(stderr, "\n"); cleanup_exit(1); }

#define LJUDGE_VERSION "v0.1"

// truncate log size, ex. compiler log, stdout, stderr, etc.
#define TRUNC_LOG 65535

// sub-directory names in cache_dir
#define SUBDIR_CHROOT "chroot"
#define SUBDIR_USER_CODE "code"
#define SUBDIR_CHECKER "checker"
#define SUBDIR_TEMP "tmp"

// envs (config file name prefixes)
#define ENV_CHECK "check"
#define ENV_COMPILE "compile"
#define ENV_EXTRA "extra"
#define ENV_RUN "run"
#define ENV_VERSION "version"

// config file extensions
#define EXT_CMD_LIST ".cmd_list"
#define EXT_EXE_NAME ".exe_name"
#define EXT_FS_REGEX ".fs_regex"
#define EXT_LRUN_ARGS ".lrun_args"
#define EXT_NAME ".name"
#define EXT_OPT_FAKE_PASSWD "fake_passwd"
#define EXT_FS_OVERRIDE ".fs_override"
#define EXT_SRC_NAME ".src_name"

// config file names which are options
#define OPTION_VALUE_TRUE "true"
#define OPTION_VALUE_FALSE "false"

// default values
#define DEFAULT_EXE_NAME "a.out"
#define DEFAULT_CONF_DIR "_default"

#define DEV_NULL "/dev/null"
#define ETC_PASSWD "/etc/passwd"

namespace TestcaseResult {
  /* [[[cog
    import cog
    for name in ['INTERNAL_ERROR', 'NON_ZERO_EXIT_CODE', 'MEMORY_LIMIT_EXCEEDED', 'TIME_LIMIT_EXCEEDED', 'OUTPUT_LIMIT_EXCEEDED', 'PRESENTATION_ERROR', 'ACCEPTED', 'RUNTIME_ERROR', 'FLOAT_POINT_EXCEPTION', 'SEGMENTATION_FAULT', 'WRONG_ANSWER']:
      cog.outl('const string %(name)s = "%(name)s";' % {'name': name})
  ]]] */
  const string INTERNAL_ERROR = "INTERNAL_ERROR";
  const string NON_ZERO_EXIT_CODE = "NON_ZERO_EXIT_CODE";
  const string MEMORY_LIMIT_EXCEEDED = "MEMORY_LIMIT_EXCEEDED";
  const string TIME_LIMIT_EXCEEDED = "TIME_LIMIT_EXCEEDED";
  const string OUTPUT_LIMIT_EXCEEDED = "OUTPUT_LIMIT_EXCEEDED";
  const string PRESENTATION_ERROR = "PRESENTATION_ERROR";
  const string ACCEPTED = "ACCEPTED";
  const string RUNTIME_ERROR = "RUNTIME_ERROR";
  const string FLOAT_POINT_EXCEPTION = "FLOAT_POINT_EXCEPTION";
  const string SEGMENTATION_FAULT = "SEGMENTATION_FAULT";
  const string WRONG_ANSWER = "WRONG_ANSWER";
  /* [[[end]]] */
};

struct Limit {
  double cpu_time;   // seconds
  double real_time;  // seconds
  long long memory;  // bytes
  long long output;  // bytes
};

struct Testcase {
  string input_path;
  string output_path;
  Limit runtime_limit;
  Limit checker_limit;
};

struct Options {
  string etc_dir;
  string cache_dir;
  string user_code_path;
  string checker_code_path;
  Limit compiler_limit;
  vector<Testcase> cases;
  map<string, string> envs;
  bool pretty_print;
  bool skip_checker;  // if true, do not run can checker, but capture user program's output
  bool keep_stdout;
  bool keep_stderr;
  bool direct_mode;  // if true, just run the program and prints the result
};

struct LrunArgs : public vector<string> {
  void append(const Limit& limit) {
    /* [[[cog
      import cog
      opts = {'cpu_time': 'g', 'real_time': 'g', 'output': 'Ld', 'memory': 'Ld'}
      for opt, flag in opts.items():
        cog.out(
          '''
          if (%(opt)s > 0) {
            push_back("--max-%(opt_m)s");
            push_back(format("%%%(flag)s", %(opt)s));
          }
          ''' % {'opt': 'limit.' + opt,
                 'opt_m': opt.replace('_', '-'),
                 'flag': flag}, trimblanklines=True)
    ]]] */
    if (limit.real_time > 0) {
      push_back("--max-real-time");
      push_back(format("%g", limit.real_time));
    }
    if (limit.cpu_time > 0) {
      push_back("--max-cpu-time");
      push_back(format("%g", limit.cpu_time));
    }
    if (limit.memory > 0) {
      push_back("--max-memory");
      push_back(format("%Ld", limit.memory));
    }
    if (limit.output > 0) {
      push_back("--max-output");
      push_back(format("%Ld", limit.output));
    }
    /* [[[end]]] */
  }

  void append(const string& arg1) {
    push_back(arg1);
  }

  void append(const string& arg1, const string& arg2) {
    push_back(arg1);
    push_back(arg2);
  }

  void append(const string& arg1, const string& arg2, const string& arg3) {
    push_back(arg1);
    push_back(arg2);
    push_back(arg3);
  }

  void append(const vector<string>& args) {
    insert(end(), args.begin(), args.end());
  }

  void append(const list<string>& args) {
    insert(end(), args.begin(), args.end());
  }

  void append_default() {
#ifndef NDEBUG
    if (getenv("LJUDGE_DEBUG_LRUN")) push_back("--debug");
#endif
    append("--reset-env", "true");
    append("--basic-devices", "true");
    append("--remount-dev", "true");
    append("--network", "false");
    append("--chdir", "/tmp");
    append("--env", "ONLINE_JUDGE", "1");
    append("--env", "HOME", "/tmp");
    append("--env", "PATH", "/usr/bin:/bin:/etc/alternatives:/usr/local/bin");
    // Pass as-is
    static const char pass_envs[][16] = {"JAVA_HOME"};
    for (size_t i = 0; i < sizeof(pass_envs) / sizeof(pass_envs[0]); ++i) {
      const char *env_val = getenv(pass_envs[i]);
      if (env_val) append("--env", pass_envs[i], env_val);
    }
  }
};

struct LrunResult {
  string error;
  long long memory;
  double cpu_time;
  double real_time;
  bool signaled;
  int exit_code;
  int term_sig;
  string exceed;
};

struct CompileResult {
  string log;
  string error;
  string work_dir;
  bool success;
};

string string_chomp(const string& str) {
  if (str.empty() || str[str.length() - 1] != '\n') return str;
  return str.substr(0, str.length() - 1);
}

vector<string> string_split(const string& str, const string& delim) {
  vector<string> result;
  if (delim.empty()) {
    result.push_back(str);
  } else {
    size_t pos = 0, start = 0;
    for (int running = 1; running;) {
      pos = str.find(delim, start);
      size_t len;
      if (pos == string::npos) {
        len = string::npos;
        running = 0;
      } else {
        len = pos - start;
      }
      result.push_back(str.substr(start, len));
      start = pos + delim.length();
    }
  }
  return result;
}

void string_replacei(string& str, const string& from, const string& to) {
  size_t pos = 0;
  while ((pos = str.find(from, pos)) != string::npos) {
    str.replace(pos, from.length(), to);
    pos += to.length();
  }
}

string which(const string& name, int access = R_OK | X_OK) {
  string result;
  char * path_env = getenv("PATH");
  if (path_env) {
    vector<string> dirs = string_split(path_env, ":");
    for (int i = 0; i < (int)dirs.size(); ++i) {
       string path = fs::join(dirs[i], name);
       if (fs::is_accessible(path, access)) {
         result = path;
         break;
       }
    }
  }
  return result;
}


static list<string> cleanup_paths;

void register_cleanup_path(const string& path) {
  if (path.empty()) return;
  cleanup_paths.push_back(path);
}

int cleanup_exit(int code) {
#ifndef NDEBUG
  if (!getenv("DEBUG") && !getenv("NOCLEANUP")) {
#endif
    for (__typeof(cleanup_paths.begin()) it = cleanup_paths.begin(); it != cleanup_paths.end(); ++it) {
      string path = *it;
      if (!fs::exists(path)) continue;
      log_debug("cleaning: rm -rf %s", path.c_str());
      fs::rm_rf(path);
    }
#ifndef NDEBUG
  } else {
    log_debug("skip cleaning up");
  }
#endif
  exit(code);
}


/**
 * Example:
 *   get_config_path("/etc/ljudge", "/path.to/bla.clang.cc", "foo")
 *
 *   returns "/etc/ljudge/clang.cc/foo"  # if it exists
 *   returns "/etc/ljudge/cc/foo"        # if it exists and the above one doesn't exist
 *   returns "/etc/ljudge/_default/foo"  # if it exists and the above two don't exist, and strit is false
 *   returns ""                          # if the above three don't exist
 */
static string get_config_path(const string& etc_dir, const string& code_path, const string& config_name, bool strict = false) {
  string basename = fs::basename(code_path);
  log_debug("get_config_path: %s %s", config_name.c_str(), basename.c_str());

  for (size_t pos = 0; (pos = basename.find('.', pos)) != string::npos; ) {
    string ext = basename.substr(++pos);
    string path = fs::join(etc_dir, ext, config_name);
    if (fs::exists(path)) return path;
  }
  if (!strict) {
    string path = fs::join(etc_dir, DEFAULT_CONF_DIR, config_name);
    if (fs::exists(path)) return path;
  }

  return "";
}

/**
 * Example:
 *   cat /etc/ljudge/cc/cmd.compile
 *   # comment line
 *   g++
 *   -Wall
 *   $src
 *   -o
 *   $dest
 *
 *   get_config_list("/etc/ljudge", "/path.to/foo.cc", "cmd.compile")
 *   returns {"g++", "-Wall", "$src", "-o", "$dest"}
 *
 *   get_config_list("/etc/ljudge", "/path.to/foo.cc", "notfound")
 *   returns {}
 */
static list<string> get_config_list(const string& etc_dir, const string& code_path, const string& name, bool strict = false) {
  string path = get_config_path(etc_dir, code_path, name, strict);
  log_debug("get_config_list: %s", path.c_str());
  list<string> result;

  char *line = NULL;
  size_t line_size = 0;

  if (!path.empty()) {
    FILE *fp = fopen(path.c_str(), "r");
    if (!fp) fatal("can not open %s for reading", path.c_str());
    for (ssize_t line_len = 0; line_len != -1; line_len = getline(&line, &line_size, fp)) {
      if (line_len <= 1 || line[0] == '#') continue;
      if (line[line_len - 1] == '\n') line[line_len - 1] = 0;  // chomp
      // ltrim
      const char *p = line;
      while (*p == ' ') ++p;
      result.push_back(p);
    }
    fclose(fp);
  }
  if (line) free(line);
  return result;
}

static string get_config_content(const string& etc_dir, const string& code_path, const string& name, const string& fallback = "", bool strict = false) {
  string result;
  string config_path = get_config_path(etc_dir, code_path, name, strict);
  log_debug("get_config_content: %s %s", name.c_str(), config_path.c_str());
  if (!config_path.empty()) {
    result = string_chomp(fs::read(config_path));
  }
  if (result.empty()) result = fallback;
  return result;
}

static string get_src_name(const string& etc_dir, const string& code_path) {
  string fallback = "a" + fs::extname(code_path);
  return get_config_content(etc_dir, code_path, ENV_COMPILE EXT_SRC_NAME, fallback);
}

static string prepare_dummy_passwd(const string& cache_dir) {
  string path = fs::join(cache_dir, format("tmp/etc/passwd-%d", (int)getuid()));
  string content = format("nobody:%d:%d::/tmp:/bin/false\n", (int)getuid(), (int)getgid());
  if (!fs::exists(path) || fs::read(path) != content) {
    fs::mkdir_p(fs::dirname(path));
    fs::touch(path);
    fs::ScopedFileLock lock(path);
    fs::write(path, content.c_str());
  }
  return path;
}

static list<string> get_override_lrun_args(const string& etc_dir, const string& cache_dir, const string& code_path, const string& env, const string& chroot_path) {
  list<string> result;
  // Hide real /etc/passwd (required by Python) on demand
  if (fs::exists(fs::join(chroot_path, ETC_PASSWD)) && get_config_content(etc_dir, code_path, format("%s%s", env, EXT_OPT_FAKE_PASSWD), OPTION_VALUE_TRUE) == OPTION_VALUE_TRUE) {
    string passwd_path = prepare_dummy_passwd(cache_dir);
    result.push_back("--bindfs-ro");
    result.push_back(fs::join(chroot_path, ETC_PASSWD));
    result.push_back(passwd_path);
  }

  string override_dir = get_config_path(etc_dir, code_path, format("%s%s", env, EXT_FS_OVERRIDE));
  if (override_dir.empty()) return result;

  list<string> files = fs::scandir(override_dir);
  for (__typeof(files.begin()) it = files.begin(); it != files.end(); ++it) {
    // treat "__" as "/"
    string name = *it;
    string path = name;
    string_replacei(path, "__", "/");
    if (fs::is_accessible(fs::join(chroot_path, path), R_OK)) {
      result.push_back("--bindfs-ro");
      result.push_back(fs::join(chroot_path, path));
      result.push_back(fs::join(override_dir, name));
    }
  }
  return result;
}

// try to keep only lrun "safe" args
template<typename L>
static L filter_user_lrun_args(const L& items) {
  L result;
  int next_safe = 0;
  for (__typeof(items.begin()) it = items.begin(); it != items.end(); ++it) {
    string item = string(*it);
    if (next_safe > 0) {
      result.push_back(item);
      --next_safe;
      continue;
    }
    if (item == "--syscalls" || item == "--domainname" || item == "--hostname" || item == "--ostype" \
        || item == "--osrelease" || item == "--osversion") {
      next_safe = 1;
    } else {
      log_info("lrun arg '%s' is unsafe, dropping it and following args", item.c_str());
      break;
    }
    result.push_back(item);
  }
  return result;
}

template<typename L>
static L escape_list(const L& items, const map<string, string>& mappings) {
  L result;
  for (__typeof(items.begin()) it = items.begin(); it != items.end(); ++it) {
    string item = string(*it);
    for (__typeof(mappings.begin()) it = mappings.begin(); it != mappings.end(); ++it) {
      const string& k = it->first;
      const string& v = it->second;
      string_replacei(item, k, v);
    }
    result.push_back(item);
  }
  return result;
}

static string shell_escape(const string& str) {
  bool should_escape = false;
  static const char safe_chars[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-+=./$:";
  for (size_t i = 0; i < str.length(); ++i) {
    if (strchr(safe_chars, str[i]) == NULL) {
      should_escape = true;
      break;
    }
  }

  if (!should_escape) return str;

  string result = "'";
  for (size_t i = 0; i < str.length(); ++i) {
    char c = str[i];
    if (c == '\'') {
      result += "'\"'\"'";
    } else {
      result += c;
    }
  }
  return result + "'";
}

static string shell_escape(const list<string>& items) {
  string result = "";
  for (__typeof(items.begin()) it = items.begin(); it != items.end(); ++it) {
    if (!result.empty()) result += " ";
    result += shell_escape(*it);
  }
  return result;
}

static string get_random_hash(int len = 40) {
  static const char chars[] = "0123456789abcdef";
  static const int MIN_LEN = 4;
  if (len < MIN_LEN) len = MIN_LEN;

  string result;
  result.reserve(len);
  for (int i = 0; i < len; ++i) result += chars[rand() % (sizeof(chars) - 1)];
  return result;
}


static void ensure_system(const string& cmd) {
  log_debug("running: %s", cmd.c_str());
  int ret = system(cmd.c_str());
  if (ret != 0) fatal("failed to run %s", cmd.c_str());
}

static string prepare_chroot(const string& etc_dir, const string& code_path, const string& env, const string& base_dir) {
  string readable_config = get_config_path(etc_dir, code_path, format("%s%s", env, EXT_FS_REGEX));
  log_debug("prepare_chroot: %s", readable_config.c_str());

  // previously it's "re:///tmp" because lrun --bindfs checks write permission and we always bind to "/tmp".
  // newer lrun does not require write permission so it could be empty.
  string writable_config = "";

  string content = "";
  if (!readable_config.empty()) content += format("r%s", fs::read(readable_config));

  string dest = fs::join(base_dir, sha1(content));
  if (fs::is_disconnected(dest)) {
    log_info("%s is disconnected. try to umount it", dest.c_str());
    int ret = system(format("fusermount -u %s", shell_escape(dest)).c_str());
    if (ret != 0) log_info("failed to umount %s", dest.c_str());
  }

  if (fs::mkdir_p(dest) < 0) fatal("can not mkdir: %s", dest.c_str());

  do {
    fs::ScopedFileLock lock(dest);

    if (fs::is_mounted(dest)) {
      log_debug("already mounted: %s. use it directly", dest.c_str());
      break;
    }

    string cmd = format("filterefs %s -o allow_other,nonempty", shell_escape(dest));
    if (!readable_config.empty()) {
      cmd += format(" --readable-config %s ", shell_escape(readable_config));
    }
    if (!writable_config.empty()) {
      cmd += format(" --writable-config %s ", shell_escape(writable_config));
    }
    if (!readable_config.empty()) {
      string comment = fs::join(fs::basename(fs::dirname(readable_config)), env);
      cmd += format(" --comment %s ", comment);
    }
    ensure_system(cmd);

    // wait 2s until mount finishes
    int mounted = 0;
    for (int i = 0; i < 20; ++i) {
      if (fs::is_mounted(dest)) {
        mounted = 1;
        break;
      }
      usleep(100000); // 0.1s
    }
    if (!mounted) fatal("%s is not mounted correctly", dest.c_str());
  } while (false);

  return dest;
}

static void print_usage() {
  fprintf(stderr,
      "Compile, run, judge and print response JSON:\n"
      "  ljudge --user-code (or -u) user-code-path\n"
      "         [--checker-code (or -c) checker-code-path\n"
      "         --input (or -i) input-path --output (or -o) output-path\n"
      "         [--input input-path --output output-path] ...\n"
      "\n"
      "Compile, run and print response JSON:\n"
      "  ljudge --skip-checker (implies --keep-stdout)\n"
      "         --user-code user-code-path\n"
      "         [--input input-path] ...\n"
      "\n"
      "Compile, run, print output instead of JSON response (the \"direct mode\"):\n"
      "  ljudge user-code-path\n"
      "\n"
      "Available options: (put these before the first `--input`)\n"
      "  ljudge [--etc-dir path] [--cache-dir path]\n"
      "         [--keep-stdout] [--keep-stderr]\n"
      "         [--max-cpu-time seconds] [--max-real-time seconds]\n"
      "         [--max-memory byes] [--max-output bytes]\n"
      "         [--max-checker-cpu-time seconds] [--max-checker-real-time seconds]\n"
      "         [--max-checker-memory byes] [--max-checker-output bytes]\n"
      "         [--max-compiler-cpu-time seconds] [--max-compiler-real-time seconds]\n"
      "         [--max-compiler-memory byes] [--max-compiler-output bytes]\n"
      "         [--env name value] [--env name value] ...\n"
      "\n"
      "Check environment:\n"
      "  ljudge --check\n"
      "\n"
      "Print compiler / interpreter versions:\n"
      "  ljudge --compiler-versions      (only list compilers installed)\n"
      "  ljudge --all-compiler-versions  (including configured but not installed ones)\n"
      "\n"
      "Show help, version:\n"
      "  ljudge --help (or -h)\n"
      "  ljudge --version (or -v)\n"
      "\n"
      "Note:\n"
      "  ljudge will truncate any output (compiler log, stdout, stderr, etc.)\n"
      "  longer than %u bytes.\n"
      "\n", (unsigned) TRUNC_LOG);
  cleanup_exit(0);
}

static void print_version() {
  printf("ljudge %s\n", LJUDGE_VERSION);
  cleanup_exit(0);
}

static string check_output(const string& command, bool capture_stderr = false) {
  string result;
  do {
    // check lrun
    string real_command = command + " <" DEV_NULL;
    if (real_command.find(" 2>") == string::npos) {
      if (capture_stderr) real_command += " 2>&1";
      else real_command += " 2>" DEV_NULL;
    }

    FILE * fp = popen(real_command.c_str(), "r");
    if (!fp) break;

    char buf[16384];
    for (;;) {
      size_t bytes = fread(buf, 1, sizeof(buf) - 1, fp);
      buf[bytes] = 0;
      if (bytes == 0) break;
      result += buf;
    }
    pclose(fp);
  } while (false);
  return result;
}

static void print_checkpoint(const string& name, bool passed, const string& solution) {
#ifndef NDEBUG
  if (getenv("YOU_SHALL_NOT_PASS")) passed = false;
#endif
  term::set(term::attr::BOLD, term::fg::WHITE, passed ? term::bg::GREEN : term::bg::RED);
  printf(passed ? " Y " : " N ");
  term::set();
  term::set(term::attr::BOLD);
  printf(" %s\n", name.c_str());
  term::set();
  if (!passed) {
    string indented = solution;
    string_replacei(indented, "\n", "\n    ");
    printf("    %s\n\n", indented.c_str());
  }
}

static void print_checkfail(const string& name, const string& message, char symbol = '!') {
  term::set(term::attr::BOLD, term::fg::WHITE, symbol == 'S' || symbol == 'W' ? term::bg::YELLOW : term::bg::RED);
  printf(" %c ", symbol);
  term::set();
  term::set(term::attr::BOLD);
  printf(" %s\n", name.c_str());
  term::set();
  string indented = message;
  string_replacei(indented, "\n", "\n    ");
  printf("    %s\n\n", indented.c_str());
}

static bool is_cgroup_enabled(const string& subsystem) {
  if (!fs::exists("/proc/cgroups")) return false;
  FILE * fp = fopen("/proc/cgroups", "r");
  if (!fp) return false;
  int enabled_bit = 0;
  char name[32], enabled[16];
  while (fscanf(fp, "%31s %*s %*s %15s", name, enabled) == 2) {
    if (subsystem == string(name)) {
      enabled_bit = (strcmp(enabled, "0") != 0);
      break;
    }
  }
  if (fp) fclose(fp);
  return enabled_bit;
}

static void do_check() {
  if (getuid() == 0) {
    fprintf(stderr,
        "Running ljudge --check using root is not supported.\n"
        "Please switch to a non-root user and try again.\n");
    exit(1);
  }

  string username = getenv("USER") ? getenv("USER") : "username";

  { // cgroup
    print_checkpoint(
        "cgroup memory controller is enabled",
        is_cgroup_enabled("memory"),
        "This is common on Debian-based systems. Add `cgroup_enable=memory`\n"
        "to kernel parameter and reboot. If you are using GRUB2, try:\n\n"
        "  grep -q cgroup_enable /etc/default/grub || {\n"
        "    S='s/CMDLINE_LINUX=\"/CMDLINE_LINUX=\"cgroup_enable=memory /'\n"
        "    sudo sed -i \"$S\" /etc/default/grub\n"
        "    sudo update-grub2\n"
        "    sudo reboot\n"
        "  }");
    print_checkpoint(
        "cgroup cpuacct, devices, freezer controllers are enabled",
        is_cgroup_enabled("cpuacct") && is_cgroup_enabled("devices") && is_cgroup_enabled("freezer"),
        "Most modern Linux distributions have cgroup enabled by default.\n"
        "Upgrade the kernel or switch to another distribution.");
  }

  do { // lrun
    string lrun_path = which("lrun");
    if (lrun_path.empty()) {
      string lrun_path_non_exec = which("lrun", F_OK);
      if (lrun_path_non_exec.empty()) {
        print_checkfail(
            "lrun not found",
            "lrun is required. Please install it.");
      } else {
        print_checkfail(
            "lrun is not executable",
            "lrun is installed but the current user cannot execute it.\n"
            "This is probably because the current user is not in `lrun`\n"
            "group. To fix it by adding the user to `lrun` group:\n\n" +
            format("  sudo gpasswd -a %s lrun", username));
      }
      break;
    }
    string lrun_help = check_output("lrun --help 2>&1");
    print_checkpoint(
        "lrun supports --syscalls",
        lrun_help.find("--syscalls") != string::npos,
        "lrun is compiled without libseccomp support.\n"
        "This means all syscall filters will cause ljudge to\n"
        "not work correctly. Install related libseccomp packages\n"
        "and recompile lrun.");
    print_checkpoint(
        "lrun supports --bindfs-ro",
        lrun_help.find("--bindfs-ro") != string::npos,
        "Please upgrade lrun to at least v1.0.0");
    print_checkpoint(
        "lrun actually works",
        check_output("lrun echo foofoo 2>" DEV_NULL).find("foofoo") != string::npos,
        "lrun doesn't work. Please make sure other issues are resolved\n"
        "and try `lrun --debug echo foo` to get some help.");
  } while (false);

  do { // filterefs, fuse
    string filterefs_path = which("filterefs");
    if (filterefs_path.empty()) {
      print_checkfail(
          "filterefs not found",
          "filterefs is required. Please install it.");
      break;
    }
    if (!(fs::exists("/dev/fuse"))) {
      print_checkfail(
          "fuse not detected",
          "Please install fuse first");
      break;
    }

    print_checkpoint(
        "current user can write /dev/fuse",
        fs::is_accessible("/dev/fuse", W_OK),
        "ljudge requires FUSE-based program filterefs to work properly,\n"
        "which needs write access to /dev/fuse. Some OS requires the user\n"
        "to be a member of `fuse` group to write /dev/fuse. If that is the\n"
        "case, run:\n\n" +
        format("  sudo gpasswd -a %s fuse", username));

    string filterefs_help = check_output("filterefs --help 2>&1");
    print_checkpoint(
        "filterefs supports \"re://\" config path",
        filterefs_help.find("re://") != string::npos,
        "Please upgrade filterefs to at least v0.3");
    if (!fs::is_accessible("/etc/fuse.conf")) {
      print_checkfail(
          "/etc/fuse.conf is not accessible",
          "Skip related checks.\n"
          "Some OS requires the user to be a member of `fuse` group to\n"
          "read that. If that is the case, run:\n\n" +
          format("  sudo gpasswd -a %s fuse", username), '!');
      break;
    }
    string fuse_conf = fs::read("/etc/fuse.conf");
    print_checkpoint(
        "fuse.conf has user_allow_other",
        ("\n" + fuse_conf).find("\nuser_allow_other") != string::npos,
        "ljudge uses filterefs to provide chroot roots. To be able\n"
        "to chroot into a FUSE mount point, it must be mounted with\n"
        "`-o allow_other`. By default, FUSE does not allow non-root\n"
        "users to use `allow_other`. To fix this issue, run:\n\n"
        "  sudo bash -c 'echo user_allow_other >> /etc/fuse.conf'");
  } while (false);

  { // kernel
    if (fs::nread("/proc/sys/debug/exception-trace", 1) == "1") {
      print_checkfail(
          "debug.exception-trace is 1",
          "Programs being judged may die in many ways, some of which\n"
          "will write the kernel log. Consider set the flag to 0 to\n"
          "keep the kernel log clean:\n\n"
          "  sudo sysctl -w debug.exception-trace=0\n"
          "  echo 'debug.exception-trace=0' | \\\n"
          "    sudo tee /etc/sysctl.d/99-disable-trace.conf", 'W');
    }

    #ifndef PR_GET_NO_NEW_PRIVS
    # define PR_GET_NO_NEW_PRIVS 39
    #endif

    if (prctl(PR_GET_NO_NEW_PRIVS, 0, 0, 0, 0) == -1) {
      print_checkfail(
          "prctl has no NO_NEW_PRIVS support",
          "You are running an old kernel which has no prctl NO_NEW_PRIVS\n"
          "support. This may cause potencial security issues. However,\n"
          "ljudge uses FUSE's `-o nosuid` to counter these issues. Not a\n"
          "big deal. Upgrading kernel is recommended.", 'W');
    }

    if (!fs::exists("/proc/self/ns/pid")) {
      print_checkfail(
          "kernel does not have full pid namespace support",
          "lrun will use a legacy method to run programes.\n"
          "Not a big deal. But upgrading kernel is recommended.", 'W');
    }

    if (sysconf(_SC_ARG_MAX) < 4096) {
      print_checkfail(
          "Maximum length of arguments for a new process is too small.",
          "Not a serious one. But upgrading kernel is recommended.", 'W');
    }
  }

  cleanup_exit(0);
}

// find something like a.b.c from a long string
static string scan_version_string(const string& content) {
  string result;
  bool current_word_is_version = false;
  for (size_t i = 0; i < content.length(); ++i) {
    char c = content[i];
    if (c >= '0' && c <= '9') {
      result += c;
      current_word_is_version = true;
    } else if (c == '.') {
      if (current_word_is_version) result += c;
    } else {
      if (current_word_is_version) {
        // exiting version, do check
        // remove tailing dot
        if (result.length() > 0 && result[result.length() - 1] == '.') result = result.substr(0, result.length() - 1);
        if (strchr(result.c_str(), '.') != NULL && result.length() >= 2) return result;
        // no '.', not a version string
        result = "";
      }
      current_word_is_version = false;
    }
  }
  return result;
}

static void fetch_compiler_versions(j::object& result, const string& etc_dir, bool only_present = true) {
  // scan etc_dir
  list<string> exts = fs::scandir(etc_dir);
  for (__typeof(exts.begin()) it = exts.begin(); it != exts.end(); ++it) {
    const string& ext = *it;
    if (ext == DEFAULT_CONF_DIR) continue;

    // pick version.cmd_list and run
    string dummy_code_path = format("a.%s", ext);
    string version_cmd = shell_escape(get_config_list(etc_dir, dummy_code_path, ENV_VERSION EXT_CMD_LIST, true /* strict */));

    // version cmd is required for every
    if (version_cmd.empty()) continue;
    string content = check_output(version_cmd, true /* stderr */);

    // scan version string from the output
    // if no version found, hide the compiler from output list
    string version = scan_version_string(content);

    j::object jo;
    if (!version.empty()) {
      jo["version"] = j::value(version);
    } else {
      if (only_present) continue;
      else jo["version"] = j::value();
    }

    // get compiler / interpreter name
    // if not set, use the first name of compile cmd or run cmd
    // besides, get compile and run commands
    string name = string_chomp(get_config_content(etc_dir, dummy_code_path, ENV_VERSION EXT_NAME, "" /* default */, true /* strict */));
    list<string> compile_cmds = get_config_list(etc_dir, dummy_code_path, ENV_COMPILE EXT_CMD_LIST, true /* strict */);
    list<string> run_cmds = get_config_list(etc_dir, dummy_code_path, ENV_RUN EXT_CMD_LIST, true /* strict */);

    if (name.empty() && !compile_cmds.empty()) name = *compile_cmds.begin();
    else if (name.empty() && !run_cmds.empty()) name = *run_cmds.begin();
    else if (name.empty()) name = ext;

    if (!compile_cmds.empty()) jo["compile_cmd"] = j::value(shell_escape(compile_cmds));
    if (!run_cmds.empty()) jo["run_cmd"] = j::value(shell_escape(run_cmds));
    jo["name"] = j::value(name);
    result[ext] = j::value(jo);
  }
}

static void print_compiler_versions(const Options& opts, bool only_present = true) {
  j::object jo;
  fetch_compiler_versions(jo, opts.etc_dir, only_present);
  printf("%s", j::value(jo).serialize(opts.pretty_print).c_str());
  exit(0);
}

static bool is_language_supported(const string& etc_dir, const string& code_path) {
  // a supported language must have version command configured
  if (get_config_path(etc_dir, code_path, ENV_VERSION EXT_CMD_LIST, true).empty()) {
    return false;
  } else {
    return true;
  }
}

static double to_number(const string& str) {
  double v = 0;
  sscanf(str.c_str(), "%lg", &v);
  return v;
}


/**
 * --user-code path-to-user-code
 * --testcase
 * --max-cpu-time 1.0
 * --max-real-time 1.5
 * --max-memory 64000000
 * --input /var/cache/bar/1.in
 * --output /var/cache/bar/1.out
 * --input /var/cache/bar/2.in
 * --output /var/cache/bar/2.out
 * --input /var/cache/bar/3.in
 * --output /var/cache/bar/3.out
 */
static Options parse_cli_options(int argc, const char *argv[]) {
  Options options;
  Testcase current_case;

  // default options
  {
    string home = getenv("HOME") ? getenv("HOME") : "/tmp";
    string etc_dir_candidates[] = { "/etc/ljudge", fs::join(home, ".config/ljudge"), "./etc/ljudge", "../etc/ljudge" };
    for (size_t i = 0; i < sizeof(etc_dir_candidates) / sizeof(etc_dir_candidates[0]); ++i) {
      if (fs::is_dir(etc_dir_candidates[i])) {
        options.etc_dir = etc_dir_candidates[i];
        break;
      }
    }
    options.cache_dir = fs::join(home, ".cache/ljudge");
    options.compiler_limit = { 5, 10, 1 << 29 /* 512M mem */, 1 << 27 /* 128M out */ };
    options.pretty_print = isatty(STDOUT_FILENO);
    options.skip_checker = false;
    options.keep_stdout = false;
    options.keep_stderr = false;
    options.direct_mode = false;
    current_case.checker_limit = { 5, 10, 1 << 30, 1 << 30 };
    current_case.runtime_limit = { 1, 3, 1 << 26 /* 64M mem */, 1 << 25 /* 32M output */ };
    debug_level = 0;
  }

#define REQUIRE_NARGV(n) if (i + n >= argc) { \
  fatal("Option '%s' requires %d argument%s.", option.c_str(), n, n > 1 ? "s" : ""); }
#define NEXT_STRING_ARG string(argv[++i])
#define NEXT_NUMBER_ARG (to_number(NEXT_STRING_ARG))
#define APPEND_TEST_CASE if (!current_case.input_path.empty()) { \
  options.cases.push_back(current_case); \
  current_case.input_path = current_case.output_path = "";}

  for (int i = 1; i < argc; ++i) {
    string option;
    if (strncmp("--", argv[i], 2) == 0) {
      option = argv[i] + 2;
    } else if (strncmp("-", argv[i], 1) == 0) {
      option = argv[i] + 1;
    } else {
      // check "direct mode"
      option = argv[i];
      if (options.user_code_path.empty() && i == argc - 1 && is_language_supported(options.etc_dir, option) && options.cases.size() <= 1 && !options.skip_checker && options.checker_code_path.empty()) {
        options.user_code_path = option;
        options.skip_checker = true;
        options.direct_mode = true;
        options.keep_stdout = true;
        options.keep_stderr = true;
        continue;
      } else {
        fatal("`%s` is not a valid option. Use `--help` for more information", argv[i]);
      }
    }

    if (option == "user-code" || option == "u") {
      REQUIRE_NARGV(1);
      options.user_code_path = NEXT_STRING_ARG;
    } else if (option == "checker-code" || option == "c") {
      REQUIRE_NARGV(1);
      options.checker_code_path = NEXT_STRING_ARG;
    } else if (option == "testcase") {
      APPEND_TEST_CASE;
    } else if (option == "env") {
      REQUIRE_NARGV(2);
      string name = NEXT_STRING_ARG;
      string value = NEXT_STRING_ARG;
      options.envs[name] = value;
    } else if (option == "input" || option == "i") {
      APPEND_TEST_CASE;
      REQUIRE_NARGV(1);
      current_case.input_path = NEXT_STRING_ARG;
    } else if (option == "output" || option == "o") {
      REQUIRE_NARGV(1);
      current_case.output_path = NEXT_STRING_ARG;
    /* [[[cog
      import cog
      opts = ['cpu_time', 'real_time', 'output', 'memory']
      prefixes = {'': 'current_case.runtime_limit',
                  'checker-': 'current_case.checker_limit',
                  'compiler-': 'options.compiler_limit'}
      for prefix, var_name in prefixes.items():
        for opt in opts:
          cog.out(
            '''
            } else if (option == "max-%s%s") {
              REQUIRE_NARGV(1);
              %s.%s = NEXT_NUMBER_ARG;
            ''' % (prefix,
                   opt.replace('_', '-'),
                   var_name,
                   opt),
            trimblanklines=True)
    ]]] */
    } else if (option == "max-cpu-time") {
      REQUIRE_NARGV(1);
      current_case.runtime_limit.cpu_time = NEXT_NUMBER_ARG;
    } else if (option == "max-real-time") {
      REQUIRE_NARGV(1);
      current_case.runtime_limit.real_time = NEXT_NUMBER_ARG;
    } else if (option == "max-output") {
      REQUIRE_NARGV(1);
      current_case.runtime_limit.output = NEXT_NUMBER_ARG;
    } else if (option == "max-memory") {
      REQUIRE_NARGV(1);
      current_case.runtime_limit.memory = NEXT_NUMBER_ARG;
    } else if (option == "max-compiler-cpu-time") {
      REQUIRE_NARGV(1);
      options.compiler_limit.cpu_time = NEXT_NUMBER_ARG;
    } else if (option == "max-compiler-real-time") {
      REQUIRE_NARGV(1);
      options.compiler_limit.real_time = NEXT_NUMBER_ARG;
    } else if (option == "max-compiler-output") {
      REQUIRE_NARGV(1);
      options.compiler_limit.output = NEXT_NUMBER_ARG;
    } else if (option == "max-compiler-memory") {
      REQUIRE_NARGV(1);
      options.compiler_limit.memory = NEXT_NUMBER_ARG;
    } else if (option == "max-checker-cpu-time") {
      REQUIRE_NARGV(1);
      current_case.checker_limit.cpu_time = NEXT_NUMBER_ARG;
    } else if (option == "max-checker-real-time") {
      REQUIRE_NARGV(1);
      current_case.checker_limit.real_time = NEXT_NUMBER_ARG;
    } else if (option == "max-checker-output") {
      REQUIRE_NARGV(1);
      current_case.checker_limit.output = NEXT_NUMBER_ARG;
    } else if (option == "max-checker-memory") {
      REQUIRE_NARGV(1);
      current_case.checker_limit.memory = NEXT_NUMBER_ARG;
    /* [[[end]]] */
    } else if (option == "etc-dir") {
      REQUIRE_NARGV(1);
      options.etc_dir = NEXT_STRING_ARG;
    } else if (option == "cache-dir") {
      REQUIRE_NARGV(1);
      options.cache_dir = NEXT_STRING_ARG;
    } else if (option == "help" || option == "h") {
      print_usage();
    } else if (option == "version" || option == "v") {
      print_version();
    } else if (option == "compiler-versions" || option == "cvs") {
      print_compiler_versions(options, true /* only_present */);
    } else if (option == "all-compiler-versions" || option == "acvs") {
      print_compiler_versions(options, false /* only_present */);
    } else if (option == "debug") {
      debug_level = 10;  // show info, warn, error
      options.keep_stdout = true;
      options.keep_stderr = true;
    } else if (option == "check") {
      do_check();
    } else if (option == "pretty-print" || option == "pp") {
      options.pretty_print = 1;
    } else if (option == "skip-checker") {
      options.skip_checker = true;
      options.keep_stdout = true;
    } else if (option == "keep-stdout") {
      options.keep_stdout = true;
    } else if (option == "keep-stderr") {
      options.keep_stderr = true;
    } else {
      fatal("'%s' is not a valid option", argv[i]);
    }
  }
  APPEND_TEST_CASE;

  // if the user has decided to skip checker and did not provide a testcase, add a dummy one
  if (options.cases.empty() && options.skip_checker) {
    current_case.input_path = isatty(STDIN_FILENO) ?
        (options.direct_mode ? "" : DEV_NULL)
      : fs::resolve(format("/proc/self/fd/%d", STDIN_FILENO));
    current_case.runtime_limit.real_time = 0;  // unlimited
    options.cases.push_back(current_case);
  }

#undef APPEND_TEST_CASE
#undef NEXT_NUMBER_ARG
#undef NEXT_STRING_ARG
#undef REQUIRE_NARGV

  log_debug("etc-dir = %s", options.etc_dir.c_str());
  log_debug("cache-dir = %s", options.cache_dir.c_str());
  log_debug("debug-level = %d", debug_level);

  return options;
}

static void check_path(std::vector<string>& errors, const string& path, bool is_dir, const string& name) {
  if (path.empty()) {
    errors.push_back(name + " is required");
    return;
  }

  if (!(is_dir ? \
          (fs::is_dir(path) && fs::is_accessible(path, R_OK | X_OK))
        : (!fs::is_dir(path) && fs::is_accessible(path, R_OK)))) {
    errors.push_back(name + " (" + path + ") is not accessible");
  }
}

static void check_options(const Options& options) {
  std::vector<string> errors;

  fs::mkdir_p(options.cache_dir);

  check_path(errors, options.etc_dir, true, "--etc-dir");
  check_path(errors, options.cache_dir, true, "--cache-dir");
  check_path(errors, options.user_code_path, false, "--user-code");

  for (int i = 0; i < (int)options.cases.size(); ++i) {
    if (!options.direct_mode || !options.cases[i].input_path.empty()) {
      check_path(errors, options.cases[i].input_path, false /* is_dir */, format("--input of testcases[%d]", i));
    }
    if (options.skip_checker) {
      if (!options.cases[i].output_path.empty()) errors.push_back("--output conflicts with --capture-user-output");
    } else {
      check_path(errors, options.cases[i].output_path, false, format("--output of testcases[%d]", i));
    }
  }

  if (options.cases.empty()) {
    errors.push_back("At lease one testcase is required");
  }

  if (options.skip_checker && !options.checker_code_path.empty()) {
    errors.push_back("--capture-user-output conflicts with --checker-code");
  }

  if (getuid() == 0) {
    errors.push_back("Running ljudge using root is forbidden");
  }

  if (errors.size() > 0) {
    for (int i = 0; i < (int)errors.size(); ++i) {
      fprintf(stderr, "%s\n", errors[i].c_str());
    }
    fprintf(stderr, "--help will show valid options\n");
    cleanup_exit(1);
  }
}

static void setfd(int dst, int src) {
  if (src == dst || src < 0) return;
  dup2(src, dst);
  close(src);
}


static LrunResult parse_lrun_output(const string& lrun_output) {
  LrunResult result;
  size_t pos = 0, start = 0;
  for (;;) {
    pos = lrun_output.find("\n", start);
    int len = (pos == string::npos ? string::npos : pos - start);
    string line = lrun_output.substr(start, len);
    size_t space_pos = line.find(' ');
    if (line.length() > 0 && space_pos > 0) {
      string key = line.substr(0, space_pos);
      string value = line.substr(9); // 9: lrun fixed field padding
      if (key == "MEMORY") {
        long long memory = 0;
        if (sscanf(value.c_str(), "%Ld", &memory) != 1) result.error = "cannot read MEMORY";
        else result.memory = memory;
      } else if (key == "CPUTIME") {
        double time = 0;
        if (sscanf(value.c_str(), "%lf", &time) != 1) result.error = "cannot read CPUTIME";
        else result.cpu_time = time;
      } else if (key == "REALTIME") {
        double time = 0;
        if (sscanf(value.c_str(), "%lf", &time) != 1) result.error = "cannot read REALTIME";
        else result.real_time = time;
      } else if (key == "SIGNALED") {
        if (value == "0") {
          result.signaled = false;
        } else if (value == "1") {
          result.signaled = true;
        } else result.error = "cannot read SIGNALED";
      } else if (key == "EXITCODE") {
        int code = 0;
        if (sscanf(value.c_str(), "%d", &code) != 1) result.error = "cannot read EXITCODE";
        else result.exit_code = code;
      } else if (key == "TERMSIG") {
        int code = 0;
        if (sscanf(value.c_str(), "%d", &code) != 1) result.error = "cannot read TERMSIG";
        else result.term_sig = code;
      } else if (key == "EXCEED") {
        if (value != "none") result.exceed = value;
      }
    }
    start = pos + 1;
    if (pos == string::npos) break;
  }
  return result;
}

#ifndef NDEBUG
static void prepare_crash_report_path() {
  setenv("SEGFAULT_OUTPUT_NAME", format("/tmp/segv.%s.log", get_random_hash(6)).c_str(), 1 /* overwrite */);
  setenv("SEGFAULT_USE_ALTSTACK", "1", 1 /* overwrite */);
}
#endif

static LrunResult lrun(
#ifdef NDEBUG
    const vector<string>& args, const string& stdin_path, const string& stdout_path, const string& stderr_path
#else
    vector<string> args, string stdin_path, string stdout_path, string stderr_path
#endif
    ) {
  LrunResult result;
  int pipe_fd[2];
  int ret = pipe(pipe_fd);
  if (ret != 0) fatal("can not create pipe to run lrun");

#ifndef NDEBUG
  prepare_crash_report_path();
  string debug_lrun_command = "lrun";
  for (__typeof(args.begin()) it = args.begin(); it != args.end(); ++it) {
      debug_lrun_command += " " + shell_escape(*it);
  }
  if (!stdin_path.empty()) debug_lrun_command += " <" + shell_escape(stdin_path);
  if (!stdout_path.empty()) debug_lrun_command += " >" + shell_escape(stdout_path);
  if (!stderr_path.empty()) {
    if (stderr_path == stdout_path)
      debug_lrun_command += " 2>&1";
    else
      debug_lrun_command += " 2>" + shell_escape(stderr_path);
  }
  log_debug("running: %s", debug_lrun_command.c_str());
  fflush(stderr);
  if (getenv("LJUDGE_DEBUG_REPLACE_LRUN_CMD_USING")) {
    string cmd = getenv("LJUDGE_DEBUG_REPLACE_LRUN_CMD_USING");
    if (cmd.length() <= 1) cmd = "bash";
    fprintf(stderr, "Debug lrun using %s [y/n]?", cmd.c_str());
    char c;
    int n = scanf(" %c", &c);
    if (n == 1 && c == 'y') {
      for (int i = 0; i < (int)args.size(); ++i) {
        if (args[i] == "--max-real-time") {
          log_debug("overwriting max-real-time");
          args[i + 1] = "9000000";
        } else if (args[i] == "--") {
          args[i] = "--debug";
          log_debug("overwriting cmd to %s", cmd.c_str());
          args.resize(i + 1);
          args.push_back("--");
          args.push_back(cmd);
          break;
        }
      }
      log_debug("overwriting stdin: %s, stdout: %s, stderr: %s", stdin_path.c_str(), stdout_path.c_str(), stderr_path.c_str());
      stdin_path = "";
      stdout_path = "";
      stderr_path = "";
    }
  }
  if (getenv("DEBUG") && stderr_path == DEV_NULL) stderr_path = "";
  if (getenv("LJUDGE_KEEP_LRUN_STDERR") && stderr_path == DEV_NULL) {
    stderr_path = format("/tmp/ljudge_lrun.%s.log", get_random_hash(6));
    log_debug("lrun stderr redirects to %s", stderr_path.c_str());
  }
#endif

  pid_t pid = fork();
  if (pid) {
    close(pipe_fd[1]);

    int status = 0;
    string lrun_output = "";

    while (true) {
      char ch;
      ssize_t read_size = read(pipe_fd[0], &ch, 1);
      if (read_size == 1) {
        lrun_output += ch;
        if (ch == '\n' && lrun_output.find("EXCEED  ") != string::npos) {
          // we receive enough content (EXCEED ... "\n" is the last line)
          // lrun's exiting may take 0.03+ seconds (mostly the kernel
          // cleaning up the pid and ipc namespace). do NOT wait for it.
          //
          // lrun ignores SIGPIPE so this won't hurt it.
          //
          // we will soon exit (after all testcases) so zombies will be
          // killed by init.
          //
          // this reduces 0.03 to 0.04s per lrun run. when running
          // examples/a-plus-b/run.sh, real time decreases from 14.27
          // to 13.29, about 7%.
          break;
        }
      } else {
        // EOF or error. get lrun exit status
        while (waitpid(pid, &status, 0) != pid) usleep(10000);
        break;
      }
    }
    close(pipe_fd[0]);

    log_debug("lrun output:\n%s", lrun_output.c_str());

    if (status && WIFSIGNALED(status)) {
      result.error = format("lrun was signaled (%d)", WTERMSIG(status));
    } else if (status && WEXITSTATUS(status) != 0) {
      result.error = format("lrun exited with non-zero (%d)", WEXITSTATUS(status));
    } else {
      result = parse_lrun_output(lrun_output);
    }
  } else {
    prctl(PR_SET_PDEATHSIG, SIGTERM);
    close(pipe_fd[0]);
    // pass lrun's fd (3) output
    static const int LRUN_FILENO = 3;
    setfd(LRUN_FILENO, pipe_fd[1]);
    // prepare fds
    if (!stdin_path.empty()) {
      fclose(stdin);
      int ret = open(stdin_path.c_str(), O_RDONLY);
      if (ret < 0) fatal("can not open %s for reading", stdin_path.c_str());
      setfd(STDIN_FILENO, ret);
    }
    if (!stderr_path.empty()) {
      fclose(stderr);
      int ret = open(stderr_path.c_str(), O_WRONLY | O_TRUNC | O_CREAT, 0600);
      if (ret < 0) fatal("can not open %s for writing", stderr_path.c_str());
      setfd(STDERR_FILENO, ret);
    }
    if (!stdout_path.empty()) {
      fclose(stdout);
      if (stderr_path == stdout_path) {
        dup2(STDERR_FILENO, STDOUT_FILENO);
      } else {
        int ret = open(stdout_path.c_str(), O_WRONLY | O_TRUNC | O_CREAT, 0600);
        if (ret < 0) fatal("can not open %s for writing", stdout_path.c_str());
        setfd(STDOUT_FILENO, ret);
      }
    }
    // prepare args
    const char **argv = (const char**)malloc(sizeof(char*) * (args.size() + 2));
    argv[0] = "lrun";
    for (int i = 0; i < (int)args.size(); ++i) {
      argv[i + 1] = args[i].c_str();
    }
    argv[args.size() + 1] = 0;
    execvp("lrun", (char * const *) argv);
    close(pipe_fd[1]);
    log_error("can not start lrun");
    // not using cleanup_exit here because it is the child
    exit(1);
  }

  return result;
}

static string get_code_work_dir(const string& cache_dir, const string& code_path, const string& subdir = SUBDIR_USER_CODE) {
  // assume code file doesn't change
  static map<string, string> cache;
  string key = code_path + "///" + cache_dir;
  if (cache.count(key)) return cache[key];
  // for checker, we'd like to get a consistent hash result from its content. but for user code, random hash is okay.
  string code_sha1 = (subdir == SUBDIR_CHECKER ? sha1(fs::read(code_path)) : get_random_hash());
  string dest = fs::join(cache_dir, format("%s/%s/%s", subdir, code_sha1.substr(0, 2), code_sha1.substr(2)));
  cache[key] = dest;
  return dest;
}

static string get_temp_file_path(const string& cache_dir, int len = 40) {
  string dest;
  do {
    string hash = get_random_hash(len);
    dest = fs::join(cache_dir, format("%s/%s/%s", SUBDIR_TEMP, hash.substr(0, 2), hash.substr(2)));
  } while (fs::exists(dest));
  if (fs::mkdir_p(fs::dirname(dest)) < 0) fatal("can not prepare directory for %s", dest.c_str());
  if (!fs::touch(dest)) fatal("can not prepare temp file %s", dest.c_str());
  register_cleanup_path(dest);
  return dest;
}

static map<string, string> get_mappings(const string& src_name, const string& exe_name, const string& dest) {
  map<string, string> mappings;
  mappings["$src"] = src_name;  // basename
  mappings["$exe"] = exe_name;  // basename
  mappings["$dir"] = dest;      // unsandboxed, workdir full path

  return mappings;
}

static CompileResult compile_code(const string& etc_dir, const string& cache_dir, const string& code_path, const Limit& limit, const string& subdir = SUBDIR_USER_CODE) {
  log_debug("compile_code: %s", code_path.c_str());

  CompileResult result;
  result.success = false;

  if (!is_language_supported(etc_dir, code_path)) {
    result.error = format("Compiling `%s` is not supported. No appropriate config found.", fs::basename(code_path));
    return result;
  }

  string dest = get_code_work_dir(cache_dir, code_path, subdir);

  result.work_dir = dest;

  if (fs::mkdir_p(dest) < 0) fatal("can not mkdir: %s", dest.c_str());

  do {
    fs::ScopedFileLock lock(dest);

    string src_name = get_src_name(etc_dir, code_path);
    string dest_code_path = fs::join(dest, src_name);
    if (!fs::exists(dest_code_path)) {
      log_debug("copying code from %s to %s", code_path.c_str(), dest_code_path.c_str());
      string code_content = fs::read(code_path);
      size_t n = fs::write(dest_code_path, code_content.c_str());
      if (n != code_content.length()) fatal("fail to copy code file to %s", dest_code_path.c_str());
    }

    std::list<string> compile_cmd = get_config_list(etc_dir, code_path, ENV_COMPILE EXT_CMD_LIST);
    if (compile_cmd.empty()) {
      result.success = true;
      log_debug("skip compilation because get_config_list() returns nothing");
      break;
    }

    string dest_compile_log_path = fs::join(dest, "compile.log");
    string exe_name = get_config_content(etc_dir, code_path, ENV_COMPILE EXT_EXE_NAME, DEFAULT_EXE_NAME);
    string dest_exe_path = fs::join(dest, exe_name);
    if (fs::exists(dest_exe_path)) {
      result.success = true;
      log_debug("skip compilation because binary exists: %s", dest_exe_path.c_str());
      result.log = fs::nread(dest_compile_log_path, TRUNC_LOG);
      break;
    }

    string chroot_path = prepare_chroot(etc_dir, code_path, ENV_COMPILE, fs::join(cache_dir, SUBDIR_CHROOT));

    LrunArgs lrun_args;
    lrun_args.append_default();
    lrun_args.append("--chroot", chroot_path);
    lrun_args.append("--bindfs", fs::join(chroot_path, "/tmp"), dest);
    lrun_args.append(limit);

    map<string, string> mappings = get_mappings(src_name, exe_name, dest);
    lrun_args.append(filter_user_lrun_args(escape_list(get_config_list(etc_dir, code_path, ENV_COMPILE EXT_LRUN_ARGS), mappings)));
    lrun_args.append(filter_user_lrun_args(escape_list(get_config_list(etc_dir, code_path, ENV_EXTRA EXT_LRUN_ARGS), mappings)));
    // Override (hide) files using user provided options
    lrun_args.append(get_override_lrun_args(etc_dir, cache_dir, code_path, ENV_COMPILE, chroot_path));
    lrun_args.append("--");
    lrun_args.append(escape_list(compile_cmd, mappings));

    LrunResult lrun_result = lrun(lrun_args, DEV_NULL, dest_compile_log_path, dest_compile_log_path);

    string log = string_chomp(fs::nread(dest_compile_log_path, TRUNC_LOG));

    // check internal error (mostly lrun can not exec the compiler)
    if (!lrun_result.error.empty()) {
      result.error = lrun_result.error + "\n" + log;
      break;
    }

    // compiler did run. check its status and outputs
    result.log = log;
    if (!lrun_result.exceed.empty()) {
      result.log += format("%sCompiler exceeded %s limit", (log.empty() ? "" : "\n\n"), lrun_result.exceed);
    } else if (lrun_result.signaled) {
      result.log += format("%sCompiler was killed by signal %d\n\n", (log.empty() ? "" : "\n\n"), lrun_result.term_sig);
    } else if (lrun_result.exit_code != 0) {
      // the message is too common. only write to "log" if log is empty
      if (result.log.empty()) result.log = format("Compiler exited with code %d", lrun_result.exit_code);
    } else if (!fs::exists(dest_exe_path)) {
      if (result.log.empty()) result.log = "Compiler did not create the expected binary";
    } else {
      result.success = true;
    }
  } while (false);

  if (!result.success) {
#ifndef NDEBUG
    if (!getenv("DEBUG") && !getenv("NOCLEANUP")) {
#endif
      log_debug("cleaning: rm -rf %s", dest.c_str());
      fs::rm_rf(dest);
#ifndef NDEBUG
    }
#endif
  }
  return result;
}

static LrunResult run_code(
    const string& etc_dir,
    const string& cache_dir,
    const string& code_path,
    const Limit& limit,
    const string& stdin_path,
    const string& stdout_path,
    const string& stderr_path = DEV_NULL,
    const vector<string>& extra_lrun_args = vector<string>(),
    const string& env = ENV_RUN,
    const vector<string>& extra_argv = vector<string>()
) {
  log_debug("run_code: %s", code_path.c_str());

  string chroot_path = prepare_chroot(etc_dir, code_path, env, fs::join(cache_dir, SUBDIR_CHROOT));
  string exe_name = get_config_content(etc_dir, code_path, ENV_COMPILE EXT_EXE_NAME, DEFAULT_EXE_NAME);
  string dest = get_code_work_dir(cache_dir, code_path);

  // assume it is precompiled
  {
    // not locking here because it may slow down checker
    // fs::ScopedFileLock lock(dest);
    std::list<string> run_cmd = get_config_list(etc_dir, code_path, ENV_RUN EXT_CMD_LIST);
    if (run_cmd.empty()) {
      // use exe name as fallback
      run_cmd.push_back("./" + exe_name);
    }

    string src_name = get_src_name(etc_dir, code_path);
    map<string, string> mappings = get_mappings(src_name, exe_name, dest);
    mappings["$chroot"] = chroot_path;

    LrunArgs lrun_args;
    lrun_args.append_default();
    lrun_args.append("--chroot", chroot_path);
    lrun_args.append("--bindfs-ro", fs::join(chroot_path, "/tmp"), dest);
    lrun_args.append(get_override_lrun_args(etc_dir, cache_dir, code_path, ENV_COMPILE, chroot_path));
    lrun_args.append(limit);
    lrun_args.append(escape_list(extra_lrun_args, mappings));
    lrun_args.append(filter_user_lrun_args(escape_list(get_config_list(etc_dir, code_path, format("%s%s", env, EXT_LRUN_ARGS)), mappings)));
    lrun_args.append(filter_user_lrun_args(escape_list(get_config_list(etc_dir, code_path, ENV_EXTRA EXT_LRUN_ARGS), mappings)));
    lrun_args.append("--");
    lrun_args.append(escape_list(run_cmd, mappings));
    lrun_args.append(escape_list(extra_argv, mappings));

    LrunResult run_result = lrun(lrun_args, stdin_path, stdout_path, stderr_path);

    return run_result;
  }
}

static void write_compile_result(j::object& jo, const CompileResult& compile_result, const string& key) {
  j::object jco;
  jco["log"] = j::value(compile_result.log);
  if (!compile_result.error.empty()) jco["error"] = j::value(compile_result.error);
  jco["success"] = j::value(compile_result.success);
  jo[key] = j::value(jco);
}

static string remove_space(const string& str) {
  string result;
  result.reserve(str.length());
  for (size_t i = 0, l = str.length(); i < l; ++i) {
    if (isspace(str[i])) continue;
    result += str[i];
  }
  return result;
}

static void run_standard_checker(j::object& result, const Testcase& testcase, const string& user_output_path) {
  log_debug("run_standard_checker: %s %s", testcase.output_path.c_str(), user_output_path.c_str());
  string out = string_chomp(fs::read(testcase.output_path));
  string usr = string_chomp(fs::read(user_output_path));

  if (usr == out) {
    result["result"] = j::value(TestcaseResult::ACCEPTED);
    return;
  } else if (remove_space(usr) == remove_space(out)) {
    result["result"] = j::value(TestcaseResult::PRESENTATION_ERROR);
  } else {
    result["result"] = j::value(TestcaseResult::WRONG_ANSWER);
  }
}

static string get_full_path(const string& path) {
  if (fs::is_absolute(path)) return path;
  return fs::join(get_current_dir_name(), path);
}


static void run_custom_checker(j::object& result, const string& etc_dir, const string& cache_dir, const string& code_path, const string& checker_code_path, const map<string, string>& envs, const Testcase& testcase, const string& user_output_path) {
  log_debug("run_custom_checker: %s %s", testcase.output_path.c_str(), user_output_path.c_str());

  // prepare check environment
  // to be compatible with legacy checkers:
  // - a file named "output" is standard output
  // - a file named argv[1] is user output file path
  // - stdin is standard input

  // prepare a "tmp" directory which contains these files:
  // - input: standard input
  // - output: standard output
  // - user_output: user output
  // - user_code: user code
  string work_dir = get_code_work_dir(cache_dir, checker_code_path, SUBDIR_CHECKER);
  string output_path = get_temp_file_path(cache_dir);


  // lrun requires non-root users to use full path
  fs::touch(fs::join(work_dir, "input"));
  fs::touch(fs::join(work_dir, "output"));
  fs::touch(fs::join(work_dir, "user_output"));
  fs::touch(fs::join(work_dir, "user_code"));

  LrunArgs lrun_args;

  lrun_args.append("--bindfs-ro", "$chroot/tmp/input", get_full_path(testcase.input_path));
  lrun_args.append("--bindfs-ro", "$chroot/tmp/output", get_full_path(testcase.output_path));
  lrun_args.append("--bindfs-ro", "$chroot/tmp/user_output", get_full_path(user_output_path));
  lrun_args.append("--bindfs-ro", "$chroot/tmp/user_code", get_full_path(code_path));

  for (__typeof(envs.begin()) it = envs.begin(); it != envs.end(); ++it) {
      lrun_args.append("--env", it->first, it->second);
  }

  // run checker
  string checker_output;
  LrunResult lrun_result;
  {
    fs::ScopedFileLock lock(output_path);
    // the checker needs argv[1], which is "user_output"
    vector<string> checker_argv;
    checker_argv.push_back("user_output");
    lrun_result = run_code(etc_dir, cache_dir, checker_code_path, testcase.checker_limit, testcase.input_path, output_path, output_path /* stderr */, lrun_args, ENV_CHECK, checker_argv);
    checker_output = fs::nread(output_path, TRUNC_LOG);
  }

  string status = TestcaseResult::INTERNAL_ERROR;
  string error_message;
  static const int CHECKER_EXITCODE_ACCEPTED = 0;
  static const int CHECKER_EXITCODE_WRONG_ANSWER = 1;
  static const int CHECKER_EXITCODE_PRESENTATION_ERROR = 2;

  if (!lrun_result.error.empty()) {
    error_message = "lrun internal error: " + lrun_result.error;
  } else if (!lrun_result.exceed.empty()) {
    error_message = "checker exceeded " + lrun_result.exceed + " limit";
  } else if (lrun_result.signaled) {
    error_message = format("checker was killed by signal %d", lrun_result.term_sig);
  } else if (lrun_result.exit_code == CHECKER_EXITCODE_ACCEPTED) {
    status = TestcaseResult::ACCEPTED;
  } else if (lrun_result.exit_code == CHECKER_EXITCODE_WRONG_ANSWER) {
    status = TestcaseResult::WRONG_ANSWER;
  } else if (lrun_result.exit_code == CHECKER_EXITCODE_PRESENTATION_ERROR) {
    status = TestcaseResult::PRESENTATION_ERROR;
  } else {
    error_message = format("unknown checker exit code %d", lrun_result.exit_code);
  }

  if (!checker_output.empty()) result["checkerOutput"] = j::value(checker_output);

  if (!error_message.empty()) result["error"] = j::value(error_message);
  result["result"] = j::value(status);
}

static j::object run_testcase(const string& etc_dir, const string& cache_dir, const string& code_path, const string& checker_code_path, const map<string, string>& envs, const Testcase& testcase, bool skip_checker = false, bool keep_stdout = false, bool keep_stderr = false) {
  log_debug("run_testcase: %s", testcase.input_path.c_str());

  // assume user code and checker code are pre-compiled
  j::object result;

  // prepare output file path
  string stdout_path = get_temp_file_path(cache_dir);
  string stderr_path = keep_stderr ? get_temp_file_path(cache_dir) : DEV_NULL;
  LrunResult run_result;
  do {
    fs::ScopedFileLock lock(stdout_path);
    run_result = run_code(etc_dir, cache_dir, code_path, testcase.runtime_limit, testcase.input_path, stdout_path, stderr_path, vector<string>() /* extra_lrun_args */, ENV_RUN /* env */);

    // write stdout, stderr
    if (keep_stdout) result["stdout"] = j::value(fs::nread(stdout_path, TRUNC_LOG));
    if (keep_stderr) result["stderr"] = j::value(fs::nread(stderr_path, TRUNC_LOG));

    // check lrun internal error
    if (!run_result.error.empty()) {
      result["result"] = j::value(TestcaseResult::INTERNAL_ERROR);
      result["error"] = j::value(run_result.error);
      break;
    }

    // check limits
    if (!run_result.exceed.empty()) {
      const string& exceed = run_result.exceed;
      if (exceed == "CPU_TIME" || exceed == "REAL_TIME") {
        result["result"] = j::value(TestcaseResult::TIME_LIMIT_EXCEEDED);
      } else if (exceed == "MEMORY") {
        result["result"] = j::value(TestcaseResult::MEMORY_LIMIT_EXCEEDED);
      } else if (exceed == "OUTPUT") {
        result["result"] = j::value(TestcaseResult::OUTPUT_LIMIT_EXCEEDED);
      }
      result["exceed"] = j::value(exceed);
      break;
    }

    // write memory, cpu_time
    result["time"] = j::value(run_result.cpu_time);
    result["memory"] = j::value((double)run_result.memory);

    // check signaled and exit code
    if (run_result.signaled) {
      // check known signals
      int termsig = run_result.term_sig;
      result["termsig"] = j::value((double)termsig);
      if (termsig == SIGFPE) {
        result["result"] = j::value(TestcaseResult::FLOAT_POINT_EXCEPTION);
      } else if (termsig == SIGSEGV) {
        result["result"] = j::value(TestcaseResult::SEGMENTATION_FAULT);
      } else {
        result["result"] = j::value(TestcaseResult::RUNTIME_ERROR);
      }
      break;
    } else if (run_result.exit_code != 0) {
      int exitcode = run_result.exit_code;
      result["exitcode"] = j::value((double)exitcode);
      result["result"] = j::value(TestcaseResult::NON_ZERO_EXIT_CODE);
      break;
    }

    if (skip_checker) {
      // just accept it
      result["result"] = j::value(TestcaseResult::ACCEPTED);
    } else {
      // run checker
      if (checker_code_path.empty()) {
        run_standard_checker(result, testcase, stdout_path);
      } else {
        run_custom_checker(result, etc_dir, cache_dir, code_path, checker_code_path, envs, testcase, stdout_path);
      }
    }
  } while (false);

  return result;
}

static j::value run_testcases(const Options& opts) {
  vector<j::value> results;
  results.resize(opts.cases.size());
  #pragma omp parallel for
  for (int i = 0; i < (int)opts.cases.size(); ++i) {
    j::object testcase_result = run_testcase(opts.etc_dir, opts.cache_dir, opts.user_code_path, opts.checker_code_path, opts.envs, opts.cases[i], opts.skip_checker, opts.keep_stdout, opts.keep_stderr);
    results[i] = j::value(testcase_result);
  }
  return j::value(results);
}

static void print_with_color(const string& content, int color, FILE *fp = stderr) {
  if (content.empty()) return;

  term::set(term::attr::RESET, color, fp);
  fprintf(fp, "%s", content.c_str());
  if (content[content.length() - 1] != '\n') fprintf(fp, "\n");
  term::set(term::attr::RESET, fp);
}

static void print_final_result(const Options& opts, const j::value& jv) {
  if (opts.direct_mode) {
    // not checking all "contains()" here because direct-mode is not that serious
    string compiler_log = jv.get("compilation").get("log").to_str();
    print_with_color(compiler_log, term::fg::YELLOW);

    if (jv.contains("testcases")) {
      const auto& test_result = jv.get("testcases").get(0);
      printf("%s", test_result.get("stdout").to_str().c_str());
      print_with_color(test_result.get("stderr").to_str(), term::fg::RED);
    }
  } else {
    printf("%s", jv.serialize(opts.pretty_print).c_str());
  }
}

int main(int argc, char const *argv[]) {
  if (argc == 1) print_usage();

  Options opts = parse_cli_options(argc, argv);
  check_options(opts);

  j::object jo;
  bool compiled = true;

  // time(0) is only accurate to seconds, which is not enough, add pid randomness
  srand((time(0) << sizeof(pid_t)) | getpid());

  { // precompile user code
    CompileResult compile_result = compile_code(opts.etc_dir, opts.cache_dir, opts.user_code_path, opts.compiler_limit);
    register_cleanup_path(compile_result.work_dir);
    write_compile_result(jo, compile_result, "compilation");
    if (!compile_result.success) compiled = false;
  }

  if (compiled && !opts.checker_code_path.empty()) { // precompile checker code
    CompileResult compile_result = compile_code(opts.etc_dir, opts.cache_dir, opts.checker_code_path, opts.compiler_limit, SUBDIR_CHECKER);
    write_compile_result(jo, compile_result, "checkerCompilation");
    if (!compile_result.success) compiled = false;
  }

  if (compiled) {
    j::value results = run_testcases(opts);
    jo["testcases"] = results;
  }

  print_final_result(opts, j::value(jo));
  cleanup_exit(0);
}
