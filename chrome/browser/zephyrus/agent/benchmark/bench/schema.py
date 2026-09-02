"""Validation for the Zephyrus tool contract.

This deliberately implements only the JSON Schema subset that tools.v1.json
uses, rather than pulling in a general-purpose validator.

The reason is not dependency avoidance. The Rust tool runtime will have to
validate the same schemas at execution time, and a permissive validator here
would let the contract drift into constructs Rust does not implement. Keeping
the checker small and explicit means the schema surface stays inside what both
sides can enforce. If a tool needs a construct that is not listed in
`SUPPORTED_KEYWORDS`, that is a decision to make on purpose, in both languages.

Supported: type (object/string/integer/number/boolean), properties, required,
additionalProperties=false, enum, minimum, maximum, minLength, format=uri.
"""

from __future__ import annotations

import re
from dataclasses import dataclass, field
from typing import Any

SUPPORTED_KEYWORDS = frozenset(
    {
        "type",
        "properties",
        "required",
        "additionalProperties",
        "enum",
        "minimum",
        "maximum",
        "minLength",
        "format",
        "description",
    }
)

_TYPES: dict[str, type | tuple[type, ...]] = {
    "object": dict,
    "string": str,
    "integer": int,
    "number": (int, float),
    "boolean": bool,
}

# Deliberately strict. A model that emits a bare hostname has not produced a
# navigable URL, and we would rather see that as a failure here than discover it
# when the browser refuses to load it.
_URI = re.compile(r"^[a-zA-Z][a-zA-Z0-9+.\-]*://[^\s]+$")


class SchemaError(Exception):
    """The schema itself is malformed or uses an unsupported construct."""


@dataclass(frozen=True)
class ValidationResult:
    ok: bool
    errors: list[str] = field(default_factory=list)

    def __bool__(self) -> bool:
        return self.ok


def assert_schema_supported(schema: dict[str, Any], where: str = "schema") -> None:
    """Raise if `schema` uses anything this validator cannot enforce.

    Called once at load time so an unsupported construct fails loudly at
    startup rather than silently passing every value at grading time.
    """
    if not isinstance(schema, dict):
        raise SchemaError(f"{where}: expected an object")

    unsupported = set(schema) - SUPPORTED_KEYWORDS
    if unsupported:
        raise SchemaError(
            f"{where}: unsupported schema keywords {sorted(unsupported)}. "
            "Add them to this validator and to the Rust runtime together, or "
            "change the schema."
        )

    declared = schema.get("type")
    if declared is not None and declared not in _TYPES:
        raise SchemaError(f"{where}: unknown type {declared!r}")

    if declared == "object" and schema.get("additionalProperties") is not False:
        raise SchemaError(
            f"{where}: object schemas must set additionalProperties to false. "
            "An invented argument is a failed call, not a call with extra data."
        )

    for name, sub in (schema.get("properties") or {}).items():
        assert_schema_supported(sub, f"{where}.properties.{name}")


def validate(value: Any, schema: dict[str, Any], path: str = "$") -> ValidationResult:
    """Validate `value` against `schema`. Collects every error, not just the first."""
    errors: list[str] = []
    _validate_into(value, schema, path, errors)
    return ValidationResult(ok=not errors, errors=errors)


def _validate_into(
    value: Any, schema: dict[str, Any], path: str, errors: list[str]
) -> None:
    declared = schema.get("type")
    if declared is not None:
        expected = _TYPES[declared]
        # bool is a subclass of int in Python; an integer field must not accept
        # True. This bites in practice because models emit bare `true`.
        if declared in ("integer", "number") and isinstance(value, bool):
            errors.append(f"{path}: expected {declared}, got boolean")
            return
        if not isinstance(value, expected):
            errors.append(
                f"{path}: expected {declared}, got {type(value).__name__}"
            )
            return

    if "enum" in schema and value not in schema["enum"]:
        errors.append(f"{path}: {value!r} is not one of {schema['enum']}")

    if isinstance(value, str):
        min_len = schema.get("minLength")
        if min_len is not None and len(value) < min_len:
            errors.append(f"{path}: shorter than minLength {min_len}")
        if schema.get("format") == "uri" and not _URI.match(value):
            errors.append(f"{path}: {value!r} is not an absolute URL with a scheme")

    if isinstance(value, (int, float)) and not isinstance(value, bool):
        minimum = schema.get("minimum")
        maximum = schema.get("maximum")
        if minimum is not None and value < minimum:
            errors.append(f"{path}: {value} is below minimum {minimum}")
        if maximum is not None and value > maximum:
            errors.append(f"{path}: {value} is above maximum {maximum}")

    if isinstance(value, dict):
        properties = schema.get("properties") or {}
        for name in schema.get("required") or []:
            if name not in value:
                errors.append(f"{path}: missing required property {name!r}")

        if schema.get("additionalProperties") is False:
            for name in value:
                if name not in properties:
                    errors.append(f"{path}: unexpected property {name!r}")

        for name, sub_value in value.items():
            sub_schema = properties.get(name)
            if sub_schema is not None:
                _validate_into(sub_value, sub_schema, f"{path}.{name}", errors)
