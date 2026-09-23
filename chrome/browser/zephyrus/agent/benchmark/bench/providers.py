"""Model adapters.

The point of this file is that swapping the model must not touch grading. The
V1 local model is a benchmark outcome, not an assumption, so the harness talks
to whatever is running through one small interface.

Providers are LOCAL by default. `base_url` defaults to loopback and the
harness refuses a non-loopback host unless `--allow-remote` is passed, so a
benchmark cannot quietly start sending page content to a hosted API. The one
hosted provider, `claude`, is remote whatever `base_url` says -- see
`is_remote` -- so it needs that flag too.

Uses only the standard library, except the `claude` provider, which imports
the Anthropic SDK when it is chosen and not before. A benchmark that needs its
own dependency tree is a benchmark people stop running, so the local runs must
never need it.
"""

from __future__ import annotations

import json
import sys
import time
import urllib.error
import urllib.parse
import urllib.request
from dataclasses import dataclass
from typing import Any, Protocol


class ProviderError(Exception):
    """The model could not be reached or returned something unusable."""


@dataclass(frozen=True)
class Completion:
    text: str
    latency_ms: int
    # Tokens this reply cost, as the provider reports them: input, output, and
    # for Claude the cache reads and writes. None where nothing is reported.
    # Kept per step because the question it answers -- what does one TASK cost
    # -- is the number a model choice turns on, and a monthly bill cannot be
    # divided back into tasks.
    usage: dict[str, int] | None = None


class Provider(Protocol):
    name: str

    def complete(self, system: str, user: str) -> Completion: ...


def _post_json(url: str, payload: dict[str, Any], timeout: float) -> dict[str, Any]:
    body = json.dumps(payload).encode("utf-8")
    request = urllib.request.Request(
        url, data=body, headers={"Content-Type": "application/json"}, method="POST"
    )
    try:
        with urllib.request.urlopen(request, timeout=timeout) as response:
            return json.loads(response.read().decode("utf-8"))
    except urllib.error.HTTPError as exc:
        detail = exc.read().decode("utf-8", "replace")[:400]
        raise ProviderError(f"{url} returned HTTP {exc.code}: {detail}") from exc
    except urllib.error.URLError as exc:
        raise ProviderError(
            f"cannot reach {url}: {exc.reason}. Is the model server running?"
        ) from exc
    except TimeoutError as exc:
        raise ProviderError(f"{url} timed out after {timeout:.0f}s") from exc


class OllamaProvider:
    """Ollama's native chat endpoint."""

    def __init__(self, model: str, base_url: str, timeout: float) -> None:
        self.name = f"ollama:{model}"
        self._model = model
        self._url = urllib.parse.urljoin(base_url, "/api/chat")
        self._timeout = timeout

    def complete(self, system: str, user: str) -> Completion:
        started = time.monotonic()
        data = _post_json(
            self._url,
            {
                "model": self._model,
                "stream": False,
                "messages": [
                    {"role": "system", "content": system},
                    {"role": "user", "content": user},
                ],
                # Temperature zero because this measures capability, not
                # creativity, and a benchmark that moves between runs cannot
                # gate anything.
                # THE SAME OPTIONS DevModelClient SENDS. Copied deliberately,
                # and num_ctx is the one that matters: without it ollama uses
                # its own default of 2048, and a prompt that outgrows the
                # window is truncated from the FRONT -- which is where the
                # system prompt lives. MEASURED on the multi-step runner:
                # qwen2.5:7b returned four consecutive unparseable replies
                # once history had accumulated, and read as a broken model
                # rather than a truncated prompt. Short single-call prompts fit
                # in 2048, which is why this went unnoticed until a loop grew
                # one past it.
                "options": {
                    "temperature": 0,
                    "seed": 7,
                    "num_predict": 256,
                    "num_ctx": 8192,
                },
                "keep_alive": "30m",
            },
            self._timeout,
        )
        text = (data.get("message") or {}).get("content")
        if not isinstance(text, str):
            raise ProviderError(f"unexpected ollama response shape: {list(data)}")
        usage = {
            "input": int(data.get("prompt_eval_count") or 0),
            "output": int(data.get("eval_count") or 0),
        }
        return Completion(text, int((time.monotonic() - started) * 1000), usage)


