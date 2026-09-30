#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# vim:ts=4:sw=4:softtabstop=4:smarttab:expandtab

"""Release embedded-Python holds on a Sigil temporary directory.

Windows cannot delete a file that is still open or a directory that is the
process working directory. Sigil calls release_path() before retrying the
removal of a temporary directory that is still in use.
"""

import gc
import io
import os


def _normalized(path):
    return os.path.normcase(os.path.realpath(os.path.abspath(path)))


def _inside(path, root):
    return path == root or path.startswith(root + os.sep)


def release_path(root):
    root = _normalized(root)
    try:
        if _inside(_normalized(os.getcwd()), root):
            os.chdir(os.path.dirname(root))
    except OSError:
        pass
    # Unreachable cycles can keep file objects (for example PIL images) open.
    gc.collect()
    closed = 0
    for obj in gc.get_objects():
        if not isinstance(obj, io.IOBase):
            continue
        try:
            if obj.closed:
                continue
            name = obj.name
            if isinstance(name, bytes):
                name = os.fsdecode(name)
            if isinstance(name, str) and _inside(_normalized(name), root):
                obj.close()
                closed += 1
        except Exception:
            pass
    return closed
