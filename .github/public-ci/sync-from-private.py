#!/usr/bin/env python3
"""Export private pull requests and branches to the public CI repository and post their checks.

Usage: sync-from-private.py [--dry-run]   (run from a checkout of the private integration branch)
"""
import argparse
import base64
import io
import json
import os
import re
import shutil
import subprocess
import sys
import tarfile
import tempfile
from collections import namedtuple
from contextlib import contextmanager
from fnmatch import fnmatchcase
from pathlib import Path, PurePosixPath
from urllib.parse import parse_qs, urlsplit

ROOT = Path(__file__).resolve().parents[2]
PRIVATE = "G-A-R-D-E-N/GardenTools"
PUBLIC = "G-A-R-D-E-N/Garden-Tools"
PRIVATE_URL = f"https://github.com/{PRIVATE}.git"
PUBLIC_URL = f"https://github.com/{PUBLIC}.git"
EXPORT = ROOT / ".github" / "public-ci" / "export-public-ci.py"
WORKFLOWS = ".github/public-ci/workflows"
ABI_CHECK = ".github/scripts/check_abi_version_collision.py"
BUNDLED_TOOLS = ".github/scripts/test_bundled_tools_manifest.py"
BUNDLED_TOOLS_TREES = ("PluginEditTool", ".forgejo")
ABI_PATHS = ("Radicle/include/fo4/*", "Radicle/bindings/csharp/*",
             "Radicle/tests/test_c_api_quest.cpp", ABI_CHECK)
BRANCHES = ("nomad/dev",)
EXPORT_CAP = 5
ABI_HISTORY = 200
EXPORT_CONTEXT = "public-ci/export"
ABI_CONTEXT = "public-ci/abi-version"
PRIVATE_CONTEXT = "public-ci/private-only-checks"
CONFLICT_TEXT = "conflicts with the base: no merge commit"
EXPORT_FAILED_TEXT = "export failed; see the public sync run"
AFTER_PUSH_TEXT = "post-push step failed; see the public sync run"
FINISHED = ("success", "failure", "error")
IDENTITY = {
    "GIT_AUTHOR_NAME": "NomadsReach",
    "GIT_AUTHOR_EMAIL": "144523850+NomadsReach@users.noreply.github.com",
    "GIT_COMMITTER_NAME": "NomadsReach",
    "GIT_COMMITTER_EMAIL": "144523850+NomadsReach@users.noreply.github.com",
}
TOKEN_KEYS = ("TOKEN", "GH_TOKEN", "GITHUB_TOKEN", "GIT_CONFIG_COUNT", "GIT_CONFIG_KEY_0",
              "GIT_CONFIG_VALUE_0")
PULL_LINE = r'.[] | "\(.number) \(.head.sha) \(.base.sha) \(.base.ref)"'
REF_LINE = r'.[] | "\(.ref) \(.object.sha)"'
SOURCE = re.compile(r"^Source-Commit: ([0-9a-f]{40})$", re.MULTILINE)
PR_BRANCH = re.compile(r"ci/pr-(\d+)")
Target = namedtuple("Target", "branch ref head number base_sha base_ref")


class Failed(Exception):
    pass


class PushFailed(Failed):
    pass


class AfterPush(Failed):
    pass


def source_commit(message):
    found = SOURCE.search(message)
    return found.group(1) if found else None


def export_message(branch, head):
    return f"Garden Tools {branch}\n\nSource-Commit: {head}"


def parse_pulls(text):
    return [(int(number), head, base_sha, base_ref)
            for number, head, base_sha, base_ref in (line.split(" ") for line in text.splitlines())]


def parse_refs(text):
    return {ref.removeprefix("refs/heads/"): sha
            for ref, sha in (line.split(" ") for line in text.splitlines())}


def json_items(text):
    decoder, items, index = json.JSONDecoder(), [], 0
    while index < len(text):
        value, index = decoder.raw_decode(text, index)
        items.extend(value)
        index = len(text) - len(text[index:].lstrip())
    return items


