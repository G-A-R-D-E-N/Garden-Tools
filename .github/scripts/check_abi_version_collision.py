#!/usr/bin/env python3
"""Fail a pull request whose RADICLE_ABI_VERSION another stack or the base also claims.

The rules, the cases behind them and the tests that pin them:
`docs/code-notes/GARDEN-TOOLS.md` at `.github/scripts/check_abi_version_collision.py:claim_conflicts`.
Requires: run from a checkout of the pull request, with the base branch reachable from
`origin` and `GITHUB_API_URL`, `GITHUB_REPOSITORY`, `GITHUB_TOKEN`, `GITHUB_BASE_REF`,
`PR_NUMBER` set, or the same values passed as options.
Exit codes: 0 clear, 1 a claim disagrees with another, 2 the gate could not run.
"""

import argparse
import base64
import binascii
import json
import os
import re
import subprocess
import sys
import urllib.error
import urllib.request

HEADER = "Radicle/include/fo4/c_api.h"
HEADER_GLOB = "Radicle/include/fo4/c_api*.h"
INTEGRATION_BRANCH = "nomad/dev"
VERSION = re.compile(r"^[ \t]*#[ \t]*define[ \t]+RADICLE_ABI_VERSION[ \t]+(\d+)[uUlL]*[ \t]*(?://[^\r\n]*)?\r?$", re.MULTILINE)
PAGE_SIZE = 50
MAX_PAGES = 10


def read_version(text, where):
    found = VERSION.search(text)
    if found is None:
        raise ValueError("{} does not define RADICLE_ABI_VERSION".format(where))
    return int(found.group(1))


def local_problems(version, base_version, changed_headers, parent_version=None):
    """Rules 1 and 3: the number moves forward, and an edited header says so (see code-notes)."""
    if version < base_version:
        return ["{} says {} but the base branch already says {}. A number may not move "
                "backwards; rebase and take one above {}.".format(
                    HEADER, version, base_version, base_version)]
    if changed_headers and version == base_version and parent_version != version:
        return ["this pull request edits {} but keeps the base branch's ABI {}. Bump "
                "RADICLE_ABI_VERSION, or a consumer built against the new header passes "
                "VerifyAbi against a library without the new exports.".format(
                    ", ".join(changed_headers), base_version)]
    return []


def _parent_edges(claims):
    """Direct descent evidence: this base is another claim's head, by branch or sha."""
    by_head_ref = {}
    by_head_sha = {}
    for claim in claims:
        if claim.get("head_ref"):
            by_head_ref[claim["head_ref"]] = claim["number"]
        if claim.get("head"):
            by_head_sha[claim["head"]] = claim["number"]
    edges = {}
    for claim in claims:
        stacked_on = by_head_ref.get(claim.get("base_ref") or "")
        if stacked_on is None:
            stacked_on = by_head_sha.get(claim.get("base") or "")
        if stacked_on is None or stacked_on == claim["number"]:
            continue
        edges.setdefault(claim["number"], set()).add(stacked_on)
    return edges


def _descendants(parents):
    """Transitive closure of the child -> parent map, as parent -> all below it."""
    children = {}
    for child, olds in parents.items():
        for old in olds:
            children.setdefault(old, set()).add(child)
    below = {}

    def walk(number):
        if number in below:
            return below[number]
        seen = below[number] = set()
        for child in children.get(number, ()):
            seen.add(child)
            seen.update(walk(child))
        return seen

    for number in children:
        walk(number)
    return below


def claim_conflicts(claims, base_version, ancestry=None, shared=None):
    """Rule 2: one entry per number that two different lineages both claim (see code-notes)."""
    parents = _parent_edges(claims)
    for child, olds in (ancestry or {}).items():
        parents.setdefault(child, set()).update(olds)
    below = _descendants(parents)
    by_version = {}
    for claim in claims:
        by_version.setdefault(claim["version"], []).append(claim)
    skipped = {base_version} if shared is None else {base_version, shared}
    conflicts = []
    for version in sorted(by_version):
        group = by_version[version]
        if version in skipped:
            continue
        numbers = [claim["number"] for claim in group]
        branched = any(
            first != second
            and second not in below.get(first, ())
            and first not in below.get(second, ())
            for index, first in enumerate(numbers)
            for second in numbers[index + 1:])
        if branched:
            conflicts.append((version, sorted(numbers)))
    return conflicts


def git(*arguments):
    return subprocess.run(("git",) + arguments, check=True, capture_output=True, text=True).stdout


def changed_headers_from(base_ref, head_ref):
    """Headers the head adds over the base: three dots exclude the base's own work (see code-notes)."""
    return [path for path in
            git("diff", "--name-only", base_ref + "..." + head_ref, "--",
                HEADER_GLOB).split("\n")
            if path]


def get_json(url, token):
    request = urllib.request.Request(url, headers={
        "Accept": "application/json",
        "Authorization": "token {}".format(token),
    })
    with urllib.request.urlopen(request, timeout=30) as response:
        return json.load(response)


