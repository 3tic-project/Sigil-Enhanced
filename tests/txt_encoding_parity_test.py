"""Compare the C++ TXT decoder to the frozen Python codec order."""

import random
import subprocess
import sys


def legacy_decode(data):
    for codec in ("utf-8", "gb18030", "utf-16"):
        try:
            return data.decode(codec)
        except UnicodeDecodeError:
            pass
    return ""


def main(executable):
    samples = [bytes((first,)) for first in range(256)]
    samples.extend(bytes((first, second)) for first in range(256) for second in range(256))
    randomizer = random.Random(20260925)
    samples.extend(
        bytes((randomizer.randrange(0x81, 0xFF), randomizer.randrange(0x30, 0x3A),
               randomizer.randrange(0x81, 0xFF), randomizer.randrange(0x30, 0x3A)))
        for _ in range(10000)
    )
    samples.extend(
        bytes(randomizer.getrandbits(8) for _ in range(randomizer.randrange(3, 13)))
        for _ in range(10000)
    )
    samples.extend(bytes.fromhex(value) for value in (
        "90308130", "9439fc36", "e3329a35", "e3329a36", "8431a437"
    ))
    input_hex = "".join(data.hex() + "\n" for data in samples)
    completed = subprocess.run(
        (executable, "--batch-probe"), input=input_hex, text=True,
        capture_output=True, check=True,
    )
    actual = completed.stdout.splitlines()
    if len(actual) != len(samples):
        raise AssertionError(f"expected {len(samples)} outputs, got {len(actual)}")

    failures = []
    for data, output in zip(samples, actual):
        expected = legacy_decode(data).encode("utf-8").hex()
        if output != expected:
            failures.append((data.hex(), expected, output))
    if failures:
        raise AssertionError(f"{len(failures)} mismatches; first ten: {failures[:10]}")
    print(f"matched {len(samples)} legacy decoder inputs")


if __name__ == "__main__":
    main(sys.argv[1])
