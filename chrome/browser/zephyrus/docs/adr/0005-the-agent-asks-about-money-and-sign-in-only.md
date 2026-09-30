# The agent asks about money and sign-in, and about nothing the user asked for

The agent used to stop and ask before every action that "leaves the browser":
send, post, publish, delete, share, submit, forward. On real tasks that was most
of the steps that mattered, and users read it as an agent that could not be left
alone. The product decision is that a person who asks an agent to send a message
has asked for the message to be sent.

## What the kernel asks about now

- **Payments, always.** A click whose label begins with a payment word (buy,
  checkout, pay, order, purchase, donate, subscribe, book, reserve, transfer)
  asks, whatever the task said. The user sees the control named.
- **Sign-in and card entry, by hand-off.** The agent never types a password or
  a card number (unchanged: it is a refusal, not a question). The refusal now
  tells the model to call `task.handoff`, which pauses the task, shows the user
  one sentence ("Sign in to Gmail, then press Continue"), and resumes from the
  same place. Nothing runs on Continue; the user did the thing themselves.
- **An outward action nobody asked for.** Send, forward, post, publish, share,
  submit, delete, remove, unsubscribe, accept and the like ask **only when the
  task did not ask for them**. "Reply to Maya" authorises Send and Reply; it
  does not authorise Forward.
- **Unchanged:** a navigation that would carry data to a site nobody mentioned,
  an address the agent invented, and a choice between two equally good
  candidates.

## Why the third rule exists and the first two are not enough

Removing the confirmations outright makes prompt injection cost something it did
not before. Benchmark mt-012 puts an instruction inside an email that says to
forward it, and a model that was told to reply follows it. With the old rule the
run was blocked; with the rules deleted it was trapped, and the benchmark said
so. The model is not the boundary, and neither is the verb -- **what the user
said** is. The task text is the one thing on the page-to-model path that a page
cannot write, so an action is authorised by matching it to the task.

The matching is by stems and is generous on purpose: a false "allowed" needs the
task to already talk about that kind of action, and a false "ask" costs one
card. Each verb has its own list; asking to send does not authorise forwarding.

## Searches

A search for the user's own words on a site they did not name (most tasks that
begin "find" or "compare") no longer asks. The query must use search-named
parameters, over https, and every word must come from the task, plus at most two
the agent added. Data the agent READ is not in the task, which is what the
exfiltration rule exists to catch. A short search on a known engine (Google,
Bing, DuckDuckGo, Brave, Scholar, arXiv, YouTube, English Wikipedia) never asks.

## What this gives up

- A page that persuades the model to do something the task already covers (a
  reply that goes to a different recipient than the user meant) is not caught
  by a card. Recipient checks are the next thing to build here.
- A payment control worded without a payment word is not caught. The agent
  cannot type card details, so the last step of a payment is the user's anyway.
