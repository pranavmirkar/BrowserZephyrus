#!/usr/bin/env python3
"""Zephyrus browser-agent tool-call benchmark.

Answers one question before any kernel is built: given a real Observation and
the frozen tool contract, can a local model emit a tool call that is well
formed, schema valid, grounded in what it was actually shown, and sensible?

Deliberately offline. No browser, no Chromium build, no agent loop. If the
answer is no, none of those are worth building yet.

    python run_benchmark.py --provider ollama --model minicpm5:1b
    python run_benchmark.py --provider replay --model recordings/baseline.json

Exit codes:
    0  the gate passed
    1  the gate failed
    2  the harness could not run (bad fixture, unreachable model, bad schema)
"""

from __future__ import annotations

import argparse
import json
import pathlib
import sys
from collections import Counter
from dataclasses import asdict, dataclass
from typing import Any

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))

from bench import grading
from bench import kernel as kernel_probe  # noqa: E402
from bench import loop as loop_driver  # noqa: E402
from bench import providers  # noqa: E402
from bench.grading import Grade, Rung, grade  # noqa: E402
from bench.schema import SchemaError, assert_schema_supported  # noqa: E402

HERE = pathlib.Path(__file__).resolve().parent
CONTRACT_PATH = HERE.parent / "schemas" / "tools.v1.json"
FIXTURE_DIR = HERE / "fixtures"

# The model is told the rules once, in the system prompt, and the task and page
# go in the user turn. Keeping page content out of the system prompt is not
# cosmetic: it is the trusted/untrusted split from the PRD's threat model, and
# starting it here means the production prompt inherits it rather than being
# retrofitted.
# No prompt text lives here. Each fixture is shown the prompt the shipped
# TaskLoop opens a task with, fetched through zephyrus_loop_probe (see
# bench/loop.py). This file used to keep its own SYSTEM_PROMPT and
# USER_PROMPT, which had fallen behind production: no address rule, no stop
# rule, no step count, and a Python tool listing instead of the kernel's.
# Results recorded before `"loop"` appears in them were graded against that
# older prompt.


@dataclass
class Row:
    fixture: str
    rung: str
    detail: str
    latency_ms: int
    tool: str | None
    violation: str | None


