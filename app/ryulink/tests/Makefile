CXX := g++
CXXFLAGS := -std=c++17 -Wall -Wextra -Werror -O0 -g -I../source
TARGET := /tmp/ryulink_evidence_redaction_tests

.PHONY: test clean

test: $(TARGET)
	$(TARGET)

$(TARGET): evidence_redaction_tests.cpp ../source/evidence_redaction.c
	$(CXX) $(CXXFLAGS) -x c++ -o $@ $^

clean:
	rm -f $(TARGET)
