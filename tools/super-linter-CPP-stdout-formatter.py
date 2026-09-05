# Copyright (c) 2026 Marcin Zdun
# This code is licensed under MIT license (see LICENSE for details)

import re
import sys
from dataclasses import dataclass, field
from typing import Callable


def include_what_you_use_handler(path: str, _line: str, msg: str) -> bool:
    m = re.match(r"^Add (?P<header>.+) for", msg)
    if not m:
        return False
    if path not in modifications:
        modifications[path] = Modifications()
    modifications[path].add_header(m.group("header"))
    return True


def whitespace_ending_newline_handler(path: str, _line: str, _msg: str) -> bool:
    if path not in modifications:
        modifications[path] = Modifications()
    modifications[path].missing_newline()
    return True


def rule_handled(path: str, line: str, msg: str, rule: str) -> bool:
    if rule not in handlers:
        return False
    return handlers[rule](path, line, msg)


@dataclass
class Modifications:
    headers: set[str] = field(default_factory=set)
    ending_newlines: bool = field(default=False)

    def add_header(self, header: str):
        self.headers.add(header)

    def missing_newline(self):
        self.ending_newlines = True


modifications: dict[str, Modifications] = {}

handlers: dict[str, Callable[[str, str, str], bool]] = {
    "build/include_what_you_use": include_what_you_use_handler,
    "whitespace/ending_newline": whitespace_ending_newline_handler,
}

marker = "Stderr contents for CPP:"
dashes = "------"

in_cpp_stderr = False
analyze = False
for line in sys.stdin:
    if line.rstrip().endswith(marker):
        in_cpp_stderr = True
    elif in_cpp_stderr:
        if line.rstrip() == dashes:
            analyze = not analyze
            if not analyze:
                in_cpp_stderr = False
    if analyze:
        m = re.match(
            r"^/tmp/lint/(?P<path>[^:]+):(?P<line>\d+):\s+(?P<msg>.*)\s+\[(?P<rule>[^]]+)\] \[\d+\]$",
            line.rstrip(),
        )
        if m:
            path, line_no, msg, rule = m.groups()
            if rule_handled(path, line_no, msg, rule):
                continue
            print(f"{path}:{line_no}:  {msg} [{rule}]")
            continue
    print(line, end="")

for path, mods in modifications.items():
    print(f"-- {path}")
    for header in mods.headers:
        print(f"{header}")
    if mods.ending_newlines:
        print("<eof>")
    print()
