#!/usr/bin/env python3
"""Small runnable check for the relay's daily membership cache."""
from relay_server import MembershipAuthorizer, MEMBERSHIP_RECHECK_SECONDS


class StubAuthorizer(MembershipAuthorizer):
    def __init__(self, allowed):
        super().__init__("http://unused")
        self.allowed = allowed
        self.calls = 0

    def validate(self, token):
        self.calls += 1
        return token == self.allowed


def main():
    auth = StubAuthorizer("rrs_ok")
    endpoint = ("127.0.0.1", 1234)
    assert auth.authenticate(endpoint, "rrs_ok", 100)
    assert auth.calls == 1 and auth.allows(endpoint, 100 + MEMBERSHIP_RECHECK_SECONDS - 1)
    assert auth.authenticate(endpoint, "rrs_ok", 101) and auth.calls == 1
    assert not auth.authenticate(endpoint, "rrs_bad", 102)
    assert not auth.allows(endpoint, 102)
    print("relay membership cache: PASS")


if __name__ == "__main__":
    main()
