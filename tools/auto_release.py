#!/usr/bin/env python3
"""Auto-release helper

Usage: tools/auto_release.py <bin-path> [packed-path]

This script:
- reads `VERSION_STRING` from the Makefile
- computes a git tag name `vX.Y.Z` (lowercase v)
- creates the tag if missing and pushes it
- creates or updates a GitHub release with the specified assets using `gh`

The script is tolerant: if `gh` is not installed or git remote is not available
it will print a message and exit successfully (non-fatal for Makefile).
"""
import os
import re
import shlex
import subprocess
import sys


def run(cmd, check=False, capture=False):
    print("$", cmd)
    args = shlex.split(cmd)
    if capture:
        return subprocess.run(args, check=check, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    return subprocess.run(args, check=check)


def read_version_string(makefile_path="Makefile"):
    with open(makefile_path, "r") as f:
        text = f.read()
    m = re.search(r'^(VERSION_STRING\s*\?=\s*)(V[0-9]+\.[0-9]+\.[0-9]+)\s*$', text, flags=re.M)
    if not m:
        return None
    return m.group(2)


def tag_exists(tag):
    # local tag
    r = subprocess.run(["git", "tag", "-l", tag], stdout=subprocess.PIPE, text=True)
    return tag in r.stdout.splitlines()


def remote_tag_exists(tag):
    r = subprocess.run(["git", "ls-remote", "--tags", "origin", tag], stdout=subprocess.PIPE, text=True)
    return bool(r.stdout.strip())


def create_and_push_tag(tag, msg=None):
    if tag_exists(tag):
        print(f"Tag {tag} already exists locally")
    else:
        msg = msg or f"Release {tag}"
        run(f"git tag -a {tag} -m \"{msg}\"", check=True)
    # push tag
    if remote_tag_exists(tag):
        print(f"Tag {tag} already exists on remote")
    else:
        run(f"git push origin {tag}", check=True)


def gh_installed():
    return subprocess.run(["which", "gh"], stdout=subprocess.DEVNULL).returncode == 0


def create_or_update_release(tag, title, body, assets):
    # check release existence
    r = subprocess.run(["gh", "release", "view", tag], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    if r.returncode == 0:
        # release exists, upload assets (clobber)
        for a in assets:
            run(f"gh release upload {tag} {shlex.quote(a)} --clobber", check=False)
    else:
        args = ["gh", "release", "create", tag]
        args += assets
        args += ["-t", title, "-n", body]
        print("$", " ".join(shlex.quote(x) for x in args))
        subprocess.run(args)


def main():
    if len(sys.argv) < 2:
        print("Usage: auto_release.py <bin-path> [packed-path]")
        return 0

    bin_path = sys.argv[1]
    packed = None
    if len(sys.argv) >= 3:
        packed = sys.argv[2]

    version = read_version_string()
    if not version:
        print("Could not read VERSION_STRING from Makefile")
        return 0

    tag = "v" + version[1:]
    title = f"{tag}"
    body = f"Automated release for {version}"

    # Create tag and push
    try:
        create_and_push_tag(tag, msg=body)
    except Exception as e:
        print("Failed to create/push tag:", e)
        # continue - release may still be possible if tag exists remotely

    # Upload using gh if available
    if not gh_installed():
        print("gh CLI not found; skipping GitHub release upload")
        return 0

    assets = []
    if os.path.isfile(bin_path):
        assets.append(bin_path)
    if packed and os.path.isfile(packed):
        assets.append(packed)

    if not assets:
        print("No artifacts found to upload:", bin_path, packed)
        return 0

    try:
        create_or_update_release(tag, title, body, assets)
    except Exception as e:
        print("Failed to create/update release:", e)
        return 0

    print(f"Release {tag} done. Assets: {assets}")
    return 0


if __name__ == '__main__':
    sys.exit(main())