def open_pulls(api, repo, token):
    """Every open pull request; an empty page ends the list and running out fails (see code-notes)."""
    pulls = []
    for page in range(1, MAX_PAGES + 1):
        batch = get_json("{}/repos/{}/pulls?state=open&limit={}&page={}".format(
            api, repo, PAGE_SIZE, page), token)
        if not isinstance(batch, list):
            raise ValueError("open pull request page {} is not a list".format(page))
        if any(not isinstance(item, dict) for item in batch):
            raise ValueError("open pull request page {} holds a non-object".format(page))
        pulls.extend(batch)
        if not batch:
            return pulls
    raise ValueError("open pull requests exceed {} pages; refusing to truncate".format(MAX_PAGES))


def claim_of(pull, api, repo, token):
    number = pull["number"]
    head = pull["head"]["sha"]
    contents = get_json("{}/repos/{}/contents/{}?ref={}".format(api, repo, HEADER, head), token)
    text = base64.b64decode(contents["content"]).decode("utf-8", "replace")
    return {
        "number": number,
        "head": head,
        "head_ref": pull["head"].get("ref") or "",
        "base": pull["base"].get("sha") or "",
        "base_ref": pull["base"].get("ref") or "",
        "version": read_version(text, "#{}".format(number)),
    }


def stack_root(claims, base):
    """Lowest ancestor branch above base through open-claim links, or None without a stack (see code-notes)."""
    seen = {base}
    root = base
    links = 0
    while root != INTEGRATION_BRANCH:
        owners = [claim for claim in claims if claim.get("head_ref") == root]
        if not owners:
            return root if links else None
        if len(owners) > 1:
            raise ValueError("more than one open pull request holds {}".format(root))
        root = owners[0].get("base_ref") or owners[0].get("base") or ""
        if not root:
            raise ValueError("a stack link above {} has no base".format(base))
        if root in seen:
            raise ValueError("stack links loop through {}".format(root))
        seen.add(root)
        links += 1
    return root if links else None


def read_version_at(api, repo, token, ref, where):
    """Number the header carries at a branch or sha; the caller exits 2 when it cannot be read (see code-notes)."""
    contents = get_json("{}/repos/{}/contents/{}?ref={}".format(api, repo, HEADER, ref), token)
    text = base64.b64decode(contents["content"]).decode("utf-8", "replace")
    return read_version(text, where)


def compare_counts(api, repo, token, old, new):
    """Commits in new that are not in old, from the Forgejo compare body (see code-notes)."""
    try:
        data = get_json("{}/repos/{}/compare/{}...{}".format(api, repo, old, new), token)
    except (OSError, ValueError, KeyError, TypeError) as error:
        print("::warning::cannot compare {} with {}: {}".format(old, new, error))
        return None
    try:
        total = data["total_commits"]
    except (KeyError, TypeError):
        total = None
    if total is None or not isinstance(total, int):
        print("::warning::compare of {} with {} hides total_commits; no ancestry call".format(
            old, new))
        return None
    return total


def compare_status(api, repo, token, old, new):
    """How new relates to old: ahead, behind, identical or diverged (see code-notes)."""
    forward = compare_counts(api, repo, token, old, new)
    backward = compare_counts(api, repo, token, new, old)
    if forward is None or backward is None:
        return None
    if forward == 0 and backward == 0:
        return "identical"
    if forward > 0 and backward == 0:
        return "ahead"
    if forward == 0 and backward > 0:
        return "behind"
    return "diverged"


