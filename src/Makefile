CXX?=g++
CXXFLAGS?=-Wall -Os -g -std=c++11
PREFIX?=/usr/local

ifneq ($(NDEBUG),)
CXXFLAGS += -DNDEBUG
endif

.SUFFIXES:

.PHONY: all clean install cog

all: ljudge

ljudge: ljudge.o sha1.o fs.o term.o
	$(CXX) -pthread -fopenmp -ldl -o $@ $^

%.o: %.cc
	$(CXX) -pthread -fopenmp -c -o $@ $(CXXFLAGS) $<

clean:
	-rm -f *.o **/*.o ljudge

install: ljudge
	install -D -m0755 -oroot -groot -s $< $(PREFIX)/bin/ljudge

cog: ljudge.cc
	cog.py -r $^
