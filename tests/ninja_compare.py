#!/usr/bin/env python3
"""Differential test: ninja against smith's executor (`smith --smith-exec`) on random graphs.

usage: python3 tests/ninja_compare.py <smith binary> [first seed [last seed [steps]]]

Not part of `smith build test`, which needs no ninja: run it by hand when the executor changes, with ninja installed
(it exits 0 saying so when there is none).

For each seed, a random manifest (plain, restat, depfile and multi-output steps, implicit and order-only inputs, aliases
and a default) is built in two identical folders, one by ninja and one by the executor. Then the same random edits (a
source changed or only touched, an output deleted or touched, a header a depfile named, a depfile's list, a command, one
target asked for) are made in both, and after each edit the two builds must have run the same commands, made the same
files and exited with the same status."""
import os, random, shutil, subprocess, sys, tempfile, time

NINJA = shutil.which("ninja")
S = tempfile.mkdtemp(prefix="smith-ninja-compare-")


def sh_quote(text):
    return "'" + text.replace("'", "'\\''") + "'"


def make_graph(rng):
    sources = ["s%d.txt" % i for i in range(rng.randint(2, 5))]
    headers = ["h%d.h" % i for i in range(rng.randint(2, 4))]
    edges = []
    outputs = []
    for e in range(rng.randint(4, 14)):
        pool = sources + outputs
        ins = rng.sample(pool, min(rng.randint(0, 3), len(pool)))
        impl = rng.sample(pool, rng.randint(0, 1)) if pool and rng.random() < 0.3 else []
        order = rng.sample(outputs, rng.randint(0, 1)) if outputs and rng.random() < 0.3 else []
        kind = rng.choice(["plain", "plain", "restat", "restat", "deps", "multi", "restat_deps", "restat_multi"])
        outs = ["o%d.txt" % e]
        if kind in ("multi", "restat_multi"):
            outs.append("o%db.txt" % e)
        edge = {"id": e, "kind": kind, "ins": ins, "impl": impl, "order": order, "outs": outs, "version": 0}
        if kind in ("deps", "restat_deps"):
            edge["deplist"] = "dl%d.txt" % e
            edge["headers"] = rng.sample(headers, rng.randint(0, len(headers)))
        edges.append(edge)
        outputs += outs
    aliases = []
    if rng.random() < 0.7:
        aliases.append(("all", rng.sample(outputs, min(len(outputs), rng.randint(1, 4)))))
    defaults = ["all"] if aliases and rng.random() < 0.6 else rng.sample(outputs, min(len(outputs), rng.randint(1, 3)))
    return {"sources": sources, "headers": headers, "edges": edges, "aliases": aliases, "defaults": defaults,
            "outputs": outputs}


def command(edge):
    e = edge["id"]
    ins = " ".join(sh_quote(i) for i in edge["ins"])
    out = edge["outs"][0]
    log = "echo %d >> runs.log" % e
    version = "# v%d" % edge["version"]
    body = []
    if edge["kind"] in ("deps", "restat_deps"):
        dl = edge["deplist"]
        body.append("cat %s %s > %s.new" % (ins, dl, out))
        body.append("echo \"%s: $(cat %s)\" > %s.d" % (out, dl, out))
    elif edge["kind"] == "restat":
        body.append("head -n 1 %s > %s.new" % (ins if ins else "/dev/null", out))
    elif edge["kind"] == "restat_multi":
        # The first output is the first line of the inputs, the second all of them: one can stay while the other changes.
        second = edge["outs"][1]
        body.append("cat %s > %s.all" % (ins if ins else "/dev/null", second))
        body.append("head -n 1 %s.all > %s.new" % (second, out))
        body.append("(cmp -s %s.all %s && rm %s.all) || mv %s.all %s" % (second, second, second, second, second))
    else:
        body.append("cat %s > %s.new" % (ins if ins else "/dev/null", out))
    if edge["kind"] in ("restat", "restat_deps", "restat_multi"):
        body.append("(cmp -s %s.new %s && rm %s.new) || mv %s.new %s" % (out, out, out, out, out))
    else:
        body.append("mv %s.new %s" % (out, out))
    for extra in edge["outs"][1:]:
        if edge["kind"] != "restat_multi":
            body.append("cp %s %s" % (out, extra))
    body.append(log)
    return "; ".join(body) + " " + version


def write_manifest(graph, path):
    lines = ["builddir = .smith", "rule step", "  command = $cmd"]
    for edge in graph["edges"]:
        outs = " ".join(edge["outs"])
        ins = list(edge["ins"])
        if "deplist" in edge:
            ins.append(edge["deplist"])
        text = "build %s: step %s" % (outs, " ".join(ins))
        if edge["impl"]:
            text += " | " + " ".join(edge["impl"])
        if edge["order"]:
            text += " || " + " ".join(edge["order"])
        lines.append(text)
        lines.append("  cmd = " + command(edge).replace("$", "$$"))
        if edge["kind"] in ("restat", "restat_deps", "restat_multi"):
            lines.append("  restat = 1")
        if edge["kind"] in ("deps", "restat_deps"):
            lines.append("  depfile = %s.d" % edge["outs"][0])
            lines.append("  deps = gcc")
    for name, targets in graph["aliases"]:
        lines.append("build %s: phony %s" % (name, " ".join(targets)))
    lines.append("default " + " ".join(graph["defaults"]))
    open(path, "w").write("\n".join(lines) + "\n")