def plan_targets(pulls, heads):
    targets = [Target(f"ci/{branch}", f"refs/heads/{branch}", heads[branch], None, None, None)
               for branch in BRANCHES]
    for number, head, base_sha, base_ref in sorted(pulls):
        targets.append(Target(f"ci/pr-{number}", f"refs/pull/{number}/merge", head, number,
                              base_sha, base_ref))
    return targets


def recorded(states):
    if states.get(EXPORT_CONTEXT, {}).get("state") in ("success", "error"):
        return True
    return any(status["state"] in FINISHED for context, status in states.items()
               if context.startswith("public-ci/") and context != EXPORT_CONTEXT)


def mergeable_at(status):
    return parse_qs(urlsplit(status.get("target_url", "")).query).get("mergeable", [""])[0]


def holds(env, target, status):
    if status.get("state") != "failure":
        return False
    if status.get("description") == CONFLICT_TEXT:
        return mergeable(env, target.number) == "CONFLICTING"
    if status.get("description") == EXPORT_FAILED_TEXT:
        return mergeable_at(status) == mergeable(env, target.number)
    return False


def is_due(target, source, states, held):
    if target.head in held:
        return False
    if source is None:
        return not recorded(states)
    return source != target.head


def oldest_first(due, dates):
    return sorted(due, key=lambda target: (target.number is not None, dates[target.head]))


def stale_pr_branches(branches, open_numbers):
    stale = []
    for branch in branches:
        found = PR_BRANCH.fullmatch(branch)
        if found and int(found.group(1)) not in open_numbers:
            stale.append(branch)
    return sorted(stale)


def merge_is_current(target, merge, parents):
    if target.number is None:
        return merge == target.head
    return merge == target.head or bool(parents) and parents[-1] == target.head


def touches_abi(paths):
    return any(fnmatchcase(path, pattern) for path in paths for pattern in ABI_PATHS)


def status_contexts(workflows):
    return sorted(Path(name).stem for name, text in workflows.items()
                  if 'context="public-ci/' in text)


def verdict(name, code):
    if code == 0:
        return "success", f"{name} passed"
    return "failure", f"{name} failed"


def first_error(output):
    for line in output.splitlines():
        if re.search(r"fail|error", line, re.IGNORECASE):
            return re.sub(r"\S*[/\\.]\S*", "<redacted>", line.strip())[:160]
    return ""


def reason(error):
    return str(error) if isinstance(error, Failed) else type(error).__name__


def git_auth(token):
    basic = base64.b64encode(f"x-access-token:{token}".encode()).decode()
    return {"GIT_CONFIG_COUNT": "1", "GIT_CONFIG_KEY_0": "http.https://github.com/.extraheader",
            "GIT_CONFIG_VALUE_0": f"AUTHORIZATION: basic {basic}"}


def check_env(env):
    return {key: value for key, value in env.items() if key not in TOKEN_KEYS}


def base_env(token):
    env = check_env(dict(os.environ, **IDENTITY, GIT_TERMINAL_PROMPT="0"))
    if token:
        env.update(GH_TOKEN=token, **git_auth(token))
    return env


def abi_env(env, token, target):
    return dict(check_env(env), **git_auth(token), GITHUB_API_URL="https://api.github.com",
                GITHUB_REPOSITORY=PRIVATE, GITHUB_TOKEN=token, GITHUB_BASE_REF=target.base_ref,
                PR_NUMBER=str(target.number))


def run(step, argv, env):
    try:
        return subprocess.run(argv, cwd=ROOT, env=env, check=True, capture_output=True,
                              encoding="utf-8").stdout
    except (OSError, subprocess.CalledProcessError):
        raise Failed(f"{step} failed") from None


def archive(step, commit, paths, env):
    try:
        return subprocess.run(["git", "archive", "--format=tar", commit, "--", *paths], cwd=ROOT,
                              env=env, check=True, capture_output=True).stdout
    except (OSError, subprocess.CalledProcessError):
        raise Failed(f"{step} failed") from None


