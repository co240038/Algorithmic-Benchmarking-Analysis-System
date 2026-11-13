# Drop each member's file into sorts/ (e.g. sorts/bubble_sort.cpp) and run `make`.
# Only the sorts whose files exist are registered in the engine.
CXX      = g++
CXXFLAGS = -O2 -std=c++17 -Wall -Wextra
ifeq ($(OS),Windows_NT)
LDFLAGS += -static
endif
SORT_SRCS := $(wildcard sorts/*_sort.cpp)
SORT_DEFS := $(foreach s,$(SORT_SRCS),-DHAVE_$(shell basename $(s) _sort.cpp | tr a-z A-Z))

all: benchmark data_gen_tests benchmark_tests

benchmark: benchmark.cpp searches.cpp $(SORT_SRCS) metrics.h data_gen.h searches.h sorts.h
	$(CXX) $(CXXFLAGS) $(SORT_DEFS) benchmark.cpp searches.cpp $(SORT_SRCS) $(LDFLAGS) -o benchmark

.PHONY: all benchmark test clean

test: data_gen_tests benchmark_tests
	./data_gen_tests
	./benchmark_tests

data_gen_tests: data_gen_tests.cpp searches.cpp data_gen.h metrics.h searches.h
	$(CXX) $(CXXFLAGS) data_gen_tests.cpp searches.cpp $(LDFLAGS) -o data_gen_tests

benchmark_tests: benchmark_tests.cpp benchmark.cpp searches.cpp data_gen.h metrics.h searches.h sorts.h
	$(CXX) $(CXXFLAGS) benchmark_tests.cpp searches.cpp $(LDFLAGS) -o benchmark_tests

clean:
	rm -f benchmark benchmark.exe data_gen_tests data_gen_tests.exe benchmark_tests benchmark_tests.exe