def setup(graph, folder, rng_seed):
    shutil.rmtree(folder, ignore_errors=True)
    os.makedirs(folder)
    r = random.Random(rng_seed)
    for s in graph["sources"]:
        open(os.path.join(folder, s), "w").write("%d\nline\n" % r.randint(0, 99))
    for h in graph["headers"]:
        open(os.path.join(folder, h), "w").write("header %d\n" % r.randint(0, 99))
    for edge in graph["edges"]:
        if "deplist" in edge:
            open(os.path.join(folder, edge["deplist"]), "w").write(" ".join(edge["headers"]) + "\n")
    write_manifest(graph, os.path.join(folder, "build.ninja"))


def run(tool, smith, folder, args):
    log = os.path.join(folder, "runs.log")
    if os.path.exists(log):
        os.remove(log)
    if tool == "ninja":
        cmd = [NINJA, "-j1", "-f", "build.ninja"] + args
    else:
        cmd = [smith, "--smith-exec", "build.ninja", "-j1", "-d", "explain"] + args
    r = subprocess.run(cmd, cwd=folder, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    ran = sorted(open(log).read().split()) if os.path.exists(log) else []
    return r.returncode, ran, r.stdout


def snapshot(folder, graph):
    state = {}
    for name in graph["outputs"] + graph["sources"] + graph["headers"]:
        p = os.path.join(folder, name)
        state[name] = open(p).read() if os.path.exists(p) else None
    return state


def mutate(rng, graph, folders):
    """One random edit, made the same way in every folder. Returns a description and the targets to build."""
    time.sleep(0.02)
    choice = rng.choice(["source", "source", "touch", "delete", "header", "deplist", "command", "none", "touch_out"])
    target_args = []
    if rng.random() < 0.25:
        target_args = [rng.choice(graph["outputs"])]
    desc = choice
    if choice == "source":
        name = rng.choice(graph["sources"])
        data = "%d\nline\n" % rng.randint(0, 3) if rng.random() < 0.5 else "%d\nother\n" % rng.randint(0, 3)
        for f in folders:
            open(os.path.join(f, name), "w").write(data)
        desc += " " + name
    elif choice == "touch":
        name = rng.choice(graph["sources"] + graph["headers"])
        for f in folders:
            os.utime(os.path.join(f, name))
        desc += " " + name
    elif choice == "touch_out":
        name = rng.choice(graph["outputs"])
        for f in folders:
            if os.path.exists(os.path.join(f, name)):
                os.utime(os.path.join(f, name))
        desc += " " + name
    elif choice == "delete":
        name = rng.choice(graph["outputs"])
        for f in folders:
            if os.path.exists(os.path.join(f, name)):
                os.remove(os.path.join(f, name))
        desc += " " + name
    elif choice == "header":
        name = rng.choice(graph["headers"])
        data = "header %d\n" % rng.randint(0, 5)
        for f in folders:
            open(os.path.join(f, name), "w").write(data)
        desc += " " + name
    elif choice == "deplist":
        deps = [e for e in graph["edges"] if "deplist" in e]
        if deps:
            edge = rng.choice(deps)
            edge["headers"] = rng.sample(graph["headers"], rng.randint(0, len(graph["headers"])))
            for f in folders:
                open(os.path.join(f, edge["deplist"]), "w").write(" ".join(edge["headers"]) + "\n")
            desc += " " + edge["deplist"]
    elif choice == "command":
        edge = rng.choice(graph["edges"])
        edge["version"] += 1
        for f in folders:
            write_manifest(graph, os.path.join(f, "build.ninja"))
        desc += " edge %d" % edge["id"]
    return desc, target_args


def main():
    if NINJA is None:
        print("ninja is not installed: nothing to compare with")
        return
    smith = os.path.abspath(sys.argv[1])
    first = int(sys.argv[2]) if len(sys.argv) > 2 else 1
    last = int(sys.argv[3]) if len(sys.argv) > 3 else first + 49
    steps = int(sys.argv[4]) if len(sys.argv) > 4 else 12
    bad = 0
    for seed in range(first, last + 1):
        rng = random.Random(seed)
        graph = make_graph(rng)
        a, b = os.path.join(S, "ninja"), os.path.join(S, "smith")
        setup(graph, a, seed)
        setup(graph, b, seed)
        history = []
        for step in range(steps + 1):
            if step == 0:
                desc, args = "initial build", []
            else:
                desc, args = mutate(rng, graph, [a, b])
            history.append(desc + (" -> " + " ".join(args) if args else ""))
            time.sleep(0.02)
            ra = run("ninja", smith, a, args)
            rb = run("smith", smith, b, args)
            sa, sb = snapshot(a, graph), snapshot(b, graph)
            if os.environ.get("VERBOSE"):
                print("  step %d: %s: ninja ran %s, smith ran %s" % (step, history[-1], ra[1], rb[1]))
            ok = (ra[0] == rb[0]) and ra[1] == rb[1] and sa == sb
            if not ok:
                bad += 1
                print("MISMATCH seed %d step %d (%s)" % (seed, step, history[-1]))
                print("  history:", "; ".join(history))
                print("  ninja: status %d ran %s" % (ra[0], ra[1]))
                print("  smith: status %d ran %s" % (rb[0], rb[1]))
                if sa != sb:
                    print("  files differ:", [k for k in sa if sa[k] != sb[k]])
                print("  ninja said:", ra[2].strip().splitlines()[-3:])
                print("  smith said:", rb[2].strip().splitlines()[-12:])
                break
    shutil.rmtree(S, ignore_errors=True)
    print("%d seeds, %d mismatches" % (last - first + 1, bad))
    sys.exit(1 if bad else 0)


main()