def gh(step, env, *args):
    return run(step, ["gh", "api", *args], env)


def check(argv, cwd, env):
    done = subprocess.run(argv, cwd=cwd, env=env, capture_output=True, encoding="utf-8",
                          errors="replace")
    return done.returncode, done.stdout + done.stderr


@contextmanager
def worktree(env, commit):
    with tempfile.TemporaryDirectory() as scratch:
        path = os.path.join(scratch, "tree")
        run("worktree add", ["git", "-c", "core.symlinks=false", "worktree", "add", "--detach",
                             "--quiet", path, commit], env)
        try:
            yield path
        finally:
            run("worktree remove", ["git", "worktree", "remove", "--force", path], env)


@contextmanager
def scratch_tree(env, commit):
    with tempfile.TemporaryDirectory() as scratch:
        tree = Path(scratch)
        data = archive("read merge tree", commit, BUNDLED_TOOLS_TREES, env)
        with tarfile.open(fileobj=io.BytesIO(data)) as tar:
            for member in tar.getmembers():
                if not member.isreg():
                    continue
                parts = PurePosixPath(member.name).parts
                if not parts or ".." in parts or member.name.startswith("/"):
                    raise Failed("read merge tree failed")
                target = tree.joinpath(*parts)
                target.parent.mkdir(parents=True, exist_ok=True)
                target.write_bytes(tar.extractfile(member).read())
        test = tree / BUNDLED_TOOLS
        test.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(ROOT / BUNDLED_TOOLS, test)
        yield tree


def open_pulls(env):
    return parse_pulls(gh("list open pull requests", env,
                          f"repos/{PRIVATE}/pulls?state=open&per_page=100",
                          "--paginate", "--jq", PULL_LINE))


def branch_head(env, branch):
    return gh("read branch head", env, f"repos/{PRIVATE}/git/ref/heads/{branch}",
              "--jq", ".object.sha").strip()


def public_branches(env):
    return parse_refs(gh("list public ci branches", env,
                         f"repos/{PUBLIC}/git/matching-refs/heads/ci/", "--paginate",
                         "--jq", REF_LINE))


def public_source(env, sha):
    return source_commit(gh("read public commit", env, f"repos/{PUBLIC}/git/commits/{sha}",
                            "--jq", ".message"))


def status_states(env, sha):
    statuses = json_items(gh("read commit statuses", env,
                             f"repos/{PRIVATE}/commits/{sha}/statuses", "--paginate"))
    latest = {}
    for status in sorted(statuses, key=lambda status: int(status["id"]), reverse=True):
        latest.setdefault(status["context"], {
            "state": status["state"],
            "description": status.get("description") or "",
            "target_url": status.get("target_url") or "",
        })
    return latest


def mergeable(env, number):
    owner, name = PRIVATE.split("/")
    query = (f'query($number: Int!) {{ repository(owner: "{owner}", name: "{name}") '
             f'{{ pullRequest(number: $number) {{ mergeable }} }} }}')
    return gh("read pull request mergeability", env, "graphql", "-F", f"number={number}",
              "-f", f"query={query}", "--jq", ".data.repository.pullRequest.mergeable").strip()


def head_date(env, sha):
    return gh("read head commit date", env, f"repos/{PRIVATE}/git/commits/{sha}",
              "--jq", ".committer.date").strip()


def workflow_texts(env, commit):
    paths = run("list workflows", ["git", "ls-tree", "-r", "--name-only", commit, "--",
                                   WORKFLOWS], env).splitlines()
    return {Path(path).name: run("read workflow", ["git", "show", f"{commit}:{path}"], env)
            for path in paths if path.endswith(".yml")}


def post(env, sha, context, state, description, url):
    gh("post status", env, f"repos/{PRIVATE}/statuses/{sha}", "-f", f"state={state}",
       "-f", f"context={context}", "-f", f"target_url={url}", "-f", f"description={description}")


