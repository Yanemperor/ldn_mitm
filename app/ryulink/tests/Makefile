CXX := g++
CXXFLAGS := -std=c++17 -Wall -Wextra -Werror -O0 -g -I../source
TARGETS := /tmp/ryulink_evidence_redaction_tests /tmp/ryulink_virtual_ip_tests /tmp/ryulink_virtual_ip_p0_tests /tmp/ryulink_network_profile_tests

.PHONY: test clean

test: $(TARGETS)
	/tmp/ryulink_evidence_redaction_tests
	/tmp/ryulink_virtual_ip_tests
	/tmp/ryulink_virtual_ip_p0_tests
	/tmp/ryulink_network_profile_tests

/tmp/ryulink_evidence_redaction_tests: evidence_redaction_tests.cpp ../source/evidence_redaction.c
	$(CXX) $(CXXFLAGS) -x c++ -o $@ $^

/tmp/ryulink_virtual_ip_tests: virtual_ip_tests.cpp ../source/virtual_ip.c
	$(CXX) $(CXXFLAGS) -x c++ -o $@ $^

/tmp/ryulink_virtual_ip_p0_tests: virtual_ip_p0_tests.cpp ../source/virtual_ip.c ../source/ldn_mitm_ipc.c ../source/auth.c
	$(CXX) $(CXXFLAGS) -Iinclude -DRYULINK_APP_VERSION=\"test\" -ffunction-sections -Wno-error=unused-variable -Wno-error=sometimes-uninitialized -Wl,-dead_strip -o $@ $<

/tmp/ryulink_network_profile_tests: network_profile_tests.cpp ../source/network_profile.c
	$(CXX) $(CXXFLAGS) -Iinclude -x c++ -o $@ $^

clean:
	rm -f $(TARGETS)
