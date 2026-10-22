# Location of the ERIM repository (https://github.com/vahldiek/erim).
# Defaults to a clone next to this makefile; override with `make ERIM_DIR=/path/to/erim`.
ERIM_DIR ?= $(CURDIR)/erim
ERIM_REPO ?= https://github.com/vahldiek/erim.git

ERIM_SRC=$(ERIM_DIR)/src
ERIM_BIN=$(ERIM_DIR)/bin

SCENARIO ?= 1
EXEC_PREV ?= 0
BENCHMARK_SIZE ?= 4096

INCLUDES=-I. -I$(ERIM_SRC)/erim -I$(ERIM_SRC)/common
DEFINES=-DSCENARIO=$(SCENARIO) -DEXEC_PREV=$(EXEC_PREV) -DBENCHMARK_SIZE=$(BENCHMARK_SIZE)
CFLAGS+=-Wall -O2 -g -flto -fno-inline $(INCLUDES) $(DEFINES)
CXXFLAGS+=-Wall -O2 -g -flto -fno-inline $(INCLUDES) $(DEFINES)

LIBERIM=$(ERIM_BIN)/erim/liberim.a
LIBSWSCOMMON=$(ERIM_BIN)/common/libswscommon.a
LIBRARIES=$(LIBSWSCOMMON) $(LIBERIM)
LDLIBS=-lm -pthread $(LIBRARIES)
# erim_memScan marks pages with unexpected WRPKRUs non-executable, which can hit ld.so's
# lazy-binding resolver. Resolve all symbols at startup so it is never called afterwards.
LDFLAGS+=-Wl,-z,now

CACHEUTILS=libcache/cacheutils.h

TESTCASES=test_application_pht test_application_btb test_application_stl micro_bench test_application_ret2spec

all: $(TESTCASES)

# Clone ERIM if it is not present yet
$(ERIM_SRC)/erim/erim.h:
	git clone $(ERIM_REPO) $(ERIM_DIR)

$(LIBERIM): | $(ERIM_SRC)/erim/erim.h
	$(MAKE) -C $(ERIM_SRC)/erim

$(LIBSWSCOMMON): | $(ERIM_SRC)/erim/erim.h
	$(MAKE) -C $(ERIM_SRC)/common

$(CACHEUTILS): libcache/cache.c libcache/cache.h
	$(MAKE) -C libcache

# With EXEC_PREV=1 these PoCs mprotect the page of their page-aligned transmit function.
# Keep source order so the page-aligned offset_further_code follows it and no other code
# (e.g. switchT/switchU, access_array) ends up on that page, which would segfault.
test_application_pht test_application_stl test_application_ret2spec: CFLAGS += -fno-toplevel-reorder
test_application_btb: CXXFLAGS += -fno-toplevel-reorder

# -z now does not cover the shared libstdc++, which lazily resolves __cxa_finalize at exit.
test_application_btb: LDFLAGS += -static-libstdc++ -static-libgcc

%: %.c $(LIBRARIES) $(CACHEUTILS) spex.h
	$(CC) $(CFLAGS) $(LDFLAGS) $< -o $@ $(LDLIBS)

%: %.cpp $(LIBRARIES) $(CACHEUTILS) spex.h
	$(CXX) $(CXXFLAGS) $(LDFLAGS) $< -o $@ $(LDLIBS)

test: $(TESTCASES)
	for test in $(TESTCASES); do \
		./$$test; \
	done

clean:
	rm -f *.o $(TESTCASES)

# Also clean the ERIM build output
distclean: clean
	$(MAKE) -C $(ERIM_SRC)/erim clean
	$(MAKE) -C $(ERIM_SRC)/common clean

.PHONY: all test clean distclean