def ancestry_of(claims, base_version, api, repo, token):
    """Child numbers to parent numbers for claims that share a number but no join (see code-notes)."""
    by_version = {}
    for claim in claims:
        by_version.setdefault(claim["version"], []).append(claim)
    ancestry = {}
    for version, group in by_version.items():
        if version == base_version or len(group) < 2:
            continue
        for index, first in enumerate(group):
            for second in group[index + 1:]:
                status = compare_status(api, repo, token, first["head"], second["head"])
                if status == "ahead":
                    ancestry.setdefault(second["number"], set()).add(first["number"])
                elif status == "behind":
                    ancestry.setdefault(first["number"], set()).add(second["number"])
                elif status == "identical":
                    ancestry.setdefault(first["number"], set()).add(second["number"])
                    ancestry.setdefault(second["number"], set()).add(first["number"])
    return ancestry


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--api-url", default=os.environ.get("GITHUB_API_URL", ""))
    parser.add_argument("--repo", default=os.environ.get("GITHUB_REPOSITORY", ""))
    parser.add_argument("--token", default=os.environ.get("GITHUB_TOKEN", ""))
    parser.add_argument("--base", default=os.environ.get("GITHUB_BASE_REF", ""))
    parser.add_argument("--number", type=int, default=int(os.environ.get("PR_NUMBER") or "0"))
    parser.add_argument("--local-only", action="store_true",
                        help="skip the open-pull-request half")
    arguments = parser.parse_args(argv)

    if not arguments.base:
        print("::error::GITHUB_BASE_REF is empty; this check only runs on a pull request")
        return 2

    try:
        version = read_version(git("show", "HEAD:" + HEADER), "HEAD:" + HEADER)
    except (subprocess.CalledProcessError, ValueError) as error:
        print("::error::cannot read the HEAD header {} ({}); the gate needs it, so this is exit 2, not a pass".format(
            HEADER, error))
        return 2
    try:
        git("fetch", "--no-tags", "origin", arguments.base)
        base_version = read_version(git("show", "FETCH_HEAD:" + HEADER), "the base branch")
        changed = changed_headers_from("FETCH_HEAD", "HEAD")
    except subprocess.CalledProcessError as error:
        print("::error::cannot fetch the base branch {} ({}); the gate needs it, so this is exit 2, not a pass".format(
            arguments.base, error))
        return 2
    except ValueError as error:
        print("::error::cannot read the base header ({}); the gate needs it, so this is exit 2, not a pass".format(
            error))
        return 2

    if arguments.local_only:
        problems = local_problems(version, base_version, changed)
        for problem in problems:
            print("::error::{}".format(problem))
        print("ABI: this pull request {}, base branch {}.".format(version, base_version))
        return 1 if problems else 0

    if not (arguments.api_url and arguments.repo and arguments.token):
        print("::error::the open-pull-request half needs GITHUB_API_URL, GITHUB_REPOSITORY "
              "and GITHUB_TOKEN, and the runner supplied fewer than three")
        return 2

    try:
        pulls = open_pulls(arguments.api_url, arguments.repo, arguments.token)
        claims = []
        for pull in pulls:
            try:
                claims.append(claim_of(pull, arguments.api_url, arguments.repo, arguments.token))
            except urllib.error.HTTPError as error:
                if error.code != 404:
                    print("::error::cannot read the ABI of #{} ({}); the gate needs every claim, so this is exit 2, not a pass".format(
                        pull.get("number"), error))
                    return 2
                print("::warning::cannot read the ABI of #{} ({}); skipping it".format(
                    pull.get("number"), error))
            except (KeyError, TypeError, json.JSONDecodeError, binascii.Error) as error:
                print("::error::malformed claim for #{} ({}); the gate needs every claim, so this is exit 2, not a pass".format(
                    pull.get("number"), error))
                return 2
            except ValueError as error:
                print("::warning::cannot read the ABI of #{} ({}); skipping it".format(
                    pull.get("number"), error))
    except (OSError, ValueError) as error:
        print("::error::cannot list the open pull requests ({}). This gate fails rather "
              "than passing silently.".format(error))
        return 2

    mine = next((claim for claim in claims if claim["number"] == arguments.number), None)
    if mine is None:
        claims.append({
            "number": arguments.number,
            "head": git("rev-parse", "HEAD").strip(),
            "head_ref": "",
            "base": git("rev-parse", "FETCH_HEAD").strip(),
            "base_ref": arguments.base,
            "version": version,
        })
    else:
        mine["version"] = version

    for claim in sorted(claims, key=lambda entry: entry["number"]):
        print("  #{} ABI {}".format(claim["number"], claim["version"]))

    parent_version = None
    try:
        root = stack_root(claims, arguments.base)
    except ValueError as error:
        print("::error::{}; the gate needs an unambiguous stack, so this is exit 2, not a pass".format(
            error))
        return 2
    if root is not None:
        try:
            floor = read_version_at(arguments.api_url, arguments.repo,
                                    arguments.token, root,
                                    "stack root {}".format(root))
        except (OSError, ValueError, KeyError, TypeError) as error:
            print("::error::cannot read the stack root {} ({}); the gate needs it, so this is exit 2, not a pass".format(
                root, error))
            return 2
        if version > floor:
            parent_version = version
    problems = local_problems(version, base_version, changed, parent_version)
    for problem in problems:
        print("::error::{}".format(problem))
    print("ABI: this pull request {}, base branch {}.".format(version, base_version))

    try:
        shared = read_version_at(arguments.api_url, arguments.repo, arguments.token,
                                 INTEGRATION_BRANCH,
                                 "integration branch {}".format(INTEGRATION_BRANCH))
    except (OSError, ValueError, KeyError, TypeError) as error:
        print("::error::cannot read the integration branch {} ({}); the gate needs it, so this is exit 2, not a pass".format(
            INTEGRATION_BRANCH, error))
        return 2
    conflicts = claim_conflicts(
        claims, base_version,
        ancestry_of(claims, base_version, arguments.api_url, arguments.repo,
                    arguments.token),
        shared)
    mine_conflicts = [(version, numbers) for version, numbers in conflicts
                      if arguments.number in numbers]
    for claimed, numbers in mine_conflicts:
        print("::error::ABI {} is claimed by more than one stack: {}. Take a number no "
              "other stack holds.".format(
                  claimed, ", ".join("#{}".format(number) for number in numbers)))
    return 1 if problems or mine_conflicts else 0


if __name__ == "__main__":
    sys.exit(main())
