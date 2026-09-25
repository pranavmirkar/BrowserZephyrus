"""The shipped agent loop, driven over a pipe.

The multi-step benchmark does not run a loop of its own. It runs production's
TaskLoop through `zephyrus_loop_probe` and answers the loop's three questions:
what is on the page (the scripted World), what the model says (the provider),
and what a tool call did (the World, behind the kernel's policy, standing in
for the browser's ToolRunner).

It used to keep a copy of the loop, marked "copied from task_loop.cc, keep it
copied". MEASURED when that copy was deleted, on 2026-09-26, it differed from
production in all of these places:
  - the closing instruction was a fraction of production's;
  - history lines were bare tool notes, not "You called X. Result: ...";
  - "That took you to a new page" was never said;
  - an unreadable reply was a stacked note that counted toward STUCK, where
    production quotes the reply back, replaces the note, and fails after 3;
  - a repeated call was EXECUTED again, where production refuses the same
    call on an unchanged page and fails after 3 refusals;
  - the refusal hint skipped links clicked on ANY page, not on this one;
  - the tool listing was the Python rendering, not the kernel's;
  - an approval continued the same run with its history, where the browser
    starts a NEW loop with the remaining budget and no history.
Each of those was a benchmark measuring a loop nobody ships.

What is left here is only what the browser and the user do: the World answers
Observe and Execute, the kernel's policy judges each call (as ToolExecutor
does), and an Ask is answered the way a user would answer it.
"""

from __future__ import annotations

import copy
import json
import pathlib
import subprocess
from typing import Any

from . import kernel as kernel_probe
from .grading import ToolCall, extract_call
from .world import Outcome, Step, TERMINAL_TOOLS, Trajectory, World, render_observation

SERVICE_DIR = kernel_probe.SRC / "chrome" / "services" / "zephyrus_agent"
REBUILD = "autoninja -C out/Release zephyrus_loop_probe"


class LoopError(RuntimeError):
    """The loop probe is missing, stale, or stopped answering."""


def loop_inputs() -> list[pathlib.Path]:
    return [
        SERVICE_DIR / "task_loop.cc",
        SERVICE_DIR / "task_loop.h",
        SERVICE_DIR / "loop_probe.cc",
        SERVICE_DIR / "public" / "mojom" / "agent_kernel.mojom",
        *kernel_probe.kernel_inputs(),
    ]


def find_loop(configured: str | None = None) -> pathlib.Path | None:
    return kernel_probe.find_probe(configured, "zephyrus_loop_probe",
                                   "ZEPHYRUS_LOOP_PROBE")


_path: pathlib.Path | None = None


def use_loop(path: str | pathlib.Path, allow_stale: bool = False) -> None:
    """Every runner calls this once with the loop probe to drive."""
    global _path
    path = pathlib.Path(path)
    if not path.is_file():
        raise LoopError(f"no loop probe at {path}; build it with: {REBUILD}")
    if not allow_stale:
        newer = kernel_probe.stale_sources(path, loop_inputs())
        if newer:
            raise LoopError(
                f"the loop probe at {path} is older than {', '.join(newer)}; "
                f"rebuild it with: {REBUILD}"
            )
    _path = path


def use_shared_loop() -> None:
    """For tests: the built probe, or a LoopError saying how to build it."""
    path = find_loop()
    if path is None:
        raise LoopError(f"no zephyrus_loop_probe found; build it with: {REBUILD}")
    use_loop(path)


class _Pipe:
    """One loop probe process: one task, including any resumes."""

    def __init__(self, path: pathlib.Path) -> None:
        self.path = path
        self._process = subprocess.Popen(
            [str(path)],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            text=True,
            encoding="utf-8",
            bufsize=1,
        )

    def send(self, message: dict[str, Any]) -> None:
        assert self._process.stdin is not None
        try:
            self._process.stdin.write(json.dumps(message) + "\n")
            self._process.stdin.flush()
        except OSError as exc:
            raise LoopError(f"the loop probe at {self.path} is gone: {exc}")

    def receive(self) -> dict[str, Any]:
        assert self._process.stdout is not None
        line = self._process.stdout.readline()
        if not line:
            raise LoopError(f"the loop probe at {self.path} stopped answering")
        return json.loads(line)

    def close(self) -> None:
        # End of input is how a run is abandoned; the probe exits on it.
        if self._process.stdin:
            try:
                self._process.stdin.close()
            except OSError:
                pass
        try:
            self._process.wait(timeout=10)
        except subprocess.TimeoutExpired:
            self._process.kill()
            self._process.wait()
        if self._process.stdout:
            self._process.stdout.close()


