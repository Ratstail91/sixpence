#include directories
INCLUDES+=.

#libraries
LIBS+=

#flags
CXXFLAGS+=-std=c++17 $(addprefix -I,$(INCLUDES))

#source
CXXSRC=$(wildcard *.cpp)

#objects
OBJDIR=obj
OBJ+=$(CXXSRC:.cpp=.o)

#output
OUTDIR=.
OUT=$(addprefix $(OUTDIR)/,sixpence)

#targets
all: $(OBJ)
	$(CXX) $(CXXFLAGS) -o $(OUT) $(OBJ) $(LIBS)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c -o $@ $<

debug: export CXXFLAGS+=-g
debug: all

release: export CXXFLAGS+=-O3
release: clean all

rebuild: clean all

clean:
ifeq (commandline,${whichshell}) #I think I've screwed with my windows shell too much
	# not working
else
	find . -type f -name '*.o' -exec rm -f -r -v {} \;
	find . -type f -name '*.a' -exec rm -f -r -v {} \;
#	rm $(OUTDIR)/* -f -r -v
#	find . -empty -type d -delete
endif
