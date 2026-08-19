#!/usr/bin/env python3
# Copyright 2026 The Chromium Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Converts an entity-attribution dataset into the Zephyrus entity artifact.

One converter per source dataset; the artifact format
(../entity_artifact.h) is the only thing the browser knows about. See
chrome/browser/zephyrus/THIRD_PARTY_DATA.md before adding a dataset — every
mainstream option is CC BY-NC-SA 4.0 and the obligations are real.

Usage:
  python3 build_entity_artifact.py --dataset tracker-radar \\
      --input  /path/to/tracker-radar/domains/US \\
      --output /path/to/zephyrus_entities.dat

The output is a standalone data file, mmap'd at runtime. It is deliberately
NOT compiled into the binary: a build-time table is an adaptation of the source
data, and ShareAlike obligations are much easier to reason about when the
adaptation is a separate, clearly-labelled file.

NOT run by the build. Run it by hand when refreshing the dataset, then ship the
output through the component updater.
"""

import argparse
import json
import os
import struct
import sys
import time

MAGIC = b"ZEPHENT2"
FORMAT_VERSION = 2
HEADER_SIZE = 160
SIGNATURE_OFFSET = 76   # byte offset of the 64-byte signature field
SIGNATURE_BYTES = 64

# Must match zephyrus_privacy::Category in ../privacy_event.h.
CATEGORY_UNKNOWN = 0
CATEGORY_ADVERTISING = 1
CATEGORY_ANALYTICS = 2
CATEGORY_SOCIAL = 3
CATEGORY_CONTENT = 4
CATEGORY_CDN = 5
CATEGORY_FINGERPRINTING = 6

# Must match zephyrus_privacy::DatasetId in ../entity_artifact.h.
DATASET_IDS = {
    "unknown": 0,
    "tracker-radar": 1,
    # Same dataset, different source file, so the same provenance id.
    "tracker-radar-domain-map": 1,
    "disconnect": 2,
    "ghostery": 3,
}

NO_ENTITY = 0xFFFF
U32 = 0xFFFFFFFF


def super_fast_hash(data: bytes) -> int:
    """Paul Hsieh's SuperFastHash, byte-identical to base::PersistentHash.

    Ported from base/third_party/superfasthash/superfasthash.c. This MUST agree
    with the C++ exactly: the browser hashes a request's host at runtime and
    looks the result up in this table, so a single differing bit makes every
    lookup miss silently — the feature would simply attribute nothing and look
    like a clean web.

    entity_resolver_hash_unittest.cc pins the agreement with shared vectors.
    Do not "tidy" this function.
    """
    length = len(data)
    if length <= 0:
        return 0

    def get16(off: int) -> int:
        return data[off] | (data[off + 1] << 8)

    def to_signed_char(byte: int) -> int:
        # The C code casts through `signed char`, so bytes >= 0x80 sign-extend
        # before widening to uint32. Python ints are unbounded, so this has to
        # be done explicitly or every non-ASCII byte diverges.
        return byte - 256 if byte >= 128 else byte

    hash_ = length & U32
    rem = length & 3
    offset = 0
    words = length >> 2

    for _ in range(words):
        hash_ = (hash_ + get16(offset)) & U32
        tmp = ((get16(offset + 2) << 11) ^ hash_) & U32
        hash_ = ((hash_ << 16) ^ tmp) & U32
        offset += 4
        hash_ = (hash_ + (hash_ >> 11)) & U32

    if rem == 3:
        hash_ = (hash_ + get16(offset)) & U32
        hash_ = (hash_ ^ (hash_ << 16)) & U32
        hash_ = (hash_ ^ ((to_signed_char(data[offset + 2]) << 18) & U32)) & U32
        hash_ = (hash_ + (hash_ >> 11)) & U32
    elif rem == 2:
        hash_ = (hash_ + get16(offset)) & U32
        hash_ = (hash_ ^ (hash_ << 11)) & U32
        hash_ = (hash_ + (hash_ >> 17)) & U32
    elif rem == 1:
        hash_ = (hash_ + (to_signed_char(data[offset]) & U32)) & U32
        hash_ = (hash_ ^ (hash_ << 10)) & U32
        hash_ = (hash_ + (hash_ >> 1)) & U32

    hash_ = (hash_ ^ (hash_ << 3)) & U32
    hash_ = (hash_ + (hash_ >> 5)) & U32
    hash_ = (hash_ ^ (hash_ << 4)) & U32
    hash_ = (hash_ + (hash_ >> 17)) & U32
    hash_ = (hash_ ^ (hash_ << 25)) & U32
    hash_ = (hash_ + (hash_ >> 6)) & U32
    return hash_


def canonical_host(domain: str):
    """The exact bytes the runtime will hash, or None if unusable.

    Spec 9.7. The runtime hashes GURL::host(), which is lowercased, has no
    trailing dot, and is **punycode ASCII** for internationalised names. This
    side reads arbitrary text out of a JSON file, so it has to reach the same
    form or the two never agree -- and disagreement is silent: every lookup
    just misses and the page looks clean.

    The previous version of this function claimed to do that and did not: it
    lowercased and stripped whitespace but never applied IDNA, so every
    non-ASCII domain in the dataset was keyed by its UTF-8 bytes while the
    browser looked it up by punycode.
    """
    host = domain.strip().lower()
    while host.endswith("."):
        host = host[:-1]
    if not host:
        return None

    if not host.isascii():
        try:
            import idna
        except ImportError:
            # Deliberately fatal. Python's stdlib "idna" codec implements IDNA
            # 2003, which encodes a handful of names -- German sharp s, Greek
            # final sigma, ZWJ sequences -- differently from the UTS-46 the URL
            # spec (and therefore GURL) uses. Falling back to it would rebuild
            # the exact silent mismatch this function exists to prevent, on
            # precisely the domains hardest to notice. Better to stop.
            sys.exit("this dataset contains non-ASCII domains and the 'idna' "
                     "package is not installed. Run: pip install idna. "
                     "Refusing to fall back to the stdlib IDNA-2003 codec: it "
                     "would key some names differently from the browser and "
                     "they would silently never attribute (spec 9.7).")
        try:
            # uts46=True applies the same case folding and normalisation the
            # URL spec expects; without it, visually identical names encode
            # differently.
            host = idna.encode(host, uts46=True).decode("ascii")
        except Exception:  # noqa: BLE001 - idna raises several distinct types
            # An unencodable name cannot be looked up by the runtime either.
            # Dropping it loses one attribution; keeping it under a key nothing
            # can produce would be dead weight that silently never matches.
            return None

    # A dataset row for an IP literal or a single-label name cannot be a
    # registrable domain. The runtime refuses to fold such hosts (see
    # HostCanHaveOwner), so an entry for one could never be hit.
    if "." not in host:
        return None
    if all(part.isdigit() for part in host.split(".")):
        return None
    return host


def hash_domain(domain: str) -> int:
    host = canonical_host(domain)
    return super_fast_hash((host or "").encode("ascii", "ignore"))


# Tracker Radar's own category vocabulary is large and inconsistent; collapse
# it to the set the UI actually renders. Anything unrecognised becomes UNKNOWN
# rather than being guessed at (spec 4.1).
TRACKER_RADAR_CATEGORIES = {
    "advertising": CATEGORY_ADVERTISING,
    "ad motivated tracking": CATEGORY_ADVERTISING,
    "ad fraud": CATEGORY_ADVERTISING,
    "analytics": CATEGORY_ANALYTICS,
    "audience measurement": CATEGORY_ANALYTICS,
    "third-party analytics marketing": CATEGORY_ANALYTICS,
    "social network": CATEGORY_SOCIAL,
    "social - share": CATEGORY_SOCIAL,
    "social - comment": CATEGORY_SOCIAL,
    "embedded content": CATEGORY_CONTENT,
    "cdn": CATEGORY_CDN,
    "online payment": CATEGORY_CONTENT,
    "fingerprinting": CATEGORY_FINGERPRINTING,
    "browser fingerprinting": CATEGORY_FINGERPRINTING,
}


def load_tracker_radar(input_dir):
    """Yields (domain, owner_name, category) from a Tracker Radar domains dir.

    Tracker Radar ships one JSON per domain under domains/<REGION>/.
    """
    if not os.path.isdir(input_dir):
        sys.exit("not a directory: %s" % input_dir)

    files = [f for f in os.listdir(input_dir) if f.endswith(".json")]
    if not files:
        sys.exit("no .json files in %s (expected tracker-radar/domains/<REGION>)"
                 % input_dir)

    for name in files:
        path = os.path.join(input_dir, name)
        try:
            with open(path, "r", encoding="utf-8") as handle:
                blob = json.load(handle)
        except (OSError, ValueError) as err:
            print("  skipping %s: %s" % (name, err), file=sys.stderr)
            continue

        domain = blob.get("domain")
        if not domain:
            continue
        owner = (blob.get("owner") or {}).get("displayName") or ""

        category = CATEGORY_UNKNOWN
        for raw in blob.get("categories") or []:
            mapped = TRACKER_RADAR_CATEGORIES.get(str(raw).strip().lower())
            if mapped:
                category = mapped
                break
        yield domain, owner.strip(), category


def load_tracker_radar_domain_map(input_path):
    """Yields (domain, owner_name, category) from Tracker Radar's domain_map.json.

    This is the CONSOLIDATED map -- one 10 MB file rather than the 38k
    per-domain files under domains/<REGION>/. That matters because the repo is
    ~12 GB: a build step cannot clone it, but it can fetch one pinned file.

    **Known limitation: this file carries owners but no categories.** Tracker
    Radar only records categories in the per-domain files, and there is no
    consolidated equivalent. Everything here therefore resolves with
    category = UNKNOWN. That is a supported state (spec 4.2: never guess), and
    it costs the "Advertising"/"Analytics" grouping while keeping the company
    attribution that is the whole reason for having a dataset. Recovering
    categories means a shallow clone of the full repo in CI, which is a
    build-infrastructure decision rather than a converter change.
    """
    if not os.path.isfile(input_path):
        sys.exit("not a file: %s (expected domain_map.json)" % input_path)
    with open(input_path, "r", encoding="utf-8") as handle:
        blob = json.load(handle)
    if not isinstance(blob, dict):
        sys.exit("domain_map.json should be an object keyed by domain")

    for domain, record in blob.items():
        if not isinstance(record, dict):
            continue
        # displayName is the human-facing form ("Begun"); entityName is the
        # legal one ('"Begun" JSC'). The UI shows a company to a person, so
        # prefer the display form and fall back rather than showing nothing.
        owner = (record.get("displayName")
                 or record.get("entityName") or "")
        yield domain, owner.strip(), CATEGORY_UNKNOWN


LOADERS = {
    "tracker-radar": load_tracker_radar,
    "tracker-radar-domain-map": load_tracker_radar_domain_map,
}


def build(records, dataset_id, meta):
    """Returns (artifact_bytes, stats). `meta` carries the attribution
    strings and the SOURCE publication date the staleness guard reads."""
    # Intern entity names. An empty owner means the dataset knows the domain
    # but not who runs it: keep the domain (the category may still be useful)
    # and record NO_ENTITY rather than inventing a name.
    entity_ids = {}
    entity_names = []

    by_hash = {}
    collisions = set()
    duplicates = 0
    dropped = 0

    skipped_unusable = 0
    for domain, owner, category in records:
        if not domain:
            continue
        host = canonical_host(domain)
        if host is None:
            # IP literal, single label, or a name that cannot be IDNA-encoded.
            # The runtime can never look any of these up (spec 9.7).
            skipped_unusable += 1
            continue
        domain = host
        digest = super_fast_hash(host.encode("ascii"))

        if owner:
            if owner not in entity_ids:
                entity_ids[owner] = len(entity_names)
                entity_names.append(owner)
            entity_id = entity_ids[owner]
            if entity_id >= NO_ENTITY:
                sys.exit("too many distinct entities for a uint16 id")
        else:
            entity_id = NO_ENTITY

        if digest in by_hash:
            existing_domain, existing_id, _ = by_hash[digest]
            if existing_domain == domain:
                duplicates += 1
                continue
            # A genuine 32-bit hash collision between two DIFFERENT domains.
            #
            # The runtime has only the hash, so it cannot tell them apart, and
            # attributing a request to the wrong company is an accuracy-contract
            # violation (spec 2) — strictly worse than saying nothing. Drop
            # every domain involved. With ~10k domains this fires for roughly
            # 1% of builds and costs a handful of entries.
            if digest not in collisions:
                collisions.add(digest)
                dropped += 1  # The one already stored, which is popped below.
            dropped += 1
            continue
        by_hash[digest] = (domain, entity_id, category)

    for digest in collisions:
        by_hash.pop(digest, None)

    entries = sorted(by_hash.items())

    # Entities whose every domain was dropped stay in the table as unreferenced
    # names. Harmless — a few wasted bytes in the string blob — and pruning
    # them would mean renumbering every entity_id, which is a lot of risk for
    # no gain.

    blob = bytearray()

    # Attribution metadata goes in the same string blob, ahead of the entity
    # names. Spec 4.2: About and the dashboard credit the dataset from the
    # artifact's own bytes, so the credit cannot drift from what shipped.
    def intern(text):
        encoded = text.encode("utf-8")
        offset = len(blob)
        blob.extend(encoded)
        return offset, len(encoded)

    version_off, version_len = intern(meta["version"])
    source_off, source_len = intern(meta["source"])
    licence_off, licence_len = intern(meta["licence"])

    records_out = bytearray()
    for name in entity_names:
        encoded = name.encode("utf-8")
        records_out += struct.pack("<II", len(blob), len(encoded))
        blob += encoded

    entries_out = bytearray()
    for digest, (_domain, entity_id, category) in entries:
        entries_out += struct.pack("<IHBB", digest, entity_id, category, 0)

    header = bytearray(HEADER_SIZE)
    struct.pack_into(
        "<8sIIIIIIQQIIIIIII", header, 0,
        MAGIC, FORMAT_VERSION, 0,
        len(entries), len(entity_names), len(blob),
        dataset_id, int(time.time()), meta["published"], dropped,
        version_off, version_len, source_off, source_len,
        licence_off, licence_len)
    # Signature field stays zero here; sign() fills it once the whole file
    # exists, because the signature covers the whole file.

    stats = {
        "published": meta["published"],
        "entries": len(entries),
        "entities": len(entity_names),
        "string_bytes": len(blob),
        "dropped_collisions": dropped,
        "duplicate_domains": duplicates,
        "skipped_unusable": skipped_unusable,
    }
    return bytes(header) + bytes(entries_out) + bytes(records_out) + bytes(blob), stats


def sign(artifact, key_path):
    """Signs the artifact in place. Ed25519 over the whole file with the
    signature field zeroed — the rule that leaves no unsigned bytes.

    Returns the signed bytes. Without a key the artifact is written UNSIGNED
    and the browser will reject it: verification runs on every load, including
    a bundled artifact, so that the path is exercised from day one rather than
    first exercised when remote delivery arrives (spec 4.4.2).
    """
    if not key_path:
        return artifact
    try:
        from cryptography.hazmat.primitives import serialization
    except ImportError:
        sys.exit("--signing-key needs the 'cryptography' package installed")
    with open(key_path, "rb") as handle:
        key = serialization.load_pem_private_key(handle.read(), password=None)
    body = bytearray(artifact)
    # Already zero, but be explicit: what is signed must be exactly what the
    # verifier reconstructs.
    body[SIGNATURE_OFFSET:SIGNATURE_OFFSET + SIGNATURE_BYTES] = bytes(SIGNATURE_BYTES)
    signature = key.sign(bytes(body))
    assert len(signature) == SIGNATURE_BYTES, "unexpected Ed25519 signature size"
    body[SIGNATURE_OFFSET:SIGNATURE_OFFSET + SIGNATURE_BYTES] = signature
    return bytes(body)


def verify(artifact, expected):
    """Re-reads the artifact the way the C++ validator will."""
    magic, version, flags, entry_count, entity_count, string_bytes = \
        struct.unpack_from("<8sIIIII", artifact, 0)
    assert magic == MAGIC, "bad magic"
    assert version == FORMAT_VERSION, "bad version"
    assert flags == 0, "flags must be zero"
    assert entry_count == expected["entries"], "entry count mismatch"
    assert entity_count == expected["entities"], "entity count mismatch"
    assert string_bytes == expected["string_bytes"], "string size mismatch"

    total = (HEADER_SIZE + entry_count * 8 + entity_count * 8 + string_bytes)
    assert total == len(artifact), "declared size %d != actual %d" % (
        total, len(artifact))

    # Strictly ascending is what makes the C++ binary search correct, and the
    # validator rejects a file that is not.
    previous = None
    for i in range(entry_count):
        digest, = struct.unpack_from("<I", artifact, HEADER_SIZE + i * 8)
        assert previous is None or digest > previous, \
            "entries not strictly ascending at %d" % i
        previous = digest



# --- Self-test (spec 9.7) -----------------------------------------------------
# The C++ half of this contract is privacy_host_canon_unittest.cc. Both sides
# assert the same table, because the failure mode when they drift is silent:
# every lookup misses and the browser reports a clean page.

CANON_CASES = [
    # (input, expected canonical host or None)
    ("example.com", "example.com"),
    ("Example.COM", "example.com"),
    ("  example.com  ", "example.com"),
    ("example.com.", "example.com"),
    ("example.com..", "example.com"),
    ("ads.example.co.uk", "ads.example.co.uk"),
    # IDN must reach punycode, which is what GURL::host() gives the runtime.
    ("münchen.de", "xn--mnchen-3ya.de"),
    ("MÜNCHEN.DE", "xn--mnchen-3ya.de"),
    ("xn--mnchen-3ya.de", "xn--mnchen-3ya.de"),
    ("例え.テスト", "xn--r8jz45g.xn--zckzah"),
    # No registrable domain: the runtime refuses to fold these, so an entry
    # would be dead weight that can never be hit.
    ("127.0.0.1", None),
    ("8.8.8.8", None),
    ("localhost", None),
    ("intranet", None),
    ("", None),
    (".", None),
]


def selftest():
    failures = []
    for raw, expected in CANON_CASES:
        actual = canonical_host(raw)
        if actual != expected:
            failures.append("canonical_host(%r) = %r, expected %r"
                            % (raw, actual, expected))

    # The property that actually matters: two spellings of one host must hash
    # to one key.
    pairs = [
        ("münchen.de", "xn--mnchen-3ya.de"),
        ("example.com.", "example.com"),
        ("Example.COM", "example.com"),
    ]
    for a, b in pairs:
        if hash_domain(a) != hash_domain(b):
            failures.append("hash_domain(%r) != hash_domain(%r); the runtime "
                            "would look up one and the artifact would hold the "
                            "other" % (a, b))

    # And the converse: distinct hosts must not collapse together.
    if hash_domain("example.com") == hash_domain("example.net"):
        failures.append("distinct hosts collapsed to one key")

    print("build_entity_artifact --selftest")
    if failures:
        for f in failures:
            print("  - %s" % f)
        return 1
    print("  OK: %d canonicalisation cases, %d equivalence pairs"
          % (len(CANON_CASES), len(pairs)))
    return 0


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--dataset", required=True, choices=sorted(LOADERS))
    parser.add_argument("--input", required=True,
                        help="tracker-radar: domains/<REGION> directory; "
                             "tracker-radar-domain-map: domain_map.json")
    parser.add_argument("--output", required=True)
    parser.add_argument("--signing-key",
                        help="PEM Ed25519 private key. Omit and the artifact "
                             "is unsigned, which the browser rejects.")
    parser.add_argument("--dataset-version", default="unknown",
                        help="upstream release/commit this was built from")
    parser.add_argument("--dataset-source", default="",
                        help="URL the dataset came from")
    parser.add_argument("--dataset-licence", default="",
                        help="exact licence string, e.g. CC BY-NC-SA 4.0")
    parser.add_argument("--dataset-published", type=int, default=0,
                        help="UNIX seconds the SOURCE dataset was published. "
                             "Staleness is measured from this, never from the "
                             "build time (spec 4.4.1).")
    args = parser.parse_args()

    if not args.dataset_published:
        # Refusing is the safe default: a zero date would read as 1970 and put
        # every build permanently into the >180-day 'stale' bucket, which is at
        # least honest, but silently guessing 'now' would be a lie.
        print("warning: --dataset-published not set; the browser will treat "
              "this artifact as stale", file=sys.stderr)

    records = list(LOADERS[args.dataset](args.input))
    if not records:
        sys.exit("no records loaded; refusing to write an empty artifact")

    meta = {
        "version": args.dataset_version,
        "source": args.dataset_source,
        "licence": args.dataset_licence,
        "published": args.dataset_published,
    }
    artifact, stats = build(records, DATASET_IDS[args.dataset], meta)
    verify(artifact, stats)
    artifact = sign(artifact, args.signing_key)

    with open(args.output, "wb") as handle:
        handle.write(artifact)

    print("wrote %s (%d bytes)" % (args.output, len(artifact)))
    print("  domains:            %d" % stats["entries"])
    print("  entities:           %d" % stats["entities"])
    print("  duplicate domains:  %d" % stats["duplicate_domains"])
    print("  skipped (no registrable domain / bad IDN): %d"
          % stats["skipped_unusable"])
    print("  dropped to hash collisions: %d" % stats["dropped_collisions"])
    if stats["dropped_collisions"]:
        print("  (colliding domains are dropped on purpose: the runtime has "
              "only the 32-bit hash and cannot tell them apart, and a wrong "
              "company is worse than no company)")


if __name__ == "__main__":
    if "--selftest" in sys.argv:
        sys.exit(selftest())
    main()