class OpenAICompatibleProvider:
    """llama.cpp server, LM Studio, vLLM and anything else speaking /v1/chat/completions."""

    def __init__(self, model: str, base_url: str, timeout: float) -> None:
        self.name = f"openai-compatible:{model}"
        self._model = model
        self._url = urllib.parse.urljoin(base_url, "/v1/chat/completions")
        self._timeout = timeout

    def complete(self, system: str, user: str) -> Completion:
        started = time.monotonic()
        data = _post_json(
            self._url,
            {
                "model": self._model,
                "temperature": 0,
                "seed": 7,
                "messages": [
                    {"role": "system", "content": system},
                    {"role": "user", "content": user},
                ],
            },
            self._timeout,
        )
        choices = data.get("choices") or []
        if not choices:
            raise ProviderError(f"no choices in response: {list(data)}")
        text = (choices[0].get("message") or {}).get("content")
        if not isinstance(text, str):
            raise ProviderError("response contained no message content")
        return Completion(text, int((time.monotonic() - started) * 1000))


class ClaudeProvider:
    """Claude through the Messages API. The CEILING measurement.

    This is here to answer one question before any architecture is built on
    it: how many of the tasks a strong model finishes on the same fixtures,
    prompt and constants the local models were measured on. It is not a
    candidate for shipping as it stands -- the browser's loop is
    loopback-enforced in code and §13.4 records no upload. The fixtures are
    synthetic pages, so sending them costs nothing private.

    Two differences from the local providers, both forced by the API rather
    than chosen:

    - No temperature or seed. Current Opus models reject sampling parameters,
      so runs can differ. Run it more than once before believing a single
      task's outcome.
    - Thinking is on (adaptive). The local models get no hidden reasoning, so
      this measures the model as it would actually be used, not a handicapped
      one.
    """

    DEFAULT_MODEL = "claude-opus-5-5"

    def __init__(self, model: str, timeout: float, effort: str | None = None) -> None:
        try:
            import anthropic
        except ImportError as exc:
            raise ProviderError(
                "the claude provider needs the Anthropic SDK: pip install anthropic"
            ) from exc
        self._anthropic = anthropic
        self._model = model or self.DEFAULT_MODEL
        # Effort is how much the model thinks, and the API default differs by
        # model: `high` on Claude Opus 5, `medium` on Claude Opus 5.5. It goes
        # in the provider name so a result file says which it was -- the same
        # model at two efforts is two different measurements.
        self._effort = effort
        self.name = f"claude:{self._model}" + (f"@{effort}" if effort else "")
        # Turns a fallback model served, so a result can never be credited to
        # a model that did not produce it. See complete().
        self.fallback_turns = 0
        try:
            # Credentials resolve from ANTHROPIC_API_KEY, ANTHROPIC_AUTH_TOKEN
            # or an `ant auth login` profile. Never from an argument: a key on
            # a command line ends up in shell history.
            self._client = anthropic.Anthropic(timeout=timeout)
        except anthropic.AnthropicError as exc:
            raise ProviderError(
                f"cannot create the Claude client: {exc}. Set ANTHROPIC_API_KEY "
                f"or run `ant auth login`."
            ) from exc

    def complete(self, system: str, user: str) -> Completion:
        anthropic = self._anthropic
        started = time.monotonic()
        try:
            response = self._client.beta.messages.create(
                model=self._model,
                max_tokens=16000,
                thinking={"type": "adaptive"},
                **({"output_config": {"effort": self._effort}} if self._effort else {}),
                # The system prompt is identical on every step of every task,
                # so it is the prefix worth caching. Below the model's minimum
                # cacheable length this silently does nothing, which is fine.
                system=[
                    {
                        "type": "text",
                        "text": system,
                        "cache_control": {"type": "ephemeral"},
                    }
                ],
                messages=[{"role": "user", "content": user}],
                # A declined request is re-run on a fallback model inside the
                # same call. Counted below rather than hidden: a benchmark that
                # credited one model with another's answer would be lying.
                betas=["server-side-fallback-2026-07-01"],
                fallbacks="default",
            )
        except anthropic.AuthenticationError as exc:
            raise ProviderError(
                "Claude rejected the credentials. Check ANTHROPIC_API_KEY."
            ) from exc
        except anthropic.RateLimitError as exc:
            raise ProviderError(f"Claude rate limit: {exc.message}") from exc
        except anthropic.APIStatusError as exc:
            raise ProviderError(
                f"Claude returned HTTP {exc.status_code}: {exc.message}"
            ) from exc
        except anthropic.APIConnectionError as exc:
            raise ProviderError(f"cannot reach the Claude API: {exc}") from exc

        latency = int((time.monotonic() - started) * 1000)
        reported = response.usage
        usage = {
            "input": int(getattr(reported, "input_tokens", 0) or 0),
            # Thinking is billed as output, and it is in this count.
            "output": int(getattr(reported, "output_tokens", 0) or 0),
            "cache_read": int(getattr(reported, "cache_read_input_tokens", 0) or 0),
            "cache_write": int(getattr(reported, "cache_creation_input_tokens", 0) or 0),
        }
        if any(
            getattr(entry, "type", None) == "fallback_message"
            for entry in (response.usage.iterations or [])
        ):
            self.fallback_turns += 1
            print(f"  [claude] a fallback model ({response.model}) served this turn",
                  file=sys.stderr)
        if response.stop_reason == "refusal":
            # Recorded as a step with no call rather than an exception: an
            # exception ends the whole run, and one declined step is a result
            # about the model, not a broken harness.
            print("  [claude] declined this step", file=sys.stderr)
            return Completion("", latency, usage)
        if response.stop_reason == "max_tokens":
            raise ProviderError("Claude hit max_tokens; the reply was cut off")
        text = "".join(
            block.text for block in response.content if block.type == "text"
        )
        return Completion(text, latency, usage)

    # List prices in US dollars per million tokens: input, output, cache read,
    # cache write (5-minute). Checked 2026-09-23; a price that changes makes old
    # result files disagree with new ones, so it is recorded here rather than
    # looked up, and a model not listed reports tokens with no cost rather than
    # a guessed one.
    PRICES = {
        "claude-opus-5": (5.00, 25.00, 0.50, 6.25),
        "claude-opus-5-5": (4.00, 20.00, 0.20, 5.00),
    }

    def cost(self, usage: dict[str, int]) -> float | None:
        prices = self.PRICES.get(self._model)
        if prices is None:
            return None
        per_input, per_output, per_read, per_write = prices
        return (
            usage.get("input", 0) * per_input
            + usage.get("output", 0) * per_output
            + usage.get("cache_read", 0) * per_read
            + usage.get("cache_write", 0) * per_write
        ) / 1_000_000