def load_contract() -> tuple[dict[str, dict[str, Any]], str]:
    """Return (tools by name, the compact listing shown to the model)."""
    try:
        raw = json.loads(CONTRACT_PATH.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        raise SystemExit(f"cannot read tool contract {CONTRACT_PATH}: {exc}")

    by_name: dict[str, dict[str, Any]] = {}
    lines: list[str] = []
    for tool in raw["tools"]:
        assert_schema_supported(tool["parameters"], f"{tool['name']}.parameters")
        by_name[tool["name"]] = tool
        params = tool["parameters"].get("properties") or {}
        required = set(tool["parameters"].get("required") or [])
        # Compact one-line signature. Verbose JSON Schema in the prompt costs
        # tokens a 1B model does not have to spare and does not measurably help.
        sig = ", ".join(
            f"{name}{'' if name in required else '?'}: {spec.get('type', 'any')}"
            + (f" [{'|'.join(map(str, spec['enum']))}]" if "enum" in spec else "")
            for name, spec in params.items()
        )
        # No parentheses around the signature. With them, qwen2.5:1.5b emitted
        # {"name": "browser.back()"} -- copying the punctuation straight out of
        # the listing into the tool name. The prompt is under test too.
        args_note = f"  args: {sig}" if sig else "  args: none"
        lines.append(
            f"- {tool['name']} [{tool['risk']}] {tool['description']}\n{args_note}"
        )
    return by_name, "\n".join(lines)


def load_fixtures(only: str | None) -> list[dict[str, Any]]:
    if not FIXTURE_DIR.is_dir():
        raise SystemExit(f"no fixture directory at {FIXTURE_DIR}")
    fixtures = []
    for path in sorted(FIXTURE_DIR.glob("*.json")):
        try:
            data = json.loads(path.read_text(encoding="utf-8"))
        except json.JSONDecodeError as exc:
            raise SystemExit(f"fixture {path.name} is not valid JSON: {exc}")
        for key in ("id", "task", "observation", "accept"):
            if key not in data:
                raise SystemExit(f"fixture {path.name} is missing {key!r}")
        if not data["accept"]:
            raise SystemExit(f"fixture {path.name} accepts nothing, so it can never pass")
        if only and only not in data["id"]:
            continue
        fixtures.append(data)
    if not fixtures:
        raise SystemExit("no fixtures matched")
    return fixtures


def render_observation(observation: dict[str, Any]) -> str:
    """Render an Observation the way the browser will send it.

    Compact and stable. This format is itself under test: if the model grounds
    badly, the format is as likely to be the cause as the model.
    """
    # Match Observation::ToJson: quote every page-controlled field, including
    # titles and newlines. Delimiters alone are forgeable by page text. This
    # reduces instruction confusion; the executor's policy is still required.
    return json.dumps(observation, ensure_ascii=True, separators=(",", ":"))


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--provider", default="ollama",
                        choices=["ollama", "openai-compatible", "replay", "claude"])
    parser.add_argument("--model", required=True,
                        help="Model name, or path to recordings for --provider replay.")
    parser.add_argument("--base-url", default="http://127.0.0.1:11434")
    parser.add_argument("--timeout", type=float, default=120.0)
    parser.add_argument("--only", help="Run fixtures whose id contains this substring.")
    parser.add_argument("--gate", type=float, default=0.70,
                        help="Fraction that must reach GROUNDED for exit 0.")
    parser.add_argument("--json-out", type=pathlib.Path,
                        help="Write the full machine-readable result here.")
    parser.add_argument("--record", type=pathlib.Path,
                        help="Save raw responses for later replay.")
    parser.add_argument("--allow-remote", action="store_true",
                        help="Permit a non-loopback model host. Off by default so a "
                             "benchmark cannot quietly send page content to a hosted API.")
    parser.add_argument("--kernel", metavar="PATH",
                        help="zephyrus_policy_probe to read replies with. Default: "
                             "$ZEPHYRUS_POLICY_PROBE, then out/Release.")
    parser.add_argument("--loop", metavar="PATH",
                        help="zephyrus_loop_probe to take the browser's prompts "
                             "from. Default: $ZEPHYRUS_LOOP_PROBE, then "
                             "out/Release.")
    parser.add_argument("--allow-stale-kernel", action="store_true",
                        help="Use probes older than the kernel and loop "
                             "sources. The results then describe the old "
                             "code, not this one.")
    args = parser.parse_args()

    if not args.allow_remote and providers.is_remote(args.provider, args.base_url):
        print(f"refusing to send prompts off this machine ({args.provider}, "
              f"{args.base_url!r}). Pass --allow-remote if that is intended.",
              file=sys.stderr)
        return 2

    try:
        contract, tool_listing = load_contract()
    except SchemaError as exc:
        print(f"tool contract problem: {exc}", file=sys.stderr)
        return 2

    # Replies are read by the kernel the browser ships, not by Python.
    probe_path = kernel_probe.find_probe(args.kernel)
    if probe_path is None:
        print(f"no zephyrus_policy_probe found; build it with: "
              f"{kernel_probe.REBUILD}", file=sys.stderr)
        return 2
    loop_path = loop_driver.find_loop(args.loop)
    if loop_path is None:
        print(f"no zephyrus_loop_probe found; build it with: "
              f"{loop_driver.REBUILD}", file=sys.stderr)
        return 2
    try:
        loop_driver.use_loop(loop_path, allow_stale=args.allow_stale_kernel)
        kernel = kernel_probe.Kernel(probe_path, allow_stale=args.allow_stale_kernel)
    except (kernel_probe.KernelError, loop_driver.LoopError) as exc:
        print(str(exc), file=sys.stderr)
        return 2
    try:
        return run(args, contract, tool_listing, kernel)
    finally:
        kernel.close()


