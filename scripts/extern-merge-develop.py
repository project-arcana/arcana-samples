#!/usr/bin/env python3

import os
import subprocess

assert os.path.exists("extern"), "execute in arcana-samples root"

print("fetching ...")
subprocess.check_call(["git", "fetch"])

for f in os.listdir("extern"):

    # print(f)

    s = subprocess.check_output(
        ["git", "branch", "-a"], cwd="extern/" + f).decode("utf-8")

    is_dev_or_master = False
    has_develop = False

    for b in s.splitlines():
        b = b.strip()
        if b in ["* develop", "* master"]:
            is_dev_or_master = True
        if b == "remotes/origin/develop":
            has_develop = True

    # print("checking {} (is on dev/master: {}, has dev: {})".format(f, is_dev_or_master, has_develop))

    if is_dev_or_master:
        continue
    if not has_develop:
        continue

    print()
    print("- merging", f)

    subprocess.check_call(["git", "merge", "origin/develop"], cwd="extern/" + f)
