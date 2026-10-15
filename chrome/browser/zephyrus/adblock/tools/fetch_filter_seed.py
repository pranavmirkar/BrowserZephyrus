# Copyright 2026 Pranav Mirkar. MIT licence (see the repository's LICENSE).
"""Downloads the bundled starter filter list: EasyList followed by EasyPrivacy.

The list is third-party data (GPLv3 or CC BY-SA 3.0, dual licensed), so it is NOT
kept in this repository. The build copies it from lists/zephyrus_filters.txt next
to the browser binary (the `filter_list` target), and the browser reads it as
data at startup, then keeps its own copy fresh with the in-browser updater.

Run this once before building:

    python chrome/browser/zephyrus/adblock/tools/fetch_filter_seed.py

Only HTTPS is accepted, each download is size-capped, and a list that does not
look like a filter list is refused rather than written.
"""
import argparse
import pathlib
import sys
import urllib.request

SOURCES = (
    "https://easylist.to/easylist/easylist.txt",
    "https://easylist.to/easylist/easyprivacy.txt",
)
MAX_BYTES = 12 * 1024 * 1024
DEFAULT_OUT = pathlib.Path(__file__).resolve().parent.parent / "lists" / "zephyrus_filters.txt"


def fetch(url: str) -> bytes:
    if not url.startswith("https://"):
        raise SystemExit(f"refusing a non-HTTPS source: {url}")
    # The list host refuses the default Python user agent.
    request = urllib.request.Request(
        url, headers={"User-Agent": "Zephyrus-build/1.0 (filter seed fetch)"})
    with urllib.request.urlopen(request, timeout=60) as response:
        data = response.read(MAX_BYTES + 1)
    if len(data) > MAX_BYTES:
        raise SystemExit(f"{url} is larger than {MAX_BYTES} bytes")
    if not data.lstrip().startswith(b"[Adblock Plus"):
        raise SystemExit(f"{url} does not look like a filter list")
    return data


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--out", type=pathlib.Path, default=DEFAULT_OUT)
    args = parser.parse_args()
    parts = [fetch(url) for url in SOURCES]
    # Each list keeps its own header: the browser tells the sections apart by it.
    combined = b"\n".join(part.replace(b"\r\n", b"\n").rstrip(b"\n") + b"\n"
                          for part in parts)
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_bytes(combined)
    print(f"wrote {args.out} ({len(combined)} bytes)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
