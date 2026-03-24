CXX ?= clang++
CXXFLAGS = -std=c++17 -O2 -Wall -Wextra
TARGET = osc_compare

all: $(TARGET)

$(TARGET): osc_compare.cpp ../../Source/DSP/KR106Oscillators.h ../../Source/DSP/KR106OscillatorsWT.h
	$(CXX) $(CXXFLAGS) -o $@ osc_compare.cpp

clean:
	rm -f $(TARGET)

.PHONY: all clean