class ReplayProvider:
    """Replays recorded responses from a file, keyed by fixture id.

    Two uses. It lets the harness be tested and the grader be trusted without a
    model present, and it makes a past run reproducible after the model that
    produced it has been replaced.
    """

    def __init__(self, path: str) -> None:
        self.name = f"replay:{path}"
        with open(path, "r", encoding="utf-8") as handle:
            self._responses: dict[str, str] = json.load(handle)
        self._current: str | None = None

    def set_fixture(self, fixture_id: str) -> None:
        self._current = fixture_id

    def complete(self, system: str, user: str) -> Completion:
        if self._current is None:
            raise ProviderError("replay provider used without set_fixture()")
        if self._current not in self._responses:
            raise ProviderError(f"no recorded response for fixture {self._current!r}")
        return Completion(self._responses[self._current], 0)


def build(kind: str, model: str, base_url: str, timeout: float,
          effort: str | None = None) -> Provider:
    if kind == "ollama":
        return OllamaProvider(model, base_url, timeout)
    if kind == "openai-compatible":
        return OpenAICompatibleProvider(model, base_url, timeout)
    if kind == "replay":
        return ReplayProvider(model)
    if kind == "claude":
        return ClaudeProvider(model, timeout, effort)
    raise ProviderError(f"unknown provider {kind!r}")


def is_loopback(base_url: str) -> bool:
    host = (urllib.parse.urlparse(base_url).hostname or "").lower()
    return host in {"localhost", "127.0.0.1", "::1", ""}


def is_remote(kind: str, base_url: str) -> bool:
    """Whether this provider sends the prompt off the machine.

    Asked of the PROVIDER, not only the address. The claude provider ignores
    --base-url, which defaults to loopback, so checking the address alone
    passed it as local -- the one provider that is certainly remote would have
    been the one the guard let through without --allow-remote.
    """
    if kind == "claude":
        return True
    if kind in ("replay", "script"):
        return False
    return not is_loopback(base_url)
