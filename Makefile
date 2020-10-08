GCOV ?= gcov
PROF ?= -fprofile-arcs -ftest-coverage -g -O0
CFLAGS ?= -W -Wall -pedantic -O3 $(PROF) $(CFLAGS_EXTRA) -std=c99
CXXFLAGS ?= -W -Wall -pedantic -O3 $(PROF) $(CFLAGS_EXTRA)

.PHONY: clean all

all: c c++

c: clean
	${RM} -rf *.gc*
	$(CC) unit_test.c -o unit_test $(CFLAGS) && ./unit_test
	$(CC) -m32 unit_test.c -o unit_test $(CFLAGS) && ./unit_test
	${GCOV} -a unit_test.c

c++: clean
	${RM} -rf *.gc*
	${CXX} frozen.c unit_test.c -o unit_test $(CXXFLAGS) && ./unit_test
	${GCOV} -a frozen.c unit_test.c

w:
	wine cl /DEBUG unit_test.c && wine unit_test.exe

clean:
	${RM} *.gc* *.dSYM unit_test unit_test.exe

