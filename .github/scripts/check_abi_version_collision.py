#!/usr/bin/env python3
"""Fail a pull request whose RADICLE_ABI_VERSION another stack or the base also claims.

The rules, the cases behind them and the tests that pin them:
`docs/code-notes/GARDEN-TOOLS.md` at `.github/scripts/check_abi_version_collision.py:claim_conflicts`.
Requires: run from a checkout of the pull request, with the base branch reachable from
`origin` and `GITHUB_API_URL`, `GITHUB_REPOSITORY`, `GITHUB_TOKEN`, `GITHUB_BASE_REF`,
`PR_NUMBER` set, or the same values passed as options.
Exit codes: 0 clear, 1 a claim disagrees with another, 2 the cross-pull-request half
could not run.
"""

import argparse
import base64
import json
import os
import re
import subprocess
import sys
import urllib.error
import urllib.request

HEADER = "Radicle/include/fo4/c_api.h"
HEADER_GLOB = "Radicle/include/fo4/c_api*.h"
VERSION = re.compile(r"^[ \t]*#[ \t]*define[ \t]+RADICLE_ABI_VERSION[ \t]+(\d+)u?[ \t]*$", re.MULTILINE)
PAGE_SIZE = 50
MAX_PAGES = 10


def read_version(text, where):
    found = VERSION.search(text)
    if found is None:
        raise ValueError("{} does not define RADICLE_ABI_VERSION".format(where))
    return int(found.group(1))


def local_problems(version, base_version, changed_headers):
    """Rules 1 and 3: the number has to move forward, and an edited header has to say so."""
    if version < base_version:
        return ["{} says {} but the base branch already says {}. A number may not move "
                "backwards; rebase and take one above {}.".format(
                    HEADER, version, base_version, base_version)]
    if changed_headers and version == base_version:
        return ["this pull request edits {} but keeps the base branch's ABI {}. Bump "
                "RADICLE_ABI_VERSION, or a consumer built against the new header passes "
                "VerifyAbi against a library without the new exports.".format(
                    ", ".join(changed_headers), base_version)]
    return []


def stack_roots(claims):
    """Union the claims whose base is another claim's head, so a stack is one claim."""
    parent = {claim["number"]: claim["number"] for claim in claims}

    def root(number):
        while parent[number] != number:
            parent[number] = parent[parent[number]]
            number = parent[number]
        return number

    by_head_ref = {}
    by_head_sha = {}
    for claim in claims:
        if claim.get("head_ref"):
            by_head_ref[claim["head_ref"]] = claim["number"]
        if claim.get("head"):
            by_head_sha[claim["head"]] = claim["number"]

    for claim in claims:
        stacked_on = by_head_ref.get(claim.get("base_ref") or "")
        if stacked_on is None:
            stacked_on = by_head_sha.get(claim.get("base") or "")
        if stacked_on is None or stacked_on == claim["number"]:
            continue
        mine, theirs = root(claim["number"]), root(stacked_on)
        if mine != theirs:
            parent[theirs] = mine
    return {claim["number"]: root(claim["number"]) for claim in claims}


def claim_conflicts(claims, base_version):
    """Rule 2: one entry per number that two different stacks both claim."""
    roots = stack_roots(claims)
    by_version = {}
    for claim in claims:
        by_version.setdefault(claim["version"], []).append(claim)
    conflicts = []
    for version in sorted(by_version):
        group = by_version[version]
        if version == base_version:
            continue
        if len({roots[claim["number"]] for claim in group}) > 1:
            conflicts.append((version, sorted(claim["number"] for claim in group)))
    return conflicts


def git(*arguments):
    return subprocess.run(("git",) + arguments, check=True, capture_output=True, text=True).stdout


def get_json(url, token):
    request = urllib.request.Request(url, headers={
        "Accept": "application/json",
        "Authorization": "token {}".format(token),
    })
    with urllib.request.urlopen(request, timeout=30) as response:
        return json.load(response)


def open_pulls(api, repo, token):
    pulls = []
    for page in range(1, MAX_PAGES + 1):
        batch = get_json("{}/repos/{}/pulls?state=open&limit={}&page={}".format(
            api, repo, PAGE_SIZE, page), token)
        pulls.extend(batch)
        if len(batch) < PAGE_SIZE:
            break
    return pulls


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

    version = read_version(git("show", "HEAD:" + HEADER), "HEAD:" + HEADER)
    git("fetch", "--no-tags", "--depth=1", "origin", arguments.base)
    base_version = read_version(git("show", "FETCH_HEAD:" + HEADER), "the base branch")
    changed = [path for path in
               git("diff", "--name-only", "FETCH_HEAD", "HEAD", "--", HEADER_GLOB).split("\n")
               if path]

    problems = local_problems(version, base_version, changed)
    for problem in problems:
        print("::error::{}".format(problem))
    print("ABI: this pull request {}, base branch {}.".format(version, base_version))

    if arguments.local_only:
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
            except (urllib.error.HTTPError, KeyError, TypeError, ValueError) as error:
                print("::warning::cannot read the ABI of #{}: {}".format(
                    pull.get("number"), error))
    except (urllib.error.URLError, urllib.error.HTTPError, ValueError) as error:
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

    conflicts = claim_conflicts(claims, base_version)
    for claimed, numbers in conflicts:
        print("::error::ABI {} is claimed by more than one stack: {}. Take a number no "
              "other stack holds.".format(
                  claimed, ", ".join("#{}".format(number) for number in numbers)))
    return 1 if problems or conflicts else 0


if __name__ == "__main__":
    sys.exit(main())
