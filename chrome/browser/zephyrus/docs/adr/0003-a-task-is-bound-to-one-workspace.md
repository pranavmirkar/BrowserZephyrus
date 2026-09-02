# A Task is bound to one Workspace

A Task may only observe and act on tabs in the Workspace it started in. It
cannot read another Workspace's tabs, and it cannot open a tab outside its own.

Workspaces already have separate cookie jars, backed by a StoragePartition each.
Binding a Task to one means an agent physically cannot act inside an identity the
user did not point it at: a Task started in a personal workspace has no path to a
work session, because it has no path to the tabs holding it. The isolation exists
already and this makes the agent inherit it rather than route around it.

The alternative was letting a Task roam the window so it could cross-reference
tabs from different Workspaces. That is a genuinely useful research capability
and we are giving it up on purpose.

## Consequences

Cross-workspace research is impossible by construction, not by policy. A user who
wants a Task to compare something in two Workspaces has to move the tabs, or run
two Tasks. This is the cost, and it is accepted.

Task scope and Workspace must not drift apart later. If a future feature wants a
Task to see "everything", that is a new decision superseding this one, not a
quiet relaxation of a check.

The audit log records the Workspace on every action, because "which identity did
this run as" is the first question anyone will ask of an agent action after the
fact.
