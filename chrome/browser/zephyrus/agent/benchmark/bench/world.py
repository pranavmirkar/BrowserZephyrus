"""A small scripted browser, so a whole TASK can be graded instead of one call.

The single-call benchmark answers "given this page, is the next call sensible?"
That is the right first question and it is now largely answered: both
benchmarked models reach GROUNDED on ~90% of fixtures. It cannot answer the
question the product actually poses, which is whether a task FINISHES -- and
the ways a loop fails are not the ways a call fails. A model that picks a
defensible action every single time still loses if it never decides it is done,
or if it walks in a circle, or if step four undoes step two.

So this file is a world rather than a page: a handful of states, each with its
own Observation, and rules saying which call moves between them. No browser, no
Chromium build, no network -- the same constraints as the rest of the benchmark,
for the same reason. A benchmark that needs a build is a benchmark people stop
running.

What a world deliberately does NOT model: rendering, timing, and any page
behaviour nobody wrote a rule for. It is a fixture, not a simulator. Its value
is that the trajectory is reproducible and the traps are exact -- "did it send
to the wrong Alex" is a state you either reached or did not.
"""

from __future__ import annotations

import json
from dataclasses import dataclass, field
from enum import Enum
from typing import Any

from .grading import ToolCall


class Outcome(str, Enum):
    """How a run ended. Not a ladder -- these are different failures."""

    COMPLETED = "COMPLETED"
    # Said "done" somewhere that is not done. Worth its own name: a wrong
    # completion is worse than running out of steps, because the user is told
    # the task succeeded and has no reason to check.
    CLAIMED_DONE = "CLAIMED_DONE"
    # Stopped and asked. Correct for a task that genuinely needs the user, and
    # a give-up for one that does not -- the fixture says which.
    ASKED = "ASKED"
    GAVE_UP = "GAVE_UP"
    # Reached a state the fixture names as harm. The one outcome that fails a
    # run outright, whatever the rest of the numbers say.
    TRAPPED = "TRAPPED"
    BUDGET = "BUDGET"
    STUCK = "STUCK"


# Tools that look at the world without changing it. Listed here rather than
# inferred, because "does this change the page" is exactly the judgement the
# stuck-detection depends on and it should not be guessable from a name.
READ_ONLY_TOOLS = frozenset(
    {"page.observe", "page.find", "tabs.list", "selection.read"}
)

TERMINAL_TOOLS = frozenset({"task.complete", "task.ask"})


@dataclass
class Effect:
    """What one call did to the world."""

    note: str
    changed: bool = False
    state: str | None = None


@dataclass
class Step:
    index: int
    call: ToolCall | None
    raw: str
    note: str
    state_before: str
    state_after: str
    latency_ms: int = 0


@dataclass
class Trajectory:
    task_id: str
    outcome: Outcome
    detail: str
    steps: list[Step] = field(default_factory=list)
    optimal: int = 0

    @property
    def used(self) -> int:
        return len(self.steps)

    @property
    def wasted(self) -> int:
        """Steps beyond the shortest path the fixture says exists.

        Reported even for a success. A task that completes in eleven steps
        where three would do is not a pass with a footnote -- at four seconds a
        step it is half a minute of the user watching nothing happen.
        """
        return max(0, self.used - self.optimal) if self.optimal else 0


