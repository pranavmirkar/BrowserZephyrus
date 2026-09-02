# Agent Kernel runs in its own process

The Agent Kernel owns Task lifecycle, Policy, Context and model routing, and it
drives a local inference worker. We run it as a separate process reached over
Mojo, rather than in the browser process, so that a model crash, a GPU
exhaustion or a runaway inference loop cannot take the browser down with it.

The second reason matters as much as the first. A process boundary forces every
capability the kernel wants to be declared in a mojom interface, which keeps the
Chromium-side patch small by construction. Our fork already carries 10,325 added
lines across 209 upstream files, all re-applied on every rebase; an in-process
kernel would have made it easy to reach into browser internals directly and grow
that number without noticing.

## Consequences

The bridge is budgeted at **400 lines of upstream patch or less**. Exceeding it
is treated as a design failure, not a cost of doing business. Anything above the
line belongs in `chrome/browser/zephyrus/`, which rebases for free.

Capabilities must be added deliberately, one mojom method at a time. This is
slower than calling a browser API directly and that is the point.

The kernel cannot hold pointers into Chromium objects. It sends typed commands
and receives typed observations. Chromium remains free to reject a request that
has gone stale.

## Validation (2026-09-02)

Built and measured rather than assumed. The kernel is a Rust policy engine in
`//chrome/services/zephyrus_agent`, hosted in a sandboxed utility process and
reached from the browser over the mojom interface in `public/mojom`.

| Claim | Result |
|---|---|
| Rust builds in our config (official, non-component, PGO, ThinLTO) | Yes. `enable_rust` was already true; `args.gn` needed no change |
| **Total upstream patch** | **12 lines across 5 files**, against the 400-line budget |
| Our side | ~1,600 lines, all new files under `chrome/services/zephyrus_agent` and `chrome/browser/zephyrus/agent` |
| Cost of crossing the language boundary | 3.2 us per call (10k iterations), against a model step of 1.6-5.9 **seconds** |
| Kernel runs outside the browser | Proven. `ZephyrusAgentKernelBrowserTest.RunsOutsideTheBrowserProcess` waits for the real service launch and asserts its pid is not the browser's |

The 12 lines: 8 in `chrome/utility/services.cc` (include, factory, registration),
and one each in `chrome/utility/DEPS`, `chrome/utility/BUILD.gn`,
`chrome/browser/BUILD.gn` and `chrome/test/BUILD.gn`. **The budget holds with
room to spare, and the shape of the cost is the interesting part: adding a
capability later is one mojom method, not a new patch site.**

### Three things the build decided for us

**The kernel could not live under `//chrome/browser`.** `chrome/services/DEPS`
forbids including from there, which is the layering rule doing its job -- code
that runs in the utility process has no business in the browser's tree. It moved
to `//chrome/services/zephyrus_agent/kernel`.

**`kService`, not `kUtility`.** The kernel parses JSON and compares strings. It
opens no files, makes no network requests and runs no dynamic code, so the
tighter sandbox applies. `kUtility` would have granted all three on Windows for
no reason.

**No `Result` across the FFI.** cxx maps a Rust `Result` to a C++ exception and
this codebase builds without exceptions, so the kernel returns a Deny decision
for malformed input instead of an error. Failing closed and having no error
channel turned out to be the same decision, and every generated entry point is
`noexcept` as a result.

### The guarantee the client carries

`AgentKernelClient` promises that **every request is answered.** If the service
dies with a call in flight, mojo destroys the reply callback without running it,
and an agent loop waiting on that reply is stuck rather than safe. Calls are
wrapped so an unreachable kernel completes with `kDeny` and a reason. A crashed
kernel is a denied action, never a hung one.

### The task loop, and where Rust stops (2026-09-02)

The loop lives in this process, in the service's C++ shell, not in the browser.
That is this ADR's own reasoning applied literally: a runaway inference loop can
only fail to take the browser down if the loop is on this side of the boundary.

It is C++ rather than Rust, and that is worth recording because the obvious
reading of "the kernel is Rust" would say otherwise. **`mojo/public/rust` now
exists** -- Remote/Receiver bindings, with `mojom()` generating a `_rust` target
alongside the C++ one. It was not used because **cxx has no async story**, and
the loop is a chain of async mojo calls. Rust keeps the two jobs it is actually
there for: parsing untrusted model output, and deciding policy.

Worth revisiting when the Rust bindings mature, or if the loop grows logic that
would benefit from being in the safer language.

The model is currently **provided to** the kernel as a
`pending_remote<AgentModel>` rather than owned by it. Model routing belongs here
per this ADR and still will; until an in-process inference runtime exists there
is nothing to route, and the seam let the loop be built and tested against a
scripted model instead of waiting.
