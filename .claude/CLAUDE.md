# pinsel: project instructions

Native image editor for generating images and local edits, built by hand down to its own GPU kernels.

## Who writes what

This is a learning project. The owner writes the core by hand; agents must not write it, even when asked to "just fix it". Explain, point at the bug, or review instead.

- **Owner only:** canvas and tiling, brush and mask, compositing and blend shaders, the inpaint crop-and-blend, the inference pipeline and every GPU kernel.
- **Agents may write:** UI boilerplate, file loading and saving, settings, build files, CI, tests and benchmarks.

When a task touches both, write the agent part and leave the owner part as a stub with a comment saying what it must do.

## Layout

- `src/`: the library (`project.hpp`, `project.cpp`) and the executable (`main.cpp`)
- `tests/`: GoogleTest, run by ctest
- `CMakePresets.json`: `dev` (Debug, AddressSanitizer and UndefinedBehaviorSanitizer, the default for every task) and `release`

## Commands

mise owns every command. `mise tasks` lists them.

```sh
mise run build          # configure and build the dev preset
mise run test           # build, then ctest under the sanitizers
mise run lint           # clang-format --dry-run, clang-tidy
mise run fmt            # clang-format -i
mise run check-commits  # cocogitto on unpushed commits
```

## Structure

- Build deep modules: a small interface over much hidden work. No pass-through wrappers, barrel files or `utils` grab bags.
- A module's public names live in one header. Other modules include that header and reach no deeper.
- Modern C++20: RAII, `std::unique_ptr` over `new`, no raw owning pointers. A sanitizer finding is a bug, never noise.
- `.clang-tidy` turns every finding into an error. Turn a check off there, with the reason, not with `NOLINT` in the code.
- At most about 7 entries per folder; above that, group by concept into a subfolder with one entry point. `scripts/check-fan-out` enforces it, with `.fan-out-baseline` for accepted overages and `.fan-out-excludes` for folders that are lists by design.
- No file passes 5000 tokens (bytes/4); `scripts/check-file-size` enforces it. A Claude Code hook in `.claude/settings.json` runs both checks on each edit. The hook needs `jq` on `PATH` or in mise's global config (`mise use -g jq`), since it reads no project's mise config.

## Comments and pull requests

- Names and structure explain the code. A comment says only what the code cannot: a limit from outside it (a spec, a hardware or library quirk) or a reason that would surprise a reader. No comment restates the code, narrates the change or tells its history.
- The pull request explains the change. Under "Changes" in its body, show each change that needs explaining as a short excerpt of the code, say why it looks that way, and link the docs, spec or issue it follows.

## Conventions

- Conventional Commits (`feat:`, `fix:`, `chore:` ...). The `commit-msg` hook rejects anything else.
- Default branch is `master`. With branch protection applied (`mise run repo:apply-settings`), changes land through PRs that pass Lint, Test, Commits, Review and Security.
- Review and Security are Claude reviews in CI. They follow `REVIEW.md`; answer every Important thread before the merge. A PR that touches a path in `.github/CODEOWNERS` also needs a code owner's review.
- Every `uses:` in a workflow names a full commit SHA, with the version as a comment. The repo refuses an action on a tag, and any action whose owner is not in `.github/selected-actions.json`. Dependabot updates the SHAs.
- Update from the Copier template with `uvx copier update`.