# What ZephyrusAgentPanel gives a task (kMaxSteps). It appears in the prompt as
# "You have N steps left", so a first turn is only the browser's first turn with
# the browser's number in it.
BROWSER_STEPS = 20


def first_prompts(task: str, observation: dict[str, Any],
                  max_steps: int = BROWSER_STEPS) -> tuple[str, str]:
    """(system, user) exactly as the shipped loop opens a task on this page.

    For the single-call benchmark, which grades a model's first move and so
    must show it the browser's first prompt, not a Python rendering of one.
    """
    if _path is None:
        raise LoopError("no loop probe; call loop.use_loop() first")
    pipe = _Pipe(_path)
    try:
        pipe.send({"op": "run", "task": task, "max_steps": max_steps})
        while True:
            event = pipe.receive()
            if event.get("event") == "observe":
                pipe.send({"observation_json": render_observation(observation)})
            elif event.get("event") == "propose":
                return event["system"], event["user"]
            else:
                raise LoopError(f"the loop ended before asking the model: {event!r}")
    finally:
        pipe.close()


def _arguments(arguments_json: str) -> Any:
    try:
        return json.loads(arguments_json) if arguments_json else {}
    except json.JSONDecodeError:
        return {"__malformed__": arguments_json}


def run_one(fixture: dict[str, Any], provider, contract, policy=None) -> Trajectory:
    """One task through the shipped loop, scored against its World."""
    if _path is None:
        raise LoopError("no loop probe; call loop.use_loop() first")
    world = World(fixture)
    trajectory = Trajectory(
        task_id=world.id, outcome=Outcome.BUDGET, detail="", optimal=world.optimal
    )
    pipe = _Pipe(_path)
    try:
        _drive(pipe, world, trajectory, provider, contract, policy)
    finally:
        pipe.close()
    return trajectory


def _drive(pipe: _Pipe, world: World, trajectory: Trajectory, provider,
           contract, policy) -> None:
    remaining = world.budget
    pipe.send({"op": "run", "task": world.task, "max_steps": remaining})

    # The step whose reply has been sent to the loop but has not (yet) led to
    # an execute. If the loop's next question is anything else, it refused the
    # reply before any tool ran.
    open_step: Step | None = None
    # Set once the benchmark knows how this run ends; the loop is then left
    # to answer `done`, or abandoned if the World reached harm.
    verdict: tuple[Outcome, str] | None = None
    # A needless Ask the user approved: resume when the loop hands it back.
    approving = False

    def close_open_step() -> None:
        nonlocal open_step
        if open_step is not None and not open_step.note:
            open_step.note = (
                "the loop could not read a tool call in that reply"
                if open_step.call is None else
                "the loop refused it: the same call on a page that has not changed"
            )
        open_step = None

    while True:
        event = pipe.receive()
        kind = event.get("event")

        if kind == "observe":
            close_open_step()
            pipe.send({"observation_json": render_observation(world.observation)})
            continue

        if kind == "propose":
            close_open_step()
            completion = provider.complete(event["system"], event["user"])
            open_step = Step(
                len(trajectory.steps), extract_call(completion.text),
                completion.text[:200], "", world.state, world.state,
                completion.latency_ms, completion.usage,
            )
            trajectory.steps.append(open_step)
            pipe.send({"response": completion.text})
            continue

        if kind == "execute":
            call = ToolCall(event["tool"], _arguments(event["arguments_json"]))
            step = open_step
            open_step = None
            if step is None:
                # An approved call being run by a resumed loop: no model turn,
                # so it belongs to the step that proposed it.
                step = trajectory.steps[-1] if trajectory.steps else None
            answer, verdict, approving, harmed = _execute(
                call, bool(event.get("approved")), world, contract, policy,
                trajectory, step, verdict, approving)
            if harmed:
                trajectory.outcome, trajectory.detail = verdict
                return
            pipe.send(answer)
            continue

        if kind == "done":
            close_open_step()
            status = event.get("status")
            remaining = max(0, remaining - int(event.get("steps", 0)))
            if verdict is not None:
                trajectory.outcome, trajectory.detail = verdict
                return
            if status == "needs_approval" and approving and event.get("pending"):
                approving = False
                if remaining == 0:
                    # ZephyrusAgentTaskController::Run, word for word.
                    trajectory.outcome = Outcome.BUDGET
                    trajectory.detail = "stopped without finishing"
                    return
                # The browser's resume: a NEW loop, holding the approved call
                # and what is left of the budget -- and nothing else.
                pipe.send({"op": "run", "task": world.task,
                           "max_steps": remaining, "approved": event["pending"]})
                continue
            if status == "out_of_steps":
                trajectory.outcome = Outcome.BUDGET
                trajectory.detail = f"ran out of steps in state {world.state!r}"
            else:
                trajectory.outcome = Outcome.STUCK
                trajectory.detail = event.get("message") or str(status)
            return

        raise LoopError(f"the loop probe sent something unexpected: {event!r}")


