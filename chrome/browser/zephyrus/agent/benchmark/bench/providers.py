"""Model adapters.

The point of this file is that swapping the model must not touch grading. The
V1 local model is a benchmark outcome, not an assumption, so the harness talks
to whatever is running through one small interface.

Every provider is expected to be LOCAL. `base_url` defaults to loopback and the
harness refuses a non-loopback host unless `--allow-remote` is passed, so a
benchmark cannot quietly start sending page content to a hosted API.

Uses only the standard library. A benchmark that needs its own dependency tree
is a benchmark people stop running.
"""

from __future__ import annotations

import json
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
        return Completion(text, int((time.monotonic() - started) * 1000))


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


def build(kind: str, model: str, base_url: str, timeout: float) -> Provider:
    if kind == "ollama":
        return OllamaProvider(model, base_url, timeout)
    if kind == "openai-compatible":
        return OpenAICompatibleProvider(model, base_url, timeout)
    if kind == "replay":
        return ReplayProvider(model)
    raise ProviderError(f"unknown provider {kind!r}")


def is_loopback(base_url: str) -> bool:
    host = (urllib.parse.urlparse(base_url).hostname or "").lower()
    return host in {"localhost", "127.0.0.1", "::1", ""}
