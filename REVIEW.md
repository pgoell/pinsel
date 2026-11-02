# Review guide

The CI reviewer reads this file, `.claude/CLAUDE.md` and the pull request
before it reads the diff. Keep it short: every line costs a review.

## What counts as a failure here

- A user's image or layers lost or garbled: a save that writes a partial file, an undo that drops a tile.
- Work on the UI thread that blocks it: inference, file IO or a GPU sync in the render loop.
- GPU or host memory that grows per edit: a leak shows up only after many strokes, and 8 GB VRAM leaves no room.
- Undefined behaviour: anything a sanitizer would flag, even if the tests pass.
- Agent-written code in a part `.claude/CLAUDE.md` reserves for the owner.
- A doc that says something the code no longer does.

## Security model

A desktop app for one user on their own machine. No accounts and no secrets.

- Image files and model files (safetensors, GGUF, LoRAs) come from disk and are untrusted input: a parser must reject a bad size or offset, never read past the buffer. No pickle formats.
- The inference sidecar listens on localhost only, never on all interfaces.
- No network access at run time, except a model download the user starts.

## Design

Look at how the change is built as well as whether it works. The lints see one
function or one file; they miss these. Each point must name what would show
it, so the author can check it:

- A shallow module: a new file, function or component whose body only passes
  calls on. Name the caller it should fold into.
- One task spread thin: name the files this one change touched that always
  change together, and the one they should become.
- A list written out twice: name every copy of the list (kinds of page,
  routes, settings) that must now be kept the same by hand.
- Code in the wrong home: name the module the feature lives in and why the
  code belongs there.
- A file cut at a line count to fit the size limit: name the commit that cut it.
- A rule of `.claude/CLAUDE.md` broken. Quote the rule.
- An explanation in the wrong place: a comment that restates the code, narrates
  the change or tells its history (name the line, and say whether to delete it
  or move its reason to the PR body), or a change the code does not explain
  whose PR body gives no excerpt and reason (name the change).

Taste with no rule behind it is not a finding.

## What not to flag

- Formatting and the rules the linters in `mise run lint` already enforce.
- A missing test where the PR body names the test that covers the line.
- A design point about code the diff does not touch.

## How to report

Only an Important finding becomes a thread on its line, and every thread must be
answered before the merge. Everything else goes in the summary, where the
author reads it and nothing waits on it: an agent fixes whatever it is told, so a
blocking design point costs a commit and a CI run whether it mattered or not.

- **Important**: a bug this diff brings in, or a gap against the linked issue.
  A thread on the file and line, naming the class of bug so the author fixes
  every place it occurs. Important findings come first: never leave one out to
  make room for anything else.
- **Design**: a point from the list above, with the change you would make. At
  most 3, in the summary.
- **Pre-existing**: a bug the base branch already has. In the summary, as a
  line the author can turn into an issue.
- **Nit**: worth a look, safe to ignore. In the summary.

At most 10 findings in all. Every finding names a file and a line in the diff;
do not guess a file name. Never report again a finding an earlier round raised.
The summary gives the count per tag, or says "No issues found."
