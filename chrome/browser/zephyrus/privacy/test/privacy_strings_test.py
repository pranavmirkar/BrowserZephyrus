#!/usr/bin/env python3
# Copyright 2026 The Chromium Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
"""The spec 2.4 accuracy-contract enforcement test.

Every user-facing string in Privacy Intelligence must be traceable to something
the browser actually observed. This walks the source strings AND every shipped
translation and asserts the three rules from spec 2.4:

  1. Each string is bound to a TrackerStatus value.
  2. No string mentioning an entity uses a verb implying knowledge, possession
     or intent.
  3. Every string carries a translator note describing its status semantics.

**Why this is a script and not a C++ unit test.** Rule 2 has to hold "against
every shipped translation" (spec 2.4) -- a mistranslation that turns "detected"
into "tracked you" is the same defect as writing it in English. Translations
live in .xtb files and are compiled per-locale into separate .pak files, so a
C++ test running in one locale structurally cannot see them. Parsing the
resource XML can.

Run:  python3 chrome/browser/zephyrus/privacy/test/privacy_strings_test.py
Exit code 0 means the contract holds. Non-zero prints every violation.
"""

import io
import os
import re
import sys
import xml.etree.ElementTree as ET

# Every message this test governs. The prefix is the contract: if you add a
# user-facing Privacy Intelligence string, name it this way and it is covered
# automatically. Name it something else and you have opted out of the accuracy
# contract, which is what the coverage check at the bottom looks for.
PREFIX = 'IDS_ZEPHYRUS_PRIVACY_'

# Spec 2.1. There is no sixth value.
STATUSES = ('DETECTED', 'BLOCKED', 'ALLOWED', 'RANDOMIZED', 'POTENTIAL')

# Opt-out marker for strings that report no observation about any tracker --
# category labels, buttons, titles, score-breakdown row names. Spelled out in
# the translator note so it is greppable and has to be argued for in review,
# rather than hidden in an allowlist inside this file where it would be
# invisible to whoever adds the next string.
NO_STATUS_MARKER = 'No status:'


def names_a_status(desc):
  """True if `desc` names a TrackerStatus as a word of its own.

  Matched on identifier boundaries rather than as a plain substring, because
  EVENT type names contain status words: USER_ALLOWED_SITE has ALLOWED inside
  it, and a naive `in` check reported a note that says "No status: ... the
  USER_ALLOWED_SITE event" as if it claimed to report ALLOWED. The status a
  string reports and the event it describes are different things, and the
  check has to be able to tell them apart.
  """
  upper = desc.upper()
  return any(re.search(r'(?<![A-Z_])' + s + r'(?![A-Z_])', upper)
             for s in STATUSES)

# Spec 2.2 / 2.4 rule 2. Verbs implying the tracker KNOWS, POSSESSES or INTENDS
# something -- none of which the browser can observe. We observe requests.
#
# Per language, because "knows" in Hindi is not the string "knows". A locale
# with no list here is reported as unenforced rather than silently passing: an
# unchecked translation is exactly the hole this test exists to close.
BANNED_VERBS = {
    'en': [
        r'\bknows?\b', r'\bknew\b', r'\bsold\b', r'\bsells?\b',
        r'\btracked you\b', r'\btracking you\b', r'\bprofil(?:e[sd]?|ing)\b',
        r'\bcollect(?:s|ed|ing)?\b', r'\bhas your\b', r'\bhave your\b',
        r'\bharvest(?:s|ed|ing)?\b', r'\bstole\b', r'\bstolen\b',
        r'\bspy(?:s|ing)?\b', r'\bspied\b', r'\bwatch(?:es|ed|ing) you\b',
        r'\bshared (?:your|it) with\b', r'\bbuilt? a profile\b',
    ],
}

# Spec 2.2: a claim of total protection is banned outright, in any string,
# because spec 9 documents paths we do not cover.
BANNED_CLAIMS = {
    'en': [
        r'\b(?:completely|totally|fully|100%) (?:protected|private|anonymous)\b',
        r'\byou are (?:invisible|untrackable|anonymous)\b',
        r'\bno one can (?:see|track)\b',
    ],
}

SRC = os.path.normpath(
    os.path.join(os.path.dirname(os.path.abspath(__file__)), *([os.pardir] * 5)))
GRD = os.path.join(SRC, 'chrome', 'app', 'generated_resources.grd')
XTB_DIR = os.path.join(SRC, 'chrome', 'app', 'resources')


