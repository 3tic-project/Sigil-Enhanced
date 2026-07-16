"""Shared result tracking for Live API coverage plugins."""

from __future__ import annotations

import json
import traceback
from datetime import datetime, timezone


class CoverageRunner:
    def __init__(self, plugin, suite_name):
        self.plugin = plugin
        self.suite_name = suite_name
        self.results = []  # list of dicts
        self.token = datetime.now(timezone.utc).strftime("%Y%m%d%H%M%S")

    def check(self, name, fn, *args, **kwargs):
        """Run fn; record pass/fail. Returns (ok, value_or_none)."""
        try:
            value = fn(*args, **kwargs)
            self.results.append(
                {
                    "name": name,
                    "status": "pass",
                    "detail": _short(value),
                }
            )
            return True, value
        except Exception as exc:
            self.results.append(
                {
                    "name": name,
                    "status": "fail",
                    "detail": "{0}: {1}".format(type(exc).__name__, exc),
                    "traceback": traceback.format_exc(),
                }
            )
            return False, None

    def skip(self, name, reason):
        self.results.append({"name": name, "status": "skip", "detail": reason})

    def note(self, name, detail):
        self.results.append({"name": name, "status": "info", "detail": detail})

    def counts(self):
        counts = {"pass": 0, "fail": 0, "skip": 0, "info": 0}
        for item in self.results:
            counts[item["status"]] = counts.get(item["status"], 0) + 1
        return counts

    def failed(self):
        return self.counts()["fail"] > 0

    def as_dict(self):
        return {
            "suite": self.suite_name,
            "finished_utc": datetime.now(timezone.utc).isoformat(),
            "counts": self.counts(),
            "results": self.results,
        }

    def text_report(self):
        counts = self.counts()
        lines = [
            "Live API Coverage — {0}".format(self.suite_name),
            "Finished: {0}".format(datetime.now(timezone.utc).isoformat()),
            "pass={pass} fail={fail} skip={skip} info={info}".format(**counts),
            "",
        ]
        for item in self.results:
            lines.append(
                "[{0}] {1}: {2}".format(
                    item["status"].upper(), item["name"], item.get("detail", "")
                )
            )
        return "\n".join(lines) + "\n"

    def json_report(self):
        return json.dumps(self.as_dict(), indent=2, ensure_ascii=False) + "\n"


def _short(value, limit=120):
    if value is None:
        return "ok"
    if isinstance(value, (bool, int, float)):
        return repr(value)
    if isinstance(value, (bytes, bytearray)):
        return "bytes[{0}]".format(len(value))
    if isinstance(value, str):
        text = value.replace("\n", "\\n")
        return text if len(text) <= limit else text[: limit - 3] + "..."
    if isinstance(value, (list, tuple)):
        return "{0}[{1}]".format(type(value).__name__, len(value))
    if isinstance(value, dict):
        return "dict keys={0}".format(sorted(value.keys())[:12])
    return type(value).__name__


def try_resolve(book, book_path):
    from sigil_live.errors import ResourceNotFound

    try:
        return book.resolve_path(book_path)
    except ResourceNotFound:
        return None
