# Cloud models are spoken to by the kernel, through the browser

Users connect a cloud model of their choice (Anthropic, any OpenAI-compatible
endpoint, Google Gemini). The kernel builds each provider's request and parses
each provider's reply. The browser only sends the request: it adds the user's
key and sends to the endpoint the user configured, and nothing else.

The alternative was provider adapters in the browser process, the usual place
for network clients. We rejected it for three reasons:

- **Replies are untrusted input.** A provider's reply is model output, and
  parsing untrusted model output is exactly what the Rust kernel in a `kService`
  sandbox is for (ADR 0001). Putting provider JSON parsers in the browser process
  would put the widest new parsing surface on the most privileged side.
- **One implementation, measured.** The benchmark drives the kernel through a
  probe. With adapters in the kernel, every provider is tested by the same code
  the browser runs. The benchmark has already paid twice for keeping a second
  copy of something (the extractor, then the loop).
- **The upstream patch stays small.** The browser side is a transport and a key
  store under `chrome/browser/zephyrus/`.

## What each side may do

The kernel produces `{path, headers, body}` for a named provider kind and model.
It never sees the key and cannot choose the host.

The browser:

- sends only to the base URL the user saved for that provider;
- accepts only a path in that provider's allowed shape;
- accepts only the non-secret headers on a fixed list, and adds the auth header
  itself;
- sends without cookies or other credentials of any Workspace;
- caps the size of the reply.

Keys are encrypted with OSCrypt and live only in the browser process.

## Decisions taken with this (2026-09-28)

- **Cloud is off until the user enables it for a Workspace.** Adding a key does
  not send anything anywhere. Fields the sanitizer marks sensitive (passwords,
  card numbers, and the rest) are withheld from every model, cloud or local.
- **First providers:** Anthropic, OpenAI-compatible (which also covers
  OpenRouter, DeepSeek, Jev, vLLM and Ollama), Gemini.
- **Default security is a provenance monitor**: the model sees the page, and the
  kernel stops page-derived data from reaching sinks. Plan-first strict mode is a
  per-Task option, not the default, because published results show it keeps
  only up to 57% of a frontier model's success.

## Consequences

The kernel's `AgentModel` seam is replaced for cloud models by a transport the
browser implements. The local developer model keeps its seam until it moves to
the OpenAI-compatible adapter (Ollama speaks it).

Tool names are mapped for the wire (`page.click` to `page_click`), because
Anthropic and OpenAI do not allow dots. The mapping is built from the contract
and checked for collisions when the kernel loads it.
