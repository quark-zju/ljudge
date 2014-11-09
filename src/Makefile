CXX?=g++
CXXFLAGS?=-Wall -Os -g

ifneq ($(NDEBUG),)
CXXFLAGS += -DNDEBUG
endif

ifeq ($(DESTDIR),)
PREFIX?=/usr/local
else
PREFIX?=/usr
endif

.SUFFIXES:

.PHONY: all clean install cog

all: ljudge

ljudge: ljudge.o sha1.o fs.o term.o
	$(CXX) -o $@ $(LDFLAGS) -fopenmp $^ -pthread -ldl

%.o: %.cc
	$(CXX) -pthread -std=c++11 -fopenmp -c -o $@ $(CXXFLAGS) $<

clean:
	-rm -f *.o **/*.o ljudge

install: ljudge
	install -D -m0755 -oroot -groot -s $< $(DESTDIR)$(PREFIX)/bin/ljudge

cog: ljudge.cc
	cog.py -r $^
