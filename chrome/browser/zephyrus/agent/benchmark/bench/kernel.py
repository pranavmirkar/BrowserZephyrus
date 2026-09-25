"""The shipped agent kernel, asked over a pipe.

The benchmark does not parse model replies and does not judge calls. The kernel
does both in the browser, so the benchmark asks the same kernel, through
`zephyrus_policy_probe` -- a small executable linking the exact Rust crate the
browser links, over the same cxx bridge.

It used to keep a Python port of the extractor instead, with a docstring saying
any change to the Rust file "belongs here in the same commit". That promise was
broken more than once. MEASURED on the day the port was deleted: 7 of 30
documented reply shapes, and 2 of 69 recorded replies, came out differently.
Every difference made the benchmark stricter than the browser. A bare
`"4.2.1"` that the kernel wraps into task.complete's answer and runs was scored
as a schema failure, and so were positional arguments and stray extra fields.
Those were failures invented by the harness and blamed on the model.

A probe that is older than the kernel sources is the same bug in a different
form, so it is refused (see `stale_sources`). Rebuild it with

    autoninja -C out/Release zephyrus_policy_probe
"""

from __future__ import annotations

import atexit
import json
import os
import pathlib
import subprocess
from typing import Any

HERE = pathlib.Path(__file__).resolve().parent
SRC = HERE.parents[5]
KERNEL_DIR = SRC / "chrome" / "services" / "zephyrus_agent" / "kernel"
CONTRACT = SRC / "chrome" / "browser" / "zephyrus" / "agent" / "schemas" / "tools.v1.json"
REBUILD = "autoninja -C out/Release zephyrus_policy_probe"


class KernelError(RuntimeError):
    """The kernel probe is missing, stale, or stopped answering."""


def find_probe(configured: str | None = None,
               name: str = "zephyrus_policy_probe",
               env: str = "ZEPHYRUS_POLICY_PROBE") -> pathlib.Path | None:
    """The probe to use: `configured`, else $`env`, else out/Release/`name`."""
    configured = configured or os.environ.get(env)
    if configured:
        path = pathlib.Path(configured)
        return path if path.is_file() else None
    for candidate in (f"{name}.exe", name):
        path = SRC / "out" / "Release" / candidate
        if path.is_file():
            return path
    return None


def kernel_inputs() -> list[pathlib.Path]:
    """Every source whose change makes a probe linking the kernel stale."""
    return [*sorted((KERNEL_DIR / "src").glob("*.rs")),
            KERNEL_DIR / "policy_probe.cc", CONTRACT]


def stale_sources(probe: pathlib.Path,
                  inputs: list[pathlib.Path] | None = None) -> list[str]:
    """Inputs modified after `probe` was built, newest first.

    Checked by timestamp, which is the one thing a stale binary cannot fake.
    The build system reporting success is not evidence here: a green test run
    against an old binary has already cost this project a false pass.
    """
    built = probe.stat().st_mtime
    inputs = kernel_inputs() if inputs is None else inputs
    newer = [(path.stat().st_mtime, path) for path in inputs
             if path.is_file() and path.stat().st_mtime > built]
    return [str(path.relative_to(SRC)) for _, path in sorted(newer, reverse=True)]


class Kernel:
    """One probe process, answering extraction and policy questions in turn."""

    def __init__(self, path: str | os.PathLike, allow_stale: bool = False) -> None:
        self.path = pathlib.Path(path)
        if not self.path.is_file():
            raise KernelError(f"no kernel probe at {self.path}; build it with: {REBUILD}")
        if not allow_stale:
            newer = stale_sources(self.path)
            if newer:
                raise KernelError(
                    f"the kernel probe at {self.path} is older than "
                    f"{', '.join(newer)}; rebuild it with: {REBUILD}"
                )
        self._process = subprocess.Popen(
            [str(self.path)],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            text=True,
            encoding="utf-8",
            bufsize=1,
        )

    def _ask(self, request: dict[str, Any]) -> dict[str, Any]:
        process = self._process
        if process.stdin is None or process.stdout is None:
            raise KernelError("the kernel probe was closed")
        try:
            process.stdin.write(json.dumps(request) + "\n")
            process.stdin.flush()
        except OSError as exc:
            raise KernelError(f"the kernel probe at {self.path} is gone: {exc}")
        line = process.stdout.readline()
        if not line:
            raise KernelError(f"the kernel probe at {self.path} stopped answering")
        return json.loads(line)

    def extract(self, response: str) -> tuple[str, Any] | None:
        """(tool, arguments) exactly as the browser would read `response`, or None.

        `arguments` is what policy would be given: normally an object, but a
        reply the kernel could not shape into one comes back as it is.
        """
        answer = self._ask({"op": "extract", "response": response})
        if not answer.get("found"):
            return None
        if "arguments" in answer:
            return answer["tool"], answer["arguments"]
        return answer["tool"], answer.get("arguments_unparsed")

    def decide(self, tool: str, arguments: Any, task: str, url: str,
               elements: list[dict[str, Any]]) -> dict[str, Any]:
        """The kernel's disposition for one proposed call."""
        return self._ask({
            "op": "decide",
            "tool": tool,
            "arguments": arguments,
            "task": task,
            "url": url,
            "elements": elements,
        })

    def close(self) -> None:
        if self._process.stdin:
            self._process.stdin.close()
            self._process.stdin = None
        try:
            self._process.wait(timeout=10)
        except subprocess.TimeoutExpired:
            self._process.kill()
            self._process.wait()
        if self._process.stdout:
            self._process.stdout.close()

    def __enter__(self) -> "Kernel":
        return self

    def __exit__(self, *exc: object) -> None:
        self.close()


_shared: Kernel | None = None


def shared() -> Kernel:
    """One probe for a whole test run, closed at exit.

    Raises KernelError when there is no current probe. Tests that read a reply
    FAIL without one rather than skip: a skipped extraction test is exactly
    how the old port's drift went unnoticed.
    """
    global _shared
    if _shared is None:
        path = find_probe()
        if path is None:
            raise KernelError(f"no zephyrus_policy_probe found; build it with: {REBUILD}")
        _shared = Kernel(path)
        atexit.register(_shared.close)
    return _shared