def message_id(msg):
  """The .xtb key for a <message>: grit's fingerprint of its source text.

  Lets this test check OUR translations specifically instead of scanning all of
  Chromium's, which would be slow and would flag upstream strings we do not own.
  Validated against a real shipped message by --selftest, so a grit change that
  moves the fingerprint scheme fails loudly instead of silently matching nothing
  and reporting a clean run.
  """
  sys.path.insert(0, os.path.join(SRC, 'tools', 'grit'))
  from grit.extern import FP  # noqa: E402  (path set up above)
  parts = [msg.text or '']
  for child in msg:
    # grit fingerprints the presentable content: a placeholder contributes its
    # NAME, not its example value.
    if child.tag == 'ph':
      parts.append(child.get('name', '').upper())
    parts.append(child.tail or '')
  return str(FP.UnsignedFingerPrint(''.join(parts).strip()))


def text_of(node):
  """Flattens a <message> body, dropping <ph> placeholders."""
  return ''.join(node.itertext())


def check_text(name, locale, text, errors):
  patterns = BANNED_VERBS.get(locale)
  if patterns is None:
    return False  # Caller reports the locale as unenforced.
  low = text.lower()
  for pattern in patterns + BANNED_CLAIMS.get(locale, []):
    match = re.search(pattern, low)
    if match:
      errors.append(
          '%s [%s]: banned phrasing %r in %r\n'
          '    Spec 2.2/2.3: describe what was OBSERVED (detected, blocked, '
          'allowed, randomized), never what a company knows, holds or intends.'
          % (name, locale, match.group(0), text.strip()[:90]))
  return True


def main():
  errors = []
  if not os.path.exists(GRD):
    print('cannot find %s' % GRD)
    return 2

  tree = ET.parse(GRD)
  messages = {}
  for msg in tree.iter('message'):
    name = msg.get('name') or ''
    if name.startswith(PREFIX):
      messages[name] = msg

  # --- Source strings -------------------------------------------------------
  for name, msg in sorted(messages.items()):
    body = text_of(msg)
    desc = (msg.get('desc') or '')

    # Rule 1: bound to a status. The binding is declared in the translator note
    # so that it survives into the .xtb and a translator can see the semantics
    # they must preserve -- which is also rule 3, so one check serves both.
    #
    # Not every string reports an observation. A category label, a button, a
    # panel title and a score-breakdown row make no claim about any tracker,
    # and forcing a status word into their notes would make the notes lie
    # rather than make the strings traceable.
    #
    # So the escape is explicit and greppable rather than a silent allowlist:
    # the author writes "No status:" followed by why. That keeps the burden on
    # whoever adds the string -- an unannotated string still fails -- while
    # leaving one obvious thing to audit. Anything that DOES report on a
    # tracker must still name its status; claiming "No status" for one of those
    # is the abuse this is watched for.
    declares_no_status = NO_STATUS_MARKER in desc
    if declares_no_status and names_a_status(desc):
      errors.append(
          '%s: desc says "%s" but also names a TrackerStatus.\n'
          '    Pick one: either the string reports an observation, or it does '
          'not.' % (name, NO_STATUS_MARKER))
    elif not declares_no_status and not names_a_status(desc):
      errors.append(
          '%s: desc names no TrackerStatus.\n'
          '    Spec 2.4 rules 1 and 3: state which of %s this string reports, '
          'so the claim is traceable and a translator cannot silently change '
          'its meaning. If it reports nothing about a tracker, write "%s" and '
          'the reason.' % (name, '/'.join(STATUSES), NO_STATUS_MARKER))

    # Rule 4 (added after this bit us): an ICU plural and $N substitution do
    # not compose. GetStringFUTF16 scans for a literal "$2"; an ICU plural
    # spells its count "#", so a message that is both DCHECKs in a debug build
    # and, where that DCHECK is compiled out, renders raw "{COUNT, plural,"
    # syntax to the user. A plural message may carry $1 -- the caller
    # substitutes it AFTER ICU runs -- but never a second numbered argument.
    if '{COUNT,' in body:
      extra = sorted(set(re.findall(r'\$([2-9])', body)))
      if extra:
        errors.append(
            '%s: is an ICU plural and also uses $%s.\n'
            '    These two substitution systems do not compose. Use "#" for '
            'the count, keep at most $1 for a name, and substitute that name '
            'after the formatter runs.' % (name, ', $'.join(extra)))

    check_text(name, 'en', body, errors)

  # --- Translations ---------------------------------------------------------
  # Only the translations of OUR messages. Scanning every .xtb wholesale would
  # take Chromium's ~200k strings and report violations in code we do not own.
  wanted = {message_id(m): n for n, m in messages.items()}
  unenforced = set()
  if wanted and os.path.isdir(XTB_DIR):
    for fname in sorted(os.listdir(XTB_DIR)):
      if not fname.endswith('.xtb'):
        continue
      locale = re.sub(r'^generated_resources_|\.xtb$', '', fname)
      try:
        xtb = ET.parse(os.path.join(XTB_DIR, fname))
      except ET.ParseError as e:
        errors.append('%s: unparseable (%s)' % (fname, e))
        continue
      for tr in xtb.iter('translation'):
        name = wanted.get(tr.get('id', ''))
        if not name:
          continue
        if not check_text(name, locale, text_of(tr), errors):
          unenforced.add(locale)

  print('spec 2.4 string test')
  print('  source strings checked: %d' % len(messages))
  if unenforced:
    # Not a failure today: Zephyrus ships English only. It becomes one the day
    # a locale ships, which is the point of saying it out loud every run.
    print('  locales with NO banned-verb list (unenforced): %s'
          % ', '.join(sorted(unenforced)))
  if errors:
    print('\n%d violation(s):\n' % len(errors))
    for e in errors:
      print('  - %s' % e)
    return 1
  print('  OK')
  return 0


