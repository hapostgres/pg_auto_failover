#! /usr/bin/env python3

#
# This script uses the `cscope` tool to find all the call sites for
# strerror() and checks that none of them are an assignement, which would be
# against the security policy.
#
# When an assignment is found, the script exists with a non-zero code.
#
# Note that at the moment our source code only uses strerror in log_...()
# calls, such as the followings:
#
# src/bin/pg_autoctl/keeper.c:1004 log_fatal("fsync error: %s", strerror(errno));
#
# In the worst case we had to read 2 lines behind the matching line, so our
# logic to complete the code is very very simple.
#

import re
import collections
import subprocess

CSCOPE = "/usr/local/bin/cscope"
CSCOPE_MODE_CALL_SITE = 3

#
# We accept to see calls to strerror() from within those funcalls, as in:
#
#   fprintf(stdout, "%s\n", strerror(errno));
#   log_fatal("fsync error: %s", strerror(errno));
#
GRANTED_FUNCALLS = ["log_fatal", "log_error", "log_warn", "log_info",
                    "log_debug", "log_trace", "fprintf"]

class Match():
    def __init__(self, filename, fname, ln, code):
        self.filename = filename
        self.fname = fname
        self.ln = ln
        self.code = code

        self.completeCode()

    def completeCode(self):
        with open(self.filename, "r") as source:
            lines = source.readlines()

            currentLineNumber = self.ln - 2
            currentLine = lines[currentLineNumber][:-1].strip()

            while not re.search("([;{}]$)|\*/$", currentLine):
                if currentLine != "":
                    self.code = currentLine + " " + self.code

                currentLineNumber = currentLineNumber - 1
                currentLine = lines[currentLineNumber][:-1].strip()

    def __str__(self):
        return "%s %s %s" % (self.filename, self.ln, self.code)


class CScope():
    def __init__(self, cscope=CSCOPE):
        pass

    def CallSites(self, fname):
        return self.run(CSCOPE_MODE_CALL_SITE, fname)

    def run(self, mode, pattern):
        self.matchList = []

        p = subprocess.run([CSCOPE, "-R", "-L", "-%d" % mode, pattern],
                           text=True,
                           capture_output=True)

        for line in p.stdout.splitlines():
            filename, func, line, *rest = line.split(' ')
            self.matchList.append(
                Match(
                    filename,
                    func,
                    int(line),
                    " ".join(rest).strip()))

        return self.matchList


if __name__ == '__main__':
    cscope = CScope()

    for match in cscope.CallSites("strerror"):
        if not re.search("^(%s)[(]" % "|".join(GRANTED_FUNCALLS), match.code):
            # this also makes the script exit with non-zero code
            raise Exception("Found an banned call to strerror at %s" % match)

        else:
            print("✓ %s" % match)
