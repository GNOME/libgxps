# Printer.py
#
# Copyright (C) 2012 Carlos Garcia Campos <carlosgc@gnome.org>
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

import sys
from Config import Config

class Printer:

    __single = None

    def __init__(self):
        if Printer.__single is not None:
            raise Printer.__single

        self._verbose = Config().verbose
        self._stream = sys.stdout
        self._rewrite = self._stream.isatty() and not self._verbose
        self._current_line = None

        Printer.__single = self

    def _erase_current_line(self):
        if not self._rewrite or self._current_line is None:
            return

        line_len = len(self._current_line)
        self._stream.write('\b' * line_len + ' ' * line_len + '\b' * line_len)
        self._current_line = None

    def _ensure_new_line(self, msg):
        if not msg.endswith('\n'):
            msg += '\n'
        return msg

    def _print(self, msg):
        self._stream.write(msg)
        self._stream.flush()

    def printout(self, msg):
        self._erase_current_line()
        self._print(msg)
        self._current_line = msg[msg.rfind('\n') + 1:]

    def printout_update(self, msg):
        if self._rewrite and self._current_line is not None:
            msg = self._current_line + msg
        elif not self._rewrite:
            msg = self._ensure_new_line(msg)
        self.printout(msg)

    def printout_ln(self, msg):
        if self._current_line is not None:
            self._current_line = None
            msg = '\n' + msg

        self._print(self._ensure_new_line(msg))

    def printerr(self, msg):
        self.stderr.write(self._ensure_new_line(msg))
        self.stderr.flush()

    def print_test_start(self, msg):
        self.printout(msg)

    def print_test_result(self, msg):
        self.printout_update(msg)

    def print_test_result_ln(self, msg):
        self.printout_update(self._ensure_new_line(msg))

    def print_default(self, msg):
        if self._verbose:
            self.printout_ln(msg)

def get_printer():
    try:
        instance = Printer()
    except Printer, i:
        instance = i

    return instance



