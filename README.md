ljudge
======

ljudge is a command line tool to compile, run, check its output and generate a JSON report. It is designed to be the backend tool for an online judge system.

Dependencies
------------
* [lrun](https://github.com/quark-zju/lrun), used for sandboxing the untrusted program

lrun provides amd64 .deb packages. You can install (and setup) them using:

```bash
wget https://github.com/quark-zju/lrun/releases/download/v1.1.3/lrun_1.1.3_amd64.deb
# for Debian 7, you may need to add wheezy-backports apt source first
sudo apt-get install libseccomp2
sudo dpkg -i lrun_1.1.3_amd64.deb

# following steps are required to pass ljudge --check
sudo gpasswd -a $USER lrun
```

Installation
------------
1. Typically, `make && sudo install`
2. Copy configuration files at `etc/ljudge` to `/etc/ljudge` or `~/.cache/ljudge`
3. Run `ljudge --check`. It will examine the environment and tell you how to fix problems it finds. Repeat this step until ljudge complains nothing
4. (Optionally) Install compilers (take Debian for example):

```bash
sudo apt-get install build-essential clisp fpc gawk gccgo gcj-jdk ghc git golang lua5.2 mono-mcs ocaml openjdk-7-jdk perl php5-cli python2.7 python3 racket rake ruby1.9.3 valac
# nodejs
sudo apt-get install rlwrap
wget https://deb.nodesource.com/node/pool/main/n/nodejs/nodejs_0.10.33-2nodesource1~wheezy1_amd64.deb
sudo dpkg -i nodejs_0.10.33-2nodesource1~wheezy1_amd64.deb
```

5. (Optionally) Run `ljudge --compiler-versions` to check installed compilers
6. (Optionally) Run tests to verify things actually work: `cd examples/a-plus-b; ./run.sh`

Example
-------

```bash
% echo 'a;b;main(){scanf("%d%d",&a,&b);printf("%d",a+b);exit(0);}' > a.c
% echo '1 2' > 1.in
% echo '3' > 1.out
% echo '111111111111111111111111 1' > 2.in
% echo '111111111111111111111112' > 2.out
% ljudge --max-cpu-time 1.0 --max-memory 32m --user-code a.c --testcase --input 1.in --output 1.out --testcase --input 2.in --output 2.out
{
  "compilation": {
    "log": "a.c:1:1: warning: data definition has no type or storage (...)",
    "success": true
  },
  "testcases": [
    {
      "memory": 1220608,
      "result": "ACCEPTED",
      "time": 0.002
    },
    {
      "memory": 1527808,
      "result": "WRONG_ANSWER",
      "time": 0.002
    }
  ]
}
```

FAQ
---
**Q: What are the available options?**

A: Run `ljudge --help`.

**Q: What is the schema of the output JSON?**

A: Run `ljudge --json-schema` or check `schema/response.json`. You can verify the output JSON with [validator tools](http://json-schema.org/implementations.html#validator-list).

**Q: Does ljudge take advantage of multiple cores?**

A: Yes. ljudge runs testcases in parallel, with thread number = cpu core number by default. You can control it with `--threads n`. For example, `--threads 1` makes ljudge to run testcases sequentially.

**Q: What is the "checker"?**

A: The checker is used to compare the output of the user program and the standard output. It will return one of ACCEPTED, WRONG\_ANSWER, PRESENTATION\_ERROR. The default checker works in these steps, given both outputs:

1. Ignores the ending `\n` of the last non-empty line of both output files.
2. If they are identical, return ACCEPTED.
3. Remove all blank characters from both outputs.
4. If they are identical now, return PRESENTATION\_ERROR.
5. Return WRONG\_ANSWER.

**Q: Why JAVA won't work**

A: You may use java 6. Java 6 requires `execve` syscall, which is disabled. Try to set default Java to 7. For Debian, run `update-alternatives --config java`. Alternative you can enable `execve` syscall.

**Q: What if I want to write a custom checker?**

A: Just write one and pass it to ljudge using `--checker-code`. Your checker's stdin is the standard input and it can open these files:

* `"input"`: the standard input
* `"output"`: the standard output
* `"user_output"` (or `argv[1]`): the output of the user program
* `"user_code"`: the source code provided using `--user-code`

The checker's stdout will be captured. It should return 0 for ACCEPTED, 1 for WRONG\_ANSWER and 2 for PRESENTATION\_ERROR.

Notes
-----
Tested in:

* Debian 7
* Ubuntu 14.04/16.04
* Arch Linux 2014.11

Some environment variables can change ljudge behavior (for debugging purpose). Therefore the environment variables must be trusted. You can also `export NDEBUG=1` before building ljudge to remove all debug related features.
