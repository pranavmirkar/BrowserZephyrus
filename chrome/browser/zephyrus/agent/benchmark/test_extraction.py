"""The benchmark reads model replies exactly as the browser does.

Run from this directory, with zephyrus_policy_probe built:

    python -m unittest test_extraction -v

Checks every case of the kernel's own extraction corpus through the probe the
benchmark uses. The kernel's Rust tests check the same file, so the two sides
cannot read a reply differently. Without a probe these tests FAIL: a skipped
extraction test is how the old Python port drifted unnoticed.
"""

from __future__ import annotations

import json
import os
import pathlib
import tempfile
import time
import unittest

from bench import grading
from bench import kernel as kernel_probe
from bench.grading import Rung, grade
from run_benchmark import load_contract

CORPUS = kernel_probe.KERNEL_DIR / "testdata" / "extraction_corpus.json"


class ExtractionCorpusTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.kernel = kernel_probe.shared()
        grading.use_kernel(cls.kernel)
        cls.cases = json.loads(CORPUS.read_text(encoding="utf-8"))["cases"]

    def test_the_probe_reads_every_case_as_the_kernel_tests_expect(self):
        self.assertGreaterEqual(len(self.cases), 20)
        for case in self.cases:
            with self.subTest(reply=case["reply"][:60]):
                found = self.kernel.extract(case["reply"])
                got = None if found is None else {"tool": found[0], "arguments": found[1]}
                self.assertEqual(got, case["expect"], case["why"])

    def test_grading_sees_the_normalized_call(self):
        # The case the Python port got wrong in a recorded run: the browser
        # runs this as task.complete with an answer, so it is not a schema
        # failure.
        call = grading.extract_call('{"name": "task.complete", "arguments": "4.2.1"}')
        self.assertEqual(call.arguments, {"answer": "4.2.1"})

    def test_a_shape_the_kernel_cannot_fix_is_still_a_schema_failure(self):
        contract, _ = load_contract()
        fixture = {"observation": {"elements": [{"id": "e3"}], "tabs": []},
                   "accept": [{"name": "page.click"}]}
        result = grade('{"name":"page.click","arguments":null}', fixture, contract)
        self.assertEqual(result.rung, Rung.NAMED)


class StaleProbeTest(unittest.TestCase):
    def test_a_probe_older_than_the_kernel_is_refused(self):
        # A fresh copy of the real probe, dated before every kernel source.
        with tempfile.TemporaryDirectory() as folder:
            real = kernel_probe.shared().path
            copy = pathlib.Path(folder) / real.name
            copy.write_bytes(real.read_bytes())
            past = time.time() - 10 * 365 * 24 * 3600
            os.utime(copy, (past, past))
            self.assertTrue(kernel_probe.stale_sources(copy))
            with self.assertRaises(kernel_probe.KernelError):
                kernel_probe.Kernel(copy)

    def test_the_probe_in_use_is_current(self):
        self.assertEqual(kernel_probe.stale_sources(kernel_probe.shared().path), [])


if __name__ == "__main__":
    unittest.main()