# --- Self-test ---------------------------------------------------------------
# There are no Privacy Intelligence strings yet: this test is the guard that
# goes in BEFORE them. A guard that reports OK over an empty set has proven
# nothing, so --selftest exercises the checks against known-bad samples and
# fails if any of them slips through.

GOOD = [
    ('IDS_ZEPHYRUS_PRIVACY_DETECTED_N_SITES',
     'Meta tracking technology detected on 2 sites',
     'DETECTED: a Meta-owned domain was requested on 2 sites.'),
    ('IDS_ZEPHYRUS_PRIVACY_REQUEST_BLOCKED', 'Request blocked',
     'BLOCKED: the request was cancelled before leaving the device.'),
    ('IDS_ZEPHYRUS_PRIVACY_FP_RANDOMIZED',
     'Fingerprinting attempt — randomized',
     'RANDOMIZED: the API returned perturbed values.'),
]

BAD = [
    ('knows', 'Facebook knows you visited this site',
     'DETECTED: a Meta domain was requested.'),
    ('sold', 'Amazon sold your data', 'ALLOWED: the request completed.'),
    ('profile', 'Google is building a profile of you',
     'DETECTED: a Google domain was requested.'),
    ('collected', 'This site collected your location',
     'ALLOWED: the request completed.'),
    ('has your', 'This tracker has your email address',
     'DETECTED: a request was observed.'),
    ('total protection', 'You are completely protected on this site',
     'BLOCKED: every known tracker request was cancelled.'),
    ('no status in desc', 'Request blocked', 'Shown in the shield popup.'),
]


def selftest():
  failures = []
  for name, body, desc in GOOD:
    errs = []
    check_text(name, 'en', body, errs)
    if not any(s in desc.upper() for s in STATUSES):
      errs.append('%s: desc names no TrackerStatus' % name)
    if errs:
      failures.append('FALSE POSITIVE on a compliant string %r: %s'
                      % (body, errs))
  for label, body, desc in BAD:
    errs = []
    check_text(label, 'en', body, errs)
    if not any(s in desc.upper() for s in STATUSES):
      errs.append('desc names no TrackerStatus')
    if not errs:
      failures.append('MISSED a banned string (%s): %r' % (label, body))

  # Prove the .xtb fingerprint math still matches grit's, using a real shipped
  # message. Without this, a grit change would make `wanted` empty and every
  # translation would go unchecked while the test happily printed OK.
  tree = ET.parse(GRD)
  probe = None
  for msg in tree.iter('message'):
    if len(list(msg)) == 0 and msg.text and len(msg.text.strip()) > 25:
      probe = msg
      break
  if probe is None:
    failures.append('no placeholder-free message to validate fingerprints with')
  else:
    wanted_id = message_id(probe)
    found = False
    for fname in os.listdir(XTB_DIR):
      if not fname.startswith('generated_resources_') or not fname.endswith(
          '.xtb'):
        continue
      if ('id="%s"' % wanted_id) in io.open(
          os.path.join(XTB_DIR, fname), encoding='utf-8').read():
        found = True
        break
    if not found:
      failures.append(
          'fingerprint scheme no longer matches grit: %s (%r) resolves to id '
          '%s, which appears in no .xtb. Translations would go unchecked.'
          % (probe.get('name'), probe.text.strip()[:40], wanted_id))

  print('spec 2.4 string test --selftest')
  if failures:
    for f in failures:
      print('  - %s' % f)
    return 1
  print('  OK: %d compliant strings passed, %d banned strings caught, '
        'fingerprint scheme verified against a shipped message'
        % (len(GOOD), len(BAD)))
  return 0


if __name__ == '__main__':
  if '--selftest' in sys.argv:
    sys.exit(selftest())
  sys.exit(main())
