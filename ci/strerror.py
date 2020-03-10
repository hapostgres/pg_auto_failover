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
    """
    The Match class represents a `cscope` match. We use the tool in its
    Line-Oriented interface where the outputs is described that way in the
    manual page:

       For each reference found, cscope outputs a line consisting  of  the
       file  name, function name, line number, and line text, separated by
       spaces, for example, main.c main 161 main(argc, argv)
    """
    def __init__(self, filename, fname, ln, code):
        self.filename = filename
        self.fname = fname
        self.ln = ln
        self.code = code

        self.completeCode()

    def completeCode(self):
        """
        Because cscope is intended to be used in visual context, it only
        outputs the current line where the match is found, not the whole C
        statement. We build the whole statement by adding previous lines
        from the match until we find a line that can't be part of the
        current statement.
        """
        with open(self.filename, "r") as source:
            lines = source.readlines()

            currentLineNumber = self.ln - 2
            currentLine = lines[currentLineNumber][:-1].strip()

            # if previous line ends with semicolon ";" it's not part of the
            # current statement, same with { or }, or end of comment: */
            while not re.search("([;{}]$)|\*/$", currentLine):
                if currentLine != "":
                    self.code = currentLine + " " + self.code

                currentLineNumber = currentLineNumber - 1
                currentLine = lines[currentLineNumber][:-1].strip()

    def __str__(self):
        """
        String representation of a Match object.
        """
        return "%s %s %s" % (self.filename, self.ln, self.code)


class CScope():
    """
    The CScope class represents a `cscope` Line-Oriented interface session.
    An instance is created for each session, where we currently support only
    a single search, using -L.
    """
    def __init__(self, cscope=CSCOPE):
        pass

    def CallSites(self, fname):
        """returns a list of Match instances, one per call-site of fname"""
        return self.run(CSCOPE_MODE_CALL_SITE, fname)

    def run(self, mode, pattern):
        """
        Runs the `cscope -R -L -3 strerror` command line and parses its output.
        """
        self.matchList = []

        p = subprocess.run([CSCOPE, "-R", "-L", "-%d" % mode, pattern],
                           text=True,
                           capture_output=True)

        # For each reference found, cscope outputs a line consisting of the
        # file name, function name, line number, and line text, separated by
        # spaces, for example, main.c main 161 main(argc, argv)
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

    #
    # We accept calls to strerror() only when they are part of a message
    # output function call, such as log_warn("%s", strerror) or fprintf.
    #
    for match in cscope.CallSites("strerror"):
        if not re.search("^(%s)[(]" % "|".join(GRANTED_FUNCALLS), match.code):
            # this also makes the script exit with non-zero code
            raise Exception("Found an banned call to strerror at %s" % match)

        else:
            print("✓ %s" % match)
