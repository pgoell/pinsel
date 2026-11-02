"""How the CI review's findings were answered, for pull requests merged lately.

Counts each Important thread by the first word of the author's answer (Fixed,
Rejected, Intentional, Didn't matter), the rounds each pull request took, and
picks pull requests whose summaries held Design points for a person to read. An
agent author fixes whatever it is told, so the answers alone do not show a
finding was worth making; the sample is where a person judges that.

    python3 .github/review/outcomes.py [days] [sample size]
"""

import json
import random
import re
import subprocess
import sys
from collections import Counter
from datetime import UTC, datetime, timedelta

BOT = "github-actions"
DAYS = 7
SAMPLE = 5
ANSWERS = ("fixed", "rejected", "intentional", "didn't matter")

QUERY = """
query($owner: String!, $name: String!, $n: Int!) {
  repository(owner: $owner, name: $name) {
    pullRequest(number: $n) {
      reviewThreads(first: 100) { nodes { comments(first: 20) {
        nodes { author { login } body } } } }
      comments(first: 100) { nodes { author { login } body } }
    }
  }
}
"""


def gh(*args: str) -> str:
    """Run gh with these arguments and return what it prints."""
    run = subprocess.run(["gh", *args], check=True, capture_output=True, text=True)
    return run.stdout


def answer(replies: list[dict]) -> str:
    """Name the author's answer by its first word, or say there is none."""
    for reply in replies:
        if reply["author"]["login"] != BOT:
            first = reply["body"].strip().lower()
            return next((a for a in ANSWERS if first.startswith(a)), "other")
    return "unanswered"


def main() -> None:
    """Print the counts and the sample."""
    given = [int(a) for a in sys.argv[1:3]]
    days, size = given + [DAYS, SAMPLE][len(given) :]
    since = (datetime.now(UTC) - timedelta(days=days)).date().isoformat()
    repo = gh("repo", "view", "--json", "nameWithOwner", "--jq", ".nameWithOwner").strip()
    prs = json.loads(
        gh(
            "pr",
            "list",
            "-R",
            repo,
            "--state",
            "merged",
            "--limit",
            "500",
            "--search",
            f"merged:>={since}",
            "--json",
            "number",
        )
    )
    answers: Counter[str] = Counter()
    rounds: Counter[int] = Counter()
    with_design = []
    for pr in prs:
        owner, name = repo.split("/")
        data = json.loads(
            gh(
                "api",
                "graphql",
                "-f",
                f"query={QUERY}",
                "-F",
                f"owner={owner}",
                "-F",
                f"name={name}",
                "-F",
                f"n={pr['number']}",
            )
        )
        node = data["data"]["repository"]["pullRequest"]
        for thread in node["reviewThreads"]["nodes"]:
            first, *replies = thread["comments"]["nodes"]
            important = first["body"].lstrip("* ").startswith("Important")
            if first["author"]["login"] == BOT and important:
                answers[answer(replies)] += 1
        bodies = [c["body"] for c in node["comments"]["nodes"] if c["author"]["login"] == BOT]
        marker = r"ci-review:review sha=\w+ round=(\d+)"
        marks = [int(m) for b in bodies for m in re.findall(marker, b)]
        if marks:
            rounds[max(marks)] += 1
        design = r"^- \*\*Design\*\*"
        if any(b.startswith("**Review**") and re.search(design, b, re.M) for b in bodies):
            with_design.append(pr["number"])

    print(f"{len(prs)} pull requests merged since {since}")
    print("Important threads by answer:", dict(answers) or "none")
    print("Pull requests by rounds:", dict(sorted(rounds.items())) or "none")
    pick = random.sample(with_design, min(size, len(with_design)))
    print(f"Read the Design points of {len(pick)} of {len(with_design)}:")
    for n in pick:
        print(f"  https://github.com/{repo}/pull/{n}")


if __name__ == "__main__":
    main()