def report_abi(env, label, head, code, url):
    state, text = verdict("ABI number check", code)
    post(env, head, ABI_CONTEXT, state, text, url)
    print(f"{label} {ABI_CONTEXT} {state}" + ("" if code == 0 else f": exit {code}"))


def report_private(env, label, head, code, output, url):
    state, text = verdict("bundled tools check", code)
    post(env, head, PRIVATE_CONTEXT, state, text, url)
    detail = "" if code == 0 else ": " + (first_error(output) or "exit " + str(code))
    print(f"{label} {PRIVATE_CONTEXT} {state}{detail}")


def no_merge_commit(env, target, url):
    if target.number is None or mergeable(env, target.number) != "CONFLICTING":
        print(f"{target.branch} has no usable merge commit yet; the next tick retries")
        return False
    post(env, target.head, EXPORT_CONTEXT, "failure", CONFLICT_TEXT, url)
    print(f"{target.branch} {EXPORT_CONTEXT} failure: conflicts with the base")
    return False


def export_failed(env, target, url):
    try:
        now = mergeable(env, target.number)
        current = status_states(env, target.head).get(EXPORT_CONTEXT, {})
        if (current.get("state") == "failure" and current.get("description") == EXPORT_FAILED_TEXT
                and mergeable_at(current) == now):
            return
        post(env, target.head, EXPORT_CONTEXT, "failure", EXPORT_FAILED_TEXT, f"{url}?mergeable={now}")
        print(f"{target.branch} {EXPORT_CONTEXT} failure: export failed")
    except Exception as error:
        print(f"{target.branch} could not post {EXPORT_CONTEXT} failure: {reason(error)}")


def publish(env, target, token, url):
    branch, head = target.branch, target.head
    local = check_env(env)
    actions = f"https://github.com/{PUBLIC}/actions?query=branch%3A{branch}"
    if target.number is not None and not run(
            "list merge ref", ["git", "ls-remote", PRIVATE_URL, target.ref], env).strip():
        return no_merge_commit(env, target, url)
    depth = 2 if target.number is not None else 1
    run("fetch", ["git", "fetch", "--quiet", "--no-tags", f"--depth={depth}", PRIVATE_URL,
                  target.ref], env)
    merge = run("resolve fetched ref", ["git", "rev-parse", "--verify", "FETCH_HEAD^{commit}"],
                local).strip()
    parents = run("read parents", ["git", "log", "-1", "--format=%P", merge], local).split()
    if not merge_is_current(target, merge, parents):
        return no_merge_commit(env, target, url)
    abi = False
    if target.number is not None:
        changed = run("list changed paths", ["git", "diff", "--name-only", "--no-renames",
                                             f"{merge}^1", merge], local).splitlines()
        abi = merge == head or touches_abi(changed)
    if abi:
        fetch = ["git", "fetch", "--quiet", "--no-tags"]
        try:
            run("deepen history", fetch + [f"--deepen={ABI_HISTORY}", PRIVATE_URL, target.ref], env)
        except Failed:
            run("unshallow history", fetch + ["--unshallow", PRIVATE_URL, target.ref], env)
    commit = run("export", [sys.executable, str(EXPORT), merge, export_message(branch, head)],
                 local).strip()
    post(env, head, EXPORT_CONTEXT, "pending", "Exporting to public CI", actions)
    for name in status_contexts(workflow_texts(local, merge)):
        post(env, head, f"public-ci/{name}", "pending", "Building on GitHub-hosted runners", actions)
    try:
        run("push", ["git", "push", "--quiet", "--force", PUBLIC_URL,
                     f"{commit}:refs/heads/{branch}"], env)
    except Failed:
        raise PushFailed("push failed") from None
    posted = set()
    contexts = [EXPORT_CONTEXT] + ([ABI_CONTEXT] if abi else []) + [PRIVATE_CONTEXT]
    try:
        post(env, head, EXPORT_CONTEXT, "success", "Exported to public CI", actions)
        posted.add(EXPORT_CONTEXT)
        if abi:
            with worktree(local, head) as tree:
                code, _ = check([sys.executable, "-I", str(ROOT / ABI_CHECK)], tree,
                                abi_env(env, token, target))
            report_abi(env, branch, head, code, url)
            posted.add(ABI_CONTEXT)
        with scratch_tree(local, merge) as tree:
            code, output = check([sys.executable, "-I", "-B", str(tree / BUNDLED_TOOLS)], tree,
                                 local)
        report_private(env, branch, head, code, output, url)
        posted.add(PRIVATE_CONTEXT)
    except Exception as error:
        for context in contexts:
            if context not in posted:
                try:
                    post(env, head, context, "error", AFTER_PUSH_TEXT, url)
                except Exception:
                    print(f"{branch} {context} could not be posted as error")
        raise AfterPush(AFTER_PUSH_TEXT) from error
    print(f"{branch} {head[:8]} exported as {commit[:8]}")
    return True