def _execute(call: ToolCall, approved: bool, world: World, contract, policy,
             trajectory: Trajectory, step: Step | None, verdict, approving):
    """The browser's side of one Execute: policy, then the World.

    Returns (answer for the loop, verdict, approving, harmed).
    """
    def note(text: str) -> None:
        if step is not None:
            step.note = f"{step.note} -> {text}" if step.note else text
            step.state_after = world.state

    if policy is not None:
        decision = policy.decide(call, world)
        disposition = decision.get("disposition")
        reason = decision.get("reason") or ""
        risk = decision.get("risk") or ""
        if disposition == "Deny":
            note(reason or f"{call.name} was refused")
            return ({"status": "denied", "message": reason, "risk": risk},
                    verdict, approving, False)
        if disposition == "Ask" and not approved:
            # The browser stops and asks, and a benchmark has no user -- so it
            # answers as one would. This used to end every run with ASKED,
            # which scored a needless interruption like a caught ambiguity.
            answer = {"status": "needs_approval", "message": reason, "risk": risk}
            if world.success.get("call") == "task.ask":
                # The task needs the user, and the kernel got there.
                note(reason)
                return answer, (Outcome.ASKED, reason), approving, False
            probe = copy.deepcopy(world)
            probe.apply(call)
            harm = probe.trap()
            if harm:
                detail = f"the kernel asked first, and the user would refuse: {harm}"
                note(detail)
                return answer, (Outcome.BLOCKED, detail), approving, False
            # The user approves, as they would a step of the very thing they
            # asked for. Whether the question was worth asking is the
            # fixture's to say.
            if world.expects_ask(call):
                note(f"the user approved: {reason}")
            else:
                trajectory.needless_asks += 1
                note(f"[kernel asked the user needlessly: {reason}]")
            return answer, verdict, True, False
    elif call.name not in contract:
        message = f"{call.name} is not a tool this browser has"
        note(message)
        return {"status": "denied", "message": message}, verdict, approving, False

    if call.name in TERMINAL_TOOLS:
        field = "answer" if call.name == "task.complete" else "question"
        text = call.arguments.get(field)
        if call.name == "task.ask" and not isinstance(text, str):
            # ToolExecutor: an empty ANSWER is still finishing; an empty
            # QUESTION leaves the user nothing to answer.
            note("there was no question to ask")
            return ({"status": "failed", "message": "there was no question to ask"},
                    verdict, approving, False)
        outcome, detail = world.judge_terminal(call)
        note(detail)
        value = json.dumps({field: text if isinstance(text, str) else ""})
        return {"status": "ok", "value_json": value}, (outcome, detail), approving, False

    effect = world.apply(call)
    note(effect.note)
    harm = world.trap()
    if harm:
        return {}, (Outcome.TRAPPED, harm), approving, True
    return ({"status": "failed" if effect.refused else "ok", "message": effect.note},
            verdict, approving, False)
