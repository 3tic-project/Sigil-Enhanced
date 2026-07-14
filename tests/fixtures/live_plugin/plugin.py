def run(plugin):
    if not plugin.ping():
        raise RuntimeError("host did not answer ping")
    return 0