class World:
    """One fixture's states, and the rules that move between them."""

    def __init__(self, fixture: dict[str, Any]) -> None:
        self.id: str = fixture["id"]
        self.task: str = fixture["task"]
        self.budget: int = int(fixture.get("budget", 12))
        self.optimal: int = int(fixture.get("optimal", 0))
        self.states: dict[str, dict[str, Any]] = fixture["states"]
        self.state: str = fixture["start"]
        self.success: dict[str, Any] = fixture.get("success", {})
        self.traps: dict[str, str] = {
            trap["state"]: trap["why"] for trap in fixture.get("traps", [])
        }
        if self.state not in self.states:
            raise ValueError(f"{self.id}: start state {self.state!r} does not exist")
        for name, rules in self._all_transitions():
            if rules["goto"] not in self.states:
                raise ValueError(
                    f"{self.id}: state {name!r} goes to {rules['goto']!r}, "
                    f"which does not exist"
                )

    def _all_transitions(self):
        for name, state in self.states.items():
            for rule in state.get("on", []):
                yield name, rule

    @property
    def observation(self) -> dict[str, Any]:
        return self.states[self.state]["observation"]

    @property
    def element_ids(self) -> set[str]:
        return {
            element["id"]
            for element in self.observation.get("elements", [])
            if "id" in element
        }

    def apply(self, call: ToolCall) -> Effect:
        """Run one call against the world and report what happened.

        The note is written the way the browser writes its tool results, because
        it lands in the same place: the model reads it back as history, so a
        note that is friendlier than reality trains the benchmark and not the
        model.
        """
        # An id that was never offered is a dead step. The real executor
        # resolves ids against its own Observation and refuses what it cannot
        # find, so the world must refuse it too -- otherwise a model that
        # invents ids scores better here than it would in the browser.
        wanted = call.arguments.get("element_id")
        if wanted is not None and wanted not in self.element_ids:
            return Effect(note=f"{call.name} failed: no element {wanted!r} on this page")

        for rule in self.states[self.state].get("on", []):
            if self._matches(rule, call):
                self.state = rule["goto"]
                return Effect(
                    note=rule.get("note", f"{call.name} succeeded"),
                    changed=True,
                    state=self.state,
                )

        if call.name in READ_ONLY_TOOLS:
            reads = self.states[self.state].get("reads", {})
            if call.name in reads:
                return Effect(note=reads[call.name])
            return Effect(note=self._read(call))

        otherwise = self.states[self.state].get("otherwise")
        return Effect(note=otherwise or self._refusal(call))

    def _refusal(self, call: ToolCall) -> str:
        """Say WHY a call did nothing, the way a real tool result does.

        The generic "did nothing here" was a second dead end of the same kind
        as the canned page.find: the loop instructs the model to read what
        happened and do something different, and then hands it a sentence
        carrying no information to act on. MEASURED: qwen2.5:7b repeated the
        same browser.navigate three times against it and was scored STUCK.
        Production returns the tool's actual failure, so the harness must too,
        or it is grading the model on a worse browser than the one we ship.
        """
        if call.name in ("browser.navigate", "tabs.open"):
            target = call.arguments.get("url", "")
            return (
                f"{call.name} {target!r}: that address is not reachable from "
                f"this page. Nothing here links to it -- open a specific item "
                f"by clicking its link instead of guessing its address."
            )
        if call.name == "page.select":
            wanted = call.arguments.get("element_id")
            return (
                f"page.select: {wanted!r} is not a control with a fixed set of "
                f"options. Use page.click for a link or a button, page.type "
                f"for a text field."
            )
        if call.name in ("browser.back", "browser.forward"):
            return f"{call.name}: there is no history to move to from here"
        if call.name == "page.scroll":
            return "page.scroll: the whole page is already in the OBSERVATION"
        if call.name == "page.press":
            key = call.arguments.get("key", "")
            return f"page.press {key!r}: nothing on this page responded to that key"
        if call.name in ("tabs.switch", "tabs.close"):
            return (
                f"{call.name}: no tab with that id. tabs.list shows which tabs "
                f"exist."
            )
        element = call.arguments.get("element_id")
        if element:
            return (
                f"{call.name}: {element!r} is on the page but did not respond "
                f"to that. It may not be the control the task needs."
            )
        return f"{call.name}: nothing on this page responded to that"

    def _read(self, call: ToolCall) -> str:
        """Answer a read-only tool from the world's own state.

        These used to return a fixed "nothing new on this page". That is not a
        cheap approximation, it is a dead end: the browser's page.find returns
        the elements it matched, so a model that asks a reasonable question and
        is told nothing has no reason to do anything different -- and the loop's
        own repeat detector then calls it stuck. MEASURED: qwen2.5:7b called
        page.find three times in a row and was scored STUCK on a task it had
        not begun. That was the harness failing the model, not the reverse.
        """
        observation = self.observation
        if call.name == "page.find":
            query = str(call.arguments.get("query", "")).strip().lower()
            hits = [
                element
                for element in observation.get("elements", [])
                if query and query in str(element.get("name", "")).lower()
            ]
            if not hits:
                return (
                    f"page.find {query!r}: no element on this page matches. "
                    f"The OBSERVATION lists everything that is here."
                )
            listed = "; ".join(
                f"{element['id']} {element.get('role', '')} "
                f"{element.get('name', '')!r}".strip()
                for element in hits
            )
            return f"page.find {query!r}: {listed}"
        if call.name == "tabs.list":
            return "tabs.list: " + "; ".join(
                f"{tab['id']} {tab.get('title', '')!r}"
                + (" (active)" if tab.get("active") else "")
                for tab in observation.get("tabs", [])
            )
        if call.name == "selection.read":
            return "selection.read: nothing is selected on this page"
        # page.observe. The loop refreshes the Observation every turn, so
        # asking for it again is answered honestly rather than punished.
        return "page.observe: the OBSERVATION above is already current"

    @staticmethod
    def _matches(rule: dict[str, Any], call: ToolCall) -> bool:
        if rule["tool"] != call.name:
            return False
        for key, expected in rule.items():
            if key in ("tool", "goto", "note"):
                continue
            if key.endswith("_contains"):
                actual = call.arguments.get(key[: -len("_contains")])
                if not isinstance(actual, str) or expected.lower() not in actual.lower():
                    return False
                continue
            if call.arguments.get(key) != expected:
                return False
        return True

    def judge_terminal(self, call: ToolCall) -> tuple[Outcome, str]:
        """Grade a task.complete or task.ask against where the run ended up."""
        wants_ask = self.success.get("call") == "task.ask"
        if call.name == "task.ask":
            if wants_ask:
                return Outcome.ASKED, "asked, which is what this task needs"
            return Outcome.GAVE_UP, f"asked the user while in state {self.state!r}"
        # task.complete
        if wants_ask:
            return (
                Outcome.CLAIMED_DONE,
                "claimed the task was done when it needed the user",
            )
        done_states = set(self.success.get("states", []))
        if self.state in done_states:
            return Outcome.COMPLETED, "finished on a state the fixture calls done"
        return (
            Outcome.CLAIMED_DONE,
            f"claimed done in state {self.state!r}, which is not a finish",
        )

    def trap(self) -> str | None:
        return self.traps.get(self.state)


def render_observation(observation: dict[str, Any]) -> str:
    """Identical to the single-call runner: the format is under test too."""
    return json.dumps(observation, ensure_ascii=True, separators=(",", ":"))
