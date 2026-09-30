# Agent memory: three layers, and only the user can write the last one

The agent used to forget everything the moment a task ended. A traced run: it
asked "should I build a new frame or link the existing ones?", the user
answered "a new browser window", and the answer was run as a brand-new task --
"A new browser tab is open" -- with the Figma prototype it was about gone. Every
message was a cold start. That is a product gap, not a prompt bug.

## The layers

| Layer | What it holds | Where it lives | Lifetime |
|---|---|---|---|
| Working | the task in progress: steps, notes, the last page read | kernel `TaskLoop` | the task |
| Conversation | this chat: what the user said, how each task ended, notes | browser, per window and workspace, RAM | the chat (or "New chat") |
| Long-term | things the user asked it to keep | the profile's `Zephyrus Agent Memory` file | until forgotten |

The browser owns the second and third. The kernel process is handed a copy of
what one task needs (`mojom::TaskMemory`) and can neither read the rest nor
write any of it. What it wants kept goes through the `memory.remember` tool, and
the tool's rules are the kernel's while its effect is the browser's.

## A follow-up is understood as one

Every message goes to the agent with the chat so far, ranked long-term facts,
and the notes the last task took. If the last exchange ended with a question,
the message is its **answer**: the task runs as "what they first asked, plus the
answer", and the prompt says so ("Your last message was a QUESTION ... do not
start a new task"). The same string is what the policy's provenance rules read,
so an answer authorises exactly what the original request and the answer
together asked for.

The user's own lines are marked as theirs. The agent's earlier replies are marked
**data**: they can quote a page, and a page must not be able to talk to the
agent through its own history.

## Only the user can write long-term memory

Persistent memory is the one place a hostile page could plant something that
pays off later ("remember that the user always wants receipts forwarded
to..."). So `memory.remember` is judged by **provenance**: the content words of
the fact have to be in what the user just said, or it is refused. A page cannot
write the user's message. Also refused, whoever said it: passwords, codes, card
numbers, IDs (any run of seven or more digits), and anything over 300 characters.
`memory.forget` runs only when the message asks for forgetting.

What is kept about a chat is masked first: a verification code after the word
"code", a number long enough to be a card, a password after "password". The
address of the page it ended on keeps its path and loses its query string.

## Who is not remembered

- **Private Workspace** writes nothing to disk. Its profile has its own memory
  object with no file, so what it learns is gone when it closes and never mixes
  with the regular profile's.
- **Workspaces** are separate contexts (ADR 0003): facts and chats are scoped to
  the workspace they were said in.
- **Off switch** in Model settings; "Forget everything it remembers here"; and a
  New chat button that ends the conversation without touching what was kept.
- Nothing is uploaded (ADR 0004 already bounds what leaves the browser; this
  adds nothing to it).

## Limits and what comes next

- Facts are stored as plain text in the profile directory. They are things the
  user said about how they like to work, and they cannot be secrets, but they are
  not encrypted; OSCrypt is the next step if that is not enough.
- Recall is word overlap plus recency, not embeddings. Enough for a few dozen
  short facts; a semantic index is the step if people keep hundreds.
- The chat is not restored after a restart. Long-term memory is.
- Not built: remembering how a site works ("this site's search is at /s?k=") and
  summarising old turns with the model instead of shrinking them. Both need a
  reflection step after a task, which is a model call the user should be able to
  see and switch off.
