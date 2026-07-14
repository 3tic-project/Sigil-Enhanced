#!/usr/bin/env python3

import argparse
import importlib.util
import os
import sys
import traceback

from sigil_live import Plugin


def load_plugin(path):
    plugin_dir = os.path.dirname(path)
    if plugin_dir not in sys.path:
        sys.path.insert(0, plugin_dir)
    spec = importlib.util.spec_from_file_location("sigil_live_target", path)
    if spec is None or spec.loader is None:
        raise ImportError("could not load plugin module: %s" % path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def main(argv=None):
    parser = argparse.ArgumentParser()
    parser.add_argument("--plugin", required=True)
    parser.add_argument("--plugin-name", required=True)
    parser.add_argument("--compat-v1", action="store_true")
    parser.add_argument("--plugin-type", choices=("edit", "validation", "output", "input"))
    args = parser.parse_args(argv)

    socket_name = os.environ.get("SIGIL_PLUGIN_SOCKET", "")
    token = os.environ.get("SIGIL_PLUGIN_TOKEN", "")
    if not socket_name or not token:
        print("live launcher did not receive its socket credentials", file=sys.stderr)
        return 2

    plugin = None
    wrapper = None
    try:
        plugin = Plugin.connect(socket_name, token, args.plugin_name)
        module = load_plugin(os.path.abspath(args.plugin))
        run = getattr(module, "run", None)
        if not callable(run):
            raise TypeError("plugin.py must define run(plugin)")
        target = plugin
        if args.compat_v1:
            if args.plugin_type not in ("edit", "input", "output", "validation"):
                raise ValueError("unsupported live v1 plugin type")
            from sigil_live.compat import (
                CompatBookContainer,
                CompatInputContainer,
                CompatOutputContainer,
                CompatValidationContainer,
                LiveInputWrapper,
                LiveWrapper,
            )

            plugin_home = os.path.dirname(os.path.abspath(args.plugin))
            wrapper_class = LiveInputWrapper if args.plugin_type == "input" else LiveWrapper
            wrapper_options = {} if args.plugin_type == "input" else {
                "writable": args.plugin_type == "edit"
            }
            wrapper = wrapper_class(
                plugin, os.path.dirname(plugin_home), args.plugin_name, **wrapper_options
            )
            containers = {
                "edit": CompatBookContainer,
                "input": CompatInputContainer,
                "output": CompatOutputContainer,
                "validation": CompatValidationContainer,
            }
            target = containers[args.plugin_type](wrapper)
        result = run(target)
        status = "success" if result in (None, 0) else "failed"
        if wrapper is not None:
            if status == "success" and args.plugin_type == "edit":
                wrapper.commit()
            elif status != "success" and args.plugin_type == "edit":
                wrapper.rollback()
            if status == "success" and args.plugin_type == "validation":
                plugin.validation.publish_results(target.results)
        plugin.finish(status=status, message="" if result in (None, 0) else "Plugin returned %r" % result)
        return 0 if status == "success" else 1
    except BaseException as exc:
        traceback.print_exc()
        if wrapper is not None:
            try:
                wrapper.rollback()
            except BaseException:
                pass
        if plugin is not None:
            try:
                plugin.finish(status="failed", message=str(exc))
            except BaseException:
                pass
        return 1
    finally:
        if plugin is not None:
            plugin.close()


if __name__ == "__main__":
    sys.exit(main())