def sync(dry_run):
    token = os.environ.get("TOKEN", "")
    url = ""
    if not dry_run:
        if not token:
            raise Failed("TOKEN is not set")
        url = (f"{os.environ['GITHUB_SERVER_URL']}/{os.environ['GITHUB_REPOSITORY']}"
               f"/actions/runs/{os.environ['GITHUB_RUN_ID']}")
    env = base_env(token)
    pulls = open_pulls(env)
    heads = {branch: branch_head(env, branch) for branch in BRANCHES}
    targets = plan_targets(pulls, heads)
    branches = public_branches(env)
    sources = {target.branch: public_source(env, branches[target.branch])
               for target in targets if target.branch in branches}
    states = {target.head: status_states(env, target.head) for target in targets
              if sources.get(target.branch) != target.head}
    held = {target.head for target in targets
            if target.number is not None
            and target.head in states
            and EXPORT_CONTEXT in states[target.head]
            and holds(env, target, states[target.head][EXPORT_CONTEXT])}
    due = [target for target in targets
           if is_due(target, sources.get(target.branch), states.get(target.head, {}), held)]
    ordered = oldest_first(due, {target.head: head_date(env, target.head) for target in due})
    stale = stale_pr_branches(branches, {number for number, *_ in pulls})
    if dry_run:
        planned = ordered[:EXPORT_CAP]
        for target in targets:
            if target in planned:
                action = "export"
            elif target in ordered:
                action = "queued"
            elif target.head in held:
                action = "handled"
            else:
                action = "current"
            print(f"{target.branch} {action}")
        for branch in stale:
            print(f"{branch} delete")
        return 0
    failures = exported = attempted = 0
    for target in ordered:
        if exported == EXPORT_CAP:
            break
        attempted += 1
        try:
            if publish(env, target, token, url):
                exported += 1
        except (PushFailed, AfterPush) as error:
            failures += 1
            print(f"{target.branch} failed: {reason(error)}")
        except Exception as error:
            failures += 1
            print(f"{target.branch} failed: {reason(error)}")
            if target.number is not None:
                export_failed(env, target, url)
    for branch in stale:
        try:
            run("delete branch", ["git", "push", "--quiet", PUBLIC_URL, "--delete", branch], env)
            print(f"deleted {branch}")
        except Exception as error:
            failures += 1
            print(f"{branch} failed: {reason(error)}")
    print(f"exported {exported}, attempted {attempted}, queued {len(ordered) - attempted}, "
          f"failed {failures}")
    return 1 if failures else 0


def main(argv=None):
    parser = argparse.ArgumentParser(description="Export private pull requests to public CI.")
    parser.add_argument("--dry-run", action="store_true",
                        help="print the planned action per target and write nothing")
    args = parser.parse_args(argv)
    try:
        return sync(args.dry_run)
    except Exception as error:
        print(f"::error::{reason(error)}")
        return 1


if __name__ == "__main__":
    sys.exit(main())