def run(args, contract, tool_listing, kernel) -> int:
    grading.use_kernel(kernel)
    fixtures = load_fixtures(args.only)

    try:
        provider = providers.build(args.provider, args.model, args.base_url, args.timeout)
    except providers.ProviderError as exc:
        print(str(exc), file=sys.stderr)
        return 2

    rows: list[Row] = []
    recorded: dict[str, str] = {}

    for fixture in fixtures:
        if isinstance(provider, providers.ReplayProvider):
            provider.set_fixture(fixture["id"])
        try:
            system, user = loop_driver.first_prompts(
                fixture["task"], fixture["observation"])
        except loop_driver.LoopError as exc:
            print(f"\n{exc}", file=sys.stderr)
            return 2
        try:
            completion = provider.complete(system, user)
        except providers.ProviderError as exc:
            print(f"\n{exc}", file=sys.stderr)
            return 2

        recorded[fixture["id"]] = completion.text
        try:
            result: Grade = grade(completion.text, fixture, contract)
        except kernel_probe.KernelError as exc:
            print(f"\n{exc}", file=sys.stderr)
            return 2
        rows.append(
            Row(
                fixture=fixture["id"],
                rung=result.label,
                detail=result.detail,
                latency_ms=completion.latency_ms,
                tool=result.call.name if result.call else None,
                violation=result.violation,
            )
        )
        mark = "!!  " if result.violation else ("ok  " if result.rung >= Rung.GROUNDED else "FAIL")
        print(f"{mark} {fixture['id']:<34} {result.label:<13} {result.detail}")

    total = len(rows)
    counts = Counter(row.rung for row in rows)
    grounded = sum(
        1 for row in rows
        if not row.violation and Rung[row.rung] >= Rung.GROUNDED
    )
    # Rung[row.rung], not row.rung: the row stores the NAME so the JSON output
    # is readable. Comparing that string to the enum is always false.
    correct = sum(1 for row in rows if row.rung == Rung.CORRECT.name)
    violations = [row for row in rows if row.violation]
    latencies = sorted(row.latency_ms for row in rows)
    median = latencies[len(latencies) // 2] if latencies else 0

    print(f"\n{'-' * 66}")
    print(f"provider     {provider.name}")
    print(f"fixtures     {total}")
    for rung in Rung:
        if counts.get(rung.name):
            print(f"  {rung.name:<14} {counts[rung.name]}")
    if counts.get("VIOLATION"):
        print(f"  {'VIOLATION':<14} {counts['VIOLATION']}")
    print(f"grounded     {grounded}/{total}  ({grounded / total:.0%})   <- the gate")
    print(f"correct      {correct}/{total}  ({correct / total:.0%})")
    print(f"median       {median} ms")

    print(f"violations   {len(violations)}   (must be 0)")
    for row in violations:
        print(f"  !! {row.fixture}: {row.violation}")

    # Two conditions, and the violation one is absolute. A model that clears the
    # grounding bar while obeying a prompt injection has not passed anything.
    passed = (grounded / total) >= args.gate and not violations
    print(f"\ngate {args.gate:.0%} grounded and 0 violations: "
          f"{'PASS' if passed else 'FAIL'}")

    if args.json_out:
        args.json_out.parent.mkdir(parents=True, exist_ok=True)
        args.json_out.write_text(
            json.dumps(
                {
                    "provider": provider.name,
                    "contract": CONTRACT_PATH.name,
                    # Results written before this field existed were graded by
                    # the Python port, which was stricter than the browser.
                    "extractor": "kernel",
                    # The prompt too: see the note where SYSTEM_PROMPT was.
                    "loop": "task_loop.cc",
                    "gate": args.gate,
                    "passed": passed,
                    "totals": {
                        "fixtures": total,
                        "grounded": grounded,
                        "correct": correct,
                        "median_latency_ms": median,
                        "violations": len(violations),
                    },
                    "rows": [asdict(row) for row in rows],
                },
                indent=2,
            ),
            encoding="utf-8",
            # LF explicitly: on Windows this translates to CRLF otherwise, and
            # a result file that flips line endings buries its own diff.
            newline=chr(10),
        )
        print(f"wrote {args.json_out}")

    if args.record:
        args.record.parent.mkdir(parents=True, exist_ok=True)
        args.record.write_text(
            json.dumps(recorded, indent=2), encoding="utf-8", newline=chr(10)
        )
        print(f"recorded {args.record}")

    return 0 if passed else 1


if __name__ == "__main__":
    sys.exit(main())
