#!/usr/bin/env python3
"""
amosOZ — Arianna Method Operating System
The most atomic way to build a working OS-like environment from scratch.
Single file. No external dependencies. The complete first algorithm.

AMOS is the operating body. OZ is the extensibility field.
"""

import sys
import os
import time
import json
import platform
import struct
import readline
import hashlib

VERSION = "0.4.0"
SYSTEM_NAME = "amosOZ"
BUILD_DATE = "2026-06-30"
MOTD = (
    f"amosOZ v{VERSION} — Arianna Method Operating System\n"
    "Dedicated to Amos Oz (עוז — strength, courage)\n"
    "Compatibility is a treaty, not obedience.\n"
    "Type 'help', 'selftest', or 'motd'. Every command leaves a trace.\n"
)
MAX_SCRIPT_DEPTH = 8

# ─── Hardware/Profile Detection ───────────────────────────────────────────────
# Detection must never crash. Unknown = "unknown", not guessed.

def detect_hardware():
    """Detect host environment using only stdlib."""
    hw = {
        "platform": platform.system() or "unknown",
        "platform_release": platform.release() or "unknown",
        "machine": platform.machine() or "unknown",
        "processor": platform.processor() or platform.machine() or "unknown",
        "cpu_count": -1,
        "memory_mb": -1,
        "terminal": "unknown",
        "persistence": 1,
        "hostname": "unknown",
    }
    try:
        n = os.cpu_count()
        hw["cpu_count"] = n if n else -1
    except Exception:
        pass
    try:
        hw["hostname"] = platform.node() or "unknown"
    except Exception:
        pass
    try:
        if os.path.exists("/proc/meminfo"):
            with open("/proc/meminfo") as f:
                for line in f:
                    if line.startswith("MemTotal:"):
                        kb = int(line.split()[1])
                        hw["memory_mb"] = kb // 1024
                        break
    except Exception:
        pass
    try:
        hw["terminal"] = os.environ.get("TERM", "unknown")
    except Exception:
        pass
    return hw


def patmatch(pat, name):
    if not pat or not name:
        return False
    if pat == "*":
        return True
    if "*" not in pat:
        return pat == name
    pre, suffix = pat.split("*", 1)
    if not name.startswith(pre):
        return False
    if len(suffix) > len(name):
        return False
    return name.endswith(suffix)


def str_trim(s):
    return s.strip() if s else ""


# ─── Error Codes ──────────────────────────────────────────────────────────────

ERR_OK = 0
ERR_NOT_FOUND = 1
ERR_PERMISSION = 2
ERR_EXISTS = 3
ERR_NO_MEMORY = 4
ERR_INVALID = 5
ERR_IO = 6
ERR_NO_PROCESS = 7
ERR_MODULE = 8


# ─── Virtual Memory ──────────────────────────────────────────────────────────
# "No kernel mythology here: only state, contracts, and consequences."

class VirtualMemory:
    def __init__(self, total_kb=65536):
        self.total = total_kb
        self.used = 0
        self.blocks = {}  # id -> {size, flags, owner}
        self.next_id = 1

    def alloc(self, size, owner="system", flags="rw-"):
        if self.used + size > self.total:
            return None, ERR_NO_MEMORY
        bid = self.next_id
        self.next_id += 1
        self.blocks[bid] = {"size": size, "flags": flags, "owner": owner}
        self.used += size
        return bid, ERR_OK

    def free(self, bid):
        if bid not in self.blocks:
            return ERR_NOT_FOUND
        self.used -= self.blocks[bid]["size"]
        del self.blocks[bid]
        return ERR_OK

    def stats(self):
        return {
            "total_kb": self.total,
            "used_kb": self.used,
            "free_kb": self.total - self.used,
            "blocks": len(self.blocks),
        }

    def mmap(self):
        return list(self.blocks.items())


# ─── Virtual Filesystem ──────────────────────────────────────────────────────
# "Compatibility is a treaty, not obedience."

class VirtualFS:
    def __init__(self, kernel=None):
        self.tree = {}
        self.cwd = "/"
        self.kernel = kernel
        self._init_tree()

    def _node(self, ntype, **kw):
        now = time.time()
        base = {"type": ntype, "perms": "rw-r--r--", "created": now,
                "modified": now, "owner": "user", "content": "",
                "symlink_target": ""}
        base.update(kw)
        return base

    def _init_tree(self):
        dirs = ["/", "/bin", "/etc", "/home", "/tmp", "/var", "/dev",
                "/proc", "/sys", "/usr", "/usr/lib", "/usr/share",
                "/usr/share/amosoz", "/home/user", "/var/log", "/etc/amosoz"]
        for d in dirs:
            self.tree[d] = self._node("dir", perms="rwxr-xr-x", owner="root")
        self.tree["/etc/hostname"] = self._node("file", content="amosoz", owner="root")
        self.tree["/etc/amosoz/version"] = self._node("file", content=VERSION, owner="root")
        self.tree["/etc/motd"] = self._node("file", owner="root", content=(
            "amosOZ — Arianna Method Operating System\n"
            "Dedicated to Amos Oz (עוז)\n"
            "OZ begins where extension becomes accountable.\n"))
        self.tree["/etc/os-release"] = self._node("file", owner="root", content=(
            'NAME="amosOZ"\n'
            'PRETTY_NAME="amosOZ (Arianna Method Operating System)"\n'
            'ID=amosoz\n'
            f'VERSION_ID="{VERSION}"\n'
            'HOME_URL="https://github.com/ariannamethod/amos.OZ"\n'
            'SUPPORT_END="never"\n'))
        for p in ("/proc/uptime", "/proc/meminfo", "/proc/cpuinfo",
                  "/proc/version", "/proc/self/status"):
            self.tree[p] = self._node("file", owner="root", content="")
        self.tree["/usr/share/amosoz/quotes.txt"] = self._node("file", owner="root", content=(
            "A conflict begins and ends in the hearts of men, not in the hills.\n"
            "The opposite of indifference is not love but curiosity.\n"
            "We all pay a price for our dreams.\n"
            "עוז — strength lives in the willingness to see the other.\n"
            "Literature is a chamber of justice without a judge.\n"))
        bins = [
            "echo", "ls", "cat", "pwd", "uname", "whoami", "date", "help",
            "grep", "head", "tail", "wc", "test", "hostname", "id", "true", "false",
            "uptime", "dmesg", "find", "ln", "export", "source", "spec", "doctor",
        ]
        for name in bins:
            p = f"/bin/{name}"
            self.tree[p] = self._node("file", content="#!/amossh\n__builtin__\n",
                                      perms="rwxr-xr-x", owner="root")
        self.tree["/home/user/hello.amos"] = self._node(
            "file", perms="rwxr-xr-x", owner="user",
            content="#!/amossh\necho Script greeting:\necho $@\n")

    def _normalize(self, path, cwd=None):
        cwd = cwd if cwd is not None else self.cwd
        if not path.startswith("/"):
            path = ("/" + path) if cwd == "/" else cwd + "/" + path
        parts = []
        for p in path.split("/"):
            if not p or p == ".":
                continue
            if p == "..":
                if parts:
                    parts.pop()
            else:
                parts.append(p)
        return "/" + "/".join(parts) if parts else "/"

    def _resolve(self, path, follow_symlinks=True):
        out = self._normalize(path)
        if not follow_symlinks:
            return out
        for _ in range(8):
            node = self.tree.get(out)
            if not node or node["type"] != "symlink":
                break
            out = self._normalize(node["symlink_target"])
        return out

    def resolve_node(self, path):
        return self._resolve(path)

    def _perm_bit(self, perms, bit_idx):
        return len(perms) >= 9 and perms[bit_idx] != "-"

    def access(self, path, user, op):
        r = self._resolve(path, follow_symlinks=False)
        node = self.tree.get(r)
        if not node:
            return ERR_NOT_FOUND
        if user == "root":
            return ERR_OK
        base = 0 if node.get("owner") == user else 6
        if op == "r":
            bit = base
        elif op == "w":
            bit = base + 1
        elif op == "x":
            bit = base + 2
        else:
            return ERR_INVALID
        return ERR_OK if self._perm_bit(node["perms"], bit) else ERR_PERMISSION

    def add_symlink(self, linkpath, target):
        r = self._normalize(linkpath)
        if r in self.tree:
            return ERR_EXISTS
        self.tree[r] = self._node("symlink", perms="lrwxrwxrwx",
                                  symlink_target=self._resolve(target))
        return ERR_OK

    def read_to_buf(self, path):
        if self.kernel and self.kernel.proc_is_virtual(path):
            self.kernel.proc_refresh_all()
        r = self._resolve(path)
        node = self.tree.get(r)
        if not node or node["type"] in ("dir", "symlink"):
            return None, ERR_NOT_FOUND
        if self.kernel and self.kernel.access_node(r, "r") != ERR_OK:
            return None, ERR_PERMISSION
        return node.get("content", ""), ERR_OK

    def exists(self, path):
        return self._resolve(path) in self.tree

    def is_dir(self, path):
        r = self._resolve(path)
        return r in self.tree and self.tree[r]["type"] == "dir"

    def mkdir(self, path):
        r = self._resolve(path)
        if r in self.tree:
            return ERR_EXISTS
        parent = "/".join(r.split("/")[:-1]) or "/"
        if parent not in self.tree or self.tree[parent]["type"] != "dir":
            return ERR_NOT_FOUND
        self.tree[r] = {"type": "dir", "perms": "rwxr-xr-x", "created": time.time(),
                        "modified": time.time(), "owner": "user"}
        return ERR_OK

    def touch(self, path):
        r = self._resolve(path)
        if r in self.tree:
            self.tree[r]["modified"] = time.time()
            return ERR_OK
        parent = "/".join(r.split("/")[:-1]) or "/"
        if parent not in self.tree:
            return ERR_NOT_FOUND
        self.tree[r] = {"type": "file", "content": "", "perms": "rw-r--r--",
                        "created": time.time(), "modified": time.time(), "owner": "user"}
        return ERR_OK

    def write(self, path, content):
        r = self._resolve(path)
        if r in self.tree and self.tree[r]["type"] == "dir":
            return ERR_INVALID
        parent = "/".join(r.split("/")[:-1]) or "/"
        if parent not in self.tree:
            return ERR_NOT_FOUND
        if r not in self.tree:
            self.tree[r] = {"type": "file", "content": content, "perms": "rw-r--r--",
                            "created": time.time(), "modified": time.time(), "owner": "user"}
        else:
            self.tree[r]["content"] = content
            self.tree[r]["modified"] = time.time()
        return ERR_OK

    def append(self, path, content):
        r = self._resolve(path)
        if r not in self.tree or self.tree[r]["type"] != "file":
            return ERR_NOT_FOUND
        self.tree[r]["content"] += content
        self.tree[r]["modified"] = time.time()
        return ERR_OK

    def read(self, path):
        r = self._resolve(path)
        if r not in self.tree:
            return None, ERR_NOT_FOUND
        if self.tree[r]["type"] != "file":
            return None, ERR_INVALID
        return self.tree[r]["content"], ERR_OK

    def rm(self, path):
        r = self._resolve(path)
        if r not in self.tree:
            return ERR_NOT_FOUND
        if self.tree[r]["type"] == "dir":
            return ERR_INVALID
        del self.tree[r]
        return ERR_OK

    def rmdir(self, path):
        r = self._resolve(path)
        if r not in self.tree:
            return ERR_NOT_FOUND
        if self.tree[r]["type"] != "dir":
            return ERR_INVALID
        # Check empty
        for k in self.tree:
            if k != r and k.startswith(r + "/"):
                return ERR_INVALID
        if r == "/":
            return ERR_PERMISSION
        del self.tree[r]
        return ERR_OK

    def ls(self, path=None):
        r = self._resolve(path or self.cwd)
        if r not in self.tree or self.tree[r]["type"] != "dir":
            return None, ERR_NOT_FOUND
        prefix = r if r == "/" else r + "/"
        entries = []
        for k in sorted(self.tree.keys()):
            if k == r:
                continue
            if k.startswith(prefix):
                rest = k[len(prefix):]
                if "/" not in rest:
                    entries.append((rest, self.tree[k]["type"], self.tree[k]["perms"]))
        return entries, ERR_OK

    def mv(self, src, dst):
        rs = self._resolve(src)
        rd = self._resolve(dst)
        if rs not in self.tree:
            return ERR_NOT_FOUND
        self.tree[rd] = self.tree.pop(rs)
        # Move children if dir
        if self.tree[rd]["type"] == "dir":
            to_move = [(k, k.replace(rs, rd, 1)) for k in list(self.tree.keys())
                       if k.startswith(rs + "/")]
            for old, new in to_move:
                self.tree[new] = self.tree.pop(old)
        return ERR_OK

    def cp(self, src, dst):
        rs = self._resolve(src)
        rd = self._resolve(dst)
        if rs not in self.tree:
            return ERR_NOT_FOUND
        if self.tree[rs]["type"] == "dir":
            return ERR_INVALID
        import copy
        self.tree[rd] = copy.deepcopy(self.tree[rs])
        self.tree[rd]["created"] = time.time()
        return ERR_OK

    def chmod(self, path, perms):
        r = self._resolve(path)
        if r not in self.tree:
            return ERR_NOT_FOUND
        self.tree[r]["perms"] = perms
        return ERR_OK

    def stat(self, path):
        r = self._resolve(path)
        if r not in self.tree:
            return None, ERR_NOT_FOUND
        node = self.tree[r]
        info = {"path": r, "type": node["type"], "perms": node["perms"],
                "owner": node["owner"], "created": node["created"],
                "modified": node["modified"]}
        if node["type"] == "file":
            info["size"] = len(node.get("content", ""))
        return info, ERR_OK

    def tree_view(self, path=None, prefix="", depth=0, max_depth=4):
        r = self._resolve(path or self.cwd)
        if depth > max_depth:
            return []
        lines = []
        entries, err = self.ls(r)
        if err != ERR_OK:
            return lines
        for name, typ, _ in entries:
            marker = "/" if typ == "dir" else ""
            lines.append(f"{prefix}{name}{marker}")
            if typ == "dir" and depth < max_depth:
                child = r + "/" + name if r != "/" else "/" + name
                lines.extend(self.tree_view(child, prefix + "  ", depth + 1, max_depth))
        return lines

    def cd(self, path):
        r = self._resolve(path)
        if r not in self.tree or self.tree[r]["type"] != "dir":
            return ERR_NOT_FOUND
        self.cwd = r
        return ERR_OK

    def serialize(self):
        return json.dumps({"tree": self.tree, "cwd": self.cwd})

    def deserialize(self, data):
        obj = json.loads(data)
        self.tree = obj["tree"]
        self.cwd = obj.get("cwd", "/")


# ─── Process/Task Table ───────────────────────────────────────────────────────

class ProcessTable:
    def __init__(self):
        self.processes = {}
        self.next_pid = 1
        self.tick_count = 0

    def spawn(self, name, state="running"):
        pid = self.next_pid
        self.next_pid += 1
        self.processes[pid] = {"name": name, "state": state,
                               "started": time.time(), "ticks": 0}
        return pid

    def kill(self, pid):
        if pid not in self.processes:
            return ERR_NO_PROCESS
        del self.processes[pid]
        return ERR_OK

    def tick(self):
        self.tick_count += 1
        for pid in self.processes:
            if self.processes[pid]["state"] == "running":
                self.processes[pid]["ticks"] += 1
        return self.tick_count

    def ps(self):
        return list(self.processes.items())

    def status(self, pid):
        if pid not in self.processes:
            return None, ERR_NO_PROCESS
        return self.processes[pid], ERR_OK


# ─── OZ Ledger — AI-ready deterministic command provenance and intent trace ──
# "Every command leaves a trace."

class OZLedger:
    def __init__(self):
        self.entries = []

    def record_ex(self, command, parsed_cmd, parsed_args, actor, tick, result_code,
                  explanation, reversible=0, undo_path=None, undo_content=None,
                  undo_is_dir=0, undo_was_create=0):
        self.entries.append({
            "command": command,
            "parsed_cmd": parsed_cmd or "",
            "parsed_args": parsed_args or "",
            "actor": actor,
            "tick": tick,
            "timestamp": time.time(),
            "result_code": result_code,
            "explanation": explanation or "executed",
            "reversible": reversible,
            "undo_path": undo_path or "",
            "undo_content": undo_content or "",
            "undo_is_dir": undo_is_dir,
            "undo_was_create": undo_was_create,
        })

    def trace(self, n=10):
        return self.entries[-n:]

    def replay_info(self):
        return self.entries

    def size(self):
        return len(self.entries)


# ─── Module/Slot/Overhead System ─────────────────────────────────────────────
# "A slot is a promise with a boundary."
# "OZ begins where extension becomes accountable."

BUILTIN_SLOTS = [
    "shell.commands", "fs.drivers", "devices", "ai.hooks",
    "sched.hooks", "boot.hooks", "diagnostics", "experiments",
    "oz.contracts", "oz.ledger"
]


class OZLayer:
    def __init__(self):
        self.slots = {s: [] for s in BUILTIN_SLOTS}
        self.modules = {}
        self.overheads = {}
        self.hooks = {"boot": [], "sched": [], "ai": [], "diag": [], "experiment": []}
        self.contracts = {}

    def register_slot(self, name):
        if name not in self.slots:
            self.slots[name] = []

    def register_module(self, name, module):
        self.modules[name] = module
        overhead = {"dispatch_entries": 0, "hooks": 0, "slots_occupied": []}
        if hasattr(module, "commands"):
            overhead["dispatch_entries"] = len(module.commands)
        if hasattr(module, "slots"):
            for s in module.slots:
                if s in self.slots:
                    self.slots[s].append(name)
                    overhead["slots_occupied"].append(s)
        if hasattr(module, "hooks"):
            for h in module.hooks:
                if h in self.hooks:
                    self.hooks[h].append(name)
                    overhead["hooks"] += 1
        if hasattr(module, "contract"):
            self.contracts[name] = module.contract
        self.overheads[name] = overhead
        if hasattr(module, "init"):
            module.init()
        return ERR_OK

    def unregister_module(self, name):
        if name not in self.modules:
            return ERR_NOT_FOUND
        mod = self.modules[name]
        if hasattr(mod, "shutdown"):
            mod.shutdown()
        for s in self.slots:
            if name in self.slots[s]:
                self.slots[s].remove(name)
        for h in self.hooks:
            if name in self.hooks[h]:
                self.hooks[h].remove(name)
        if name in self.contracts:
            del self.contracts[name]
        del self.modules[name]
        del self.overheads[name]
        return ERR_OK

    def total_overhead(self):
        total = {"dispatch_entries": 0, "hooks": 0, "slots_occupied": 0, "modules": len(self.modules)}
        for name, ov in self.overheads.items():
            total["dispatch_entries"] += ov["dispatch_entries"]
            total["hooks"] += ov["hooks"]
            total["slots_occupied"] += len(ov["slots_occupied"])
        return total

    def call_module(self, mod_name, method, *args):
        if mod_name not in self.modules:
            return None, ERR_NOT_FOUND
        mod = self.modules[mod_name]
        if not hasattr(mod, method):
            return None, ERR_INVALID
        result = getattr(mod, method)(*args)
        return result, ERR_OK


# ─── Built-in Modules ────────────────────────────────────────────────────────

class CoreutilsModule:
    name = "coreutils"
    description = "Core utilities module"
    commands = ["uptime"]
    slots = ["shell.commands"]
    hooks = []
    contract = {"provides": ["uptime"], "requires": [], "version": "0.1"}

    def init(self):
        self.start = time.time()

    def shutdown(self):
        pass

    def uptime(self):
        return f"System up for {int(time.time() - self.start)} seconds"


class HWProbeModule:
    name = "hwprobe"
    description = "Hardware probe module"
    commands = ["hwinfo"]
    slots = ["diagnostics"]
    hooks = ["boot"]
    contract = {"provides": ["hwinfo"], "requires": [], "version": "0.1"}

    def init(self):
        pass

    def shutdown(self):
        pass

    def hwinfo(self, hw):
        return hw


class DiagModule:
    name = "diag"
    description = "Diagnostics module"
    commands = ["diag_status"]
    slots = ["diagnostics"]
    hooks = ["diag"]
    contract = {"provides": ["diag_status"], "requires": [], "version": "0.1"}

    def init(self):
        pass

    def shutdown(self):
        pass

    def diag_status(self):
        return "diagnostics: nominal"


class AISeedModule:
    name = "ai_seed"
    description = "AI seed hooks module"
    commands = ["ai_status"]
    slots = ["ai.hooks"]
    hooks = ["ai"]
    contract = {"provides": ["ai_status", "intent_hints"], "requires": ["oz.ledger"], "version": "0.1"}

    def init(self):
        pass

    def shutdown(self):
        pass

    def ai_status(self):
        return "ai_seed: active, no external model loaded"


class OZLedgerModule:
    name = "oz_ledger"
    description = "OZ Ledger provenance tracking module"
    commands = ["ledger_size"]
    slots = ["oz.ledger", "oz.contracts"]
    hooks = ["ai"]
    contract = {"provides": ["ledger_size", "trace", "replay"], "requires": [], "version": "0.1"}

    def init(self):
        pass

    def shutdown(self):
        pass

    def ledger_size(self, ledger):
        return ledger.size()


class ExampleExtModule:
    """Example extension module — adds 'fortune' command."""
    name = "fortune_ext"
    description = "Example extension: fortune cookies"
    commands = ["fortune"]
    slots = ["shell.commands", "experiments"]
    hooks = ["experiment"]
    contract = {"provides": ["fortune"], "requires": [], "version": "0.1"}

    def init(self):
        self.fortunes = [
            "The system is the territory.",
            "Every overhead has a name.",
            "A module is a guest with manners.",
            "Slots are finite; ambition is not.",
            "Trace everything. Regret nothing.",
        ]
        self.idx = 0

    def shutdown(self):
        pass

    def fortune(self):
        f = self.fortunes[self.idx % len(self.fortunes)]
        self.idx += 1
        return f


# ─── Kernel ──────────────────────────────────────────────────────────────────
# "The shell is the first weather of the system."

class AmosOZKernel:
    def __init__(self):
        self.name = SYSTEM_NAME
        self.version = VERSION
        self.boot_time = 0
        self.hw = {}
        self.mem = None
        self.fs = None
        self.procs = None
        self.oz = OZLayer()
        self.ledger = OZLedger()
        self.env = {}
        self.user = "user"
        self.history = []
        self.devices = []
        self.running = True
        self.hook_boot_fired = 0
        self.hook_sched_fired = 0
        self.boot_log = ""
        self.fortune_idx = 0
        self.fortune_oz_idx = 0
        self.shell_stdin_buf = ""
        self.shell_depth = 0
        self.pending_ledger = {}
        self._kernel_init()

    def _kernel_init(self):
        self.hw = detect_hardware()
        self.mem = VirtualMemory(65536)
        self.fs = VirtualFS(self)
        self.procs = ProcessTable()
        self.ledger = OZLedger()
        self.env = {}
        self.history = []
        self.user = "user"
        self.running = True
        self.hook_boot_fired = 0
        self.hook_sched_fired = 0
        self.fortune_idx = 0
        self.fortune_oz_idx = 0
        self.shell_stdin_buf = ""
        self.shell_depth = 0
        self.pending_ledger = {}
        self.devices = [
            {"name": "console", "type": "char", "status": "active"},
            {"name": "mem", "type": "block", "status": "active"},
            {"name": "null", "type": "char", "status": "active"},
            {"name": "zero", "type": "char", "status": "active"},
        ]
        self.boot_time = time.time()
        self._init_modules()
        self.env_set("HOME", "/home/user")
        self.env_set("USER", "user")
        self.env_set("SHELL", "/bin/amossh")
        self.env_set("PATH", "/bin:/usr/bin")
        self.env_set("TERM", self.hw.get("terminal", "unknown"))
        self.env_set("HOSTNAME", self.hw.get("hostname", "amosoz"))
        self.procs.spawn("init", "running")
        self.procs.spawn("amossh", "running")
        self.hook_boot_fired = self.fire_hook("boot")
        self.boot_log = (
            f"[boot] amosOZ {VERSION} started\n"
            f"[boot] {self.hook_boot_fired} hook subscribers on boot\n"
            "[boot] dedicated to Amos Oz (עוז)\n"
        )
        self.proc_refresh_all()

    def _init_modules(self):
        self.oz.register_module("coreutils", CoreutilsModule())
        self.oz.register_module("hwprobe", HWProbeModule())
        self.oz.register_module("diag", DiagModule())
        self.oz.register_module("ai_seed", AISeedModule())
        self.oz.register_module("oz_ledger", OZLedgerModule())
        self.oz.register_module("fortune_ext", ExampleExtModule())

    def env_set(self, key, val):
        self.env[key] = val

    def env_get(self, key):
        return self.env.get(key)

    def env_unset(self, key):
        self.env.pop(key, None)

    def fire_hook(self, name):
        return len(self.oz.hooks.get(name, []))

    def validate_module_contracts(self):
        for name, contract in self.oz.contracts.items():
            if name not in self.oz.modules:
                continue
            for req in contract.get("requires", []):
                if not self.oz.slots.get(req):
                    return ERR_MODULE
        return ERR_OK

    def proc_is_virtual(self, path):
        return path and (path == "/proc" or path.startswith("/proc/"))

    def access_node(self, path, op):
        return self.fs.access(path, self.user, op)

    def proc_refresh_all(self):
        elapsed = int(time.time() - self.boot_time)
        self._proc_write("/proc/uptime", f"{elapsed}.0\n")
        free = self.mem.total - self.mem.used
        self._proc_write("/proc/meminfo",
            f"MemTotal:       {self.mem.total} kB\n"
            f"MemFree:        {free} kB\n"
            f"MemAvailable:   {free} kB\n")
        cores = self.hw.get("cpu_count", 1)
        if cores < 1:
            cores = 1
        self._proc_write("/proc/cpuinfo",
            f"processor\t: 0\nmodel name\t: amosOZ virtual cpu\n"
            f"cpu cores\t: {cores}\nhostname\t: {self.hw.get('hostname', 'amosoz')}\n")
        self._proc_write("/proc/version",
            f"{SYSTEM_NAME} {VERSION} {self.hw.get('machine', 'unknown')} "
            f"{self.hw.get('platform', 'unknown')}\n")
        self._proc_write("/proc/self/status",
            f"Name:\tamossh\nPid:\t1\nState:\trunning (virtual)\n"
            f"VmSize:\t{self.mem.used} kB\nThreads:\t{self.procs.next_pid - 1}\n")

    def _proc_write(self, path, content):
        if path not in self.fs.tree:
            self.fs.tree[path] = self.fs._node("file", owner="root", content=content,
                                              perms="r--r--r--")
        else:
            self.fs.tree[path]["content"] = content
            self.fs.tree[path]["modified"] = time.time()

    def ledger_meta_clear(self):
        self.pending_ledger = {}

    def ledger_meta_set(self, reversible, explanation, undo_path=None,
                        undo_content=None, undo_is_dir=0, undo_was_create=0):
        self.pending_ledger = {
            "active": True, "reversible": reversible, "explanation": explanation,
            "undo_path": undo_path or "", "undo_content": undo_content or "",
            "undo_is_dir": undo_is_dir, "undo_was_create": undo_was_create,
        }

    def kernel_syscall(self, op, argv):
        if op == "read":
            if not argv:
                return "", ERR_INVALID
            content, err = self.fs.read_to_buf(argv[0])
            return (content or ""), err
        if op == "write":
            if len(argv) < 2:
                return "", ERR_INVALID
            path, content = argv[0], " ".join(argv[1:])
            err = self.fs.write(path, content)
            return "", err
        if op == "stat":
            info, err = self.fs.stat(argv[0]) if argv else (None, ERR_INVALID)
            if err != ERR_OK:
                return "", err
            typ = info["type"]
            size = info.get("size", 0) if typ == "file" else 0
            return (f"path={info['path']} type={typ} perms={info['perms']} "
                    f"owner={info['owner']} size={size}"), ERR_OK
        if op == "getcwd":
            return self.fs.cwd, ERR_OK
        if op == "chdir":
            if not argv:
                return "", ERR_INVALID
            r = self.fs._resolve(argv[0])
            node = self.fs.tree.get(r)
            if not node or node["type"] != "dir":
                return "", ERR_NOT_FOUND
            if self.access_node(r, "x") != ERR_OK:
                return "", ERR_PERMISSION
            self.fs.cwd = r
            return "", ERR_OK
        if op == "alloc":
            bid, err = self.mem.alloc(int(argv[0]), self.user) if argv else (None, ERR_INVALID)
            return str(bid) if bid else "", err
        if op == "free":
            return "", self.mem.free(int(argv[0])) if argv else ERR_INVALID
        if op == "spawn":
            return str(self.procs.spawn(argv[0])), ERR_OK if argv else ("", ERR_INVALID)
        if op == "kill":
            return "", self.procs.kill(int(argv[0])) if argv else ERR_INVALID
        if op == "getenv":
            return self.env_get(argv[0]) or "", ERR_OK if argv else ("", ERR_INVALID)
        return "", ERR_NOT_FOUND

    def dispatch(self, line):
        line = line.strip()
        if not line:
            return ""
        self.history.append(line)
        return self.shell_execute_line(line)

    def shell_execute_line(self, line):
        parts = []
        cur = []
        for ch in line:
            if ch == "|":
                parts.append("".join(cur).strip())
                cur = []
            else:
                cur.append(ch)
        parts.append("".join(cur).strip())
        pipe_buf = ""
        last_err = ERR_OK
        output = ""
        for i, seg in enumerate(parts):
            if i > 0:
                self.shell_stdin_buf = pipe_buf
            else:
                self.shell_stdin_buf = ""
            segout, last_err = self.shell_execute_segment(seg, line)
            if i < len(parts) - 1:
                pipe_buf = segout
            else:
                output = segout
            if last_err != ERR_OK:
                break
        self.shell_stdin_buf = ""
        return output

    def shell_execute_segment(self, seg, line):
        work = seg
        redir_path, redir_in, append = "", "", False
        if "<" in work:
            left, right = work.split("<", 1)
            work = left.strip()
            redir_in = right.strip()
        if ">>" in work:
            left, right = work.split(">>", 1)
            work = left.strip()
            redir_path = right.strip()
            append = True
        elif ">" in work:
            left, right = work.split(">", 1)
            work = left.strip()
            redir_path = right.strip()
        if redir_in:
            content, err = self.fs.read_to_buf(redir_in)
            if err != ERR_OK:
                return f"amosoz: {redir_in}: cannot open for input", err
            self.shell_stdin_buf = content or ""
        argv = work.split() if work else []
        out, err = self.dispatch_argv(line if line else work, argv)
        if redir_path and err == ERR_OK:
            werr = self.shell_redirect_write(redir_path, out, append)
            if werr == ERR_PERMISSION:
                return f"amosoz: {redir_path}: Permission denied", werr
            if werr != ERR_OK:
                return f"amosoz: redirect to {redir_path} failed", werr
            return "", ERR_OK
        return out, err

    def shell_redirect_write(self, path, data, append):
        r = self.fs._resolve(path)
        if r in self.fs.tree:
            if self.access_node(r, "w") != ERR_OK:
                return ERR_PERMISSION
            node = self.fs.tree[r]
            if append:
                node["content"] += data
                if data and not data.endswith("\n"):
                    node["content"] += "\n"
            else:
                node["content"] = data
            node["modified"] = time.time()
            return ERR_OK
        self.fs.write(path, data + ("\n" if data and not data.endswith("\n") else ""))
        return ERR_OK

    def path_lookup_executable(self, name):
        if "/" in name:
            r = self.fs._resolve(name)
            node = self.fs.tree.get(r)
            if not node or node["type"] == "dir":
                return None
            if self.access_node(r, "x") != ERR_OK:
                return None
            return r
        for d in (self.env_get("PATH") or "/bin:/usr/bin").split(":"):
            p = f"{d}/{name}"
            r = self.fs._resolve(p)
            node = self.fs.tree.get(r)
            if node and node["type"] != "dir" and self.access_node(r, "x") == ERR_OK:
                return r
        return None

    def is_script_node(self, path):
        r = self.fs._resolve(path)
        node = self.fs.tree.get(r)
        if not node:
            return False
        if node["type"] == "symlink":
            r = self.fs._resolve(node["symlink_target"])
            node = self.fs.tree.get(r)
        if not node or node["type"] == "dir":
            return False
        if ".amos" in r:
            return True
        return node.get("content", "").startswith("#!/amossh")

    def expand_script_vars(self, text, argv):
        out = []
        i = 0
        while i < len(text):
            if text[i] == "$" and i + 1 < len(text):
                if text[i + 1] == "@":
                    out.append(" ".join(argv[1:]))
                    i += 2
                    continue
                if text[i + 1].isdigit():
                    n = int(text[i + 1])
                    if n < len(argv):
                        out.append(argv[n])
                    i += 2
                    continue
            out.append(text[i])
            i += 1
        return "".join(out)

    def run_script_node(self, path, argv):
        if self.shell_depth >= MAX_SCRIPT_DEPTH:
            return "script: max nesting depth exceeded", ERR_INVALID
        self.shell_depth += 1
        r = self.fs._resolve(path)
        node = self.fs.tree.get(r)
        body = node.get("content", "")
        if "__builtin__" in body:
            cmd = r.rsplit("/", 1)[-1]
            nargs = argv[1:] if len(argv) > 1 else []
            out, err = self.dispatch_argv(None, [cmd] + nargs)
            self.shell_depth -= 1
            return out, err
        if body.startswith("#!"):
            body = body.split("\n", 1)[1] if "\n" in body else ""
        outputs = []
        for line in body.split("\n"):
            line = str_trim(line)
            if not line or line.startswith("#"):
                continue
            expanded = self.expand_script_vars(line, argv)
            segout = self.shell_execute_line(expanded)
            if segout:
                outputs.append(segout)
        self.shell_depth -= 1
        return "\n".join(outputs), ERR_OK

    def dispatch_argv(self, line, argv):
        self.ledger_meta_clear()
        self.fire_hook("ai")
        if not argv:
            return "", ERR_OK
        pargs = " ".join(argv[1:])
        if argv[0] == "exit":
            self.running = False
            out = "Shutting down amosOZ."
            self.ledger.record_ex(line or "exit", "exit", pargs, self.user,
                                  self.procs.tick_count, ERR_OK, "shutdown", 0)
            return out, ERR_OK
        handler = self._cmd_table().get(argv[0])
        if handler:
            result = handler(argv)
            if isinstance(result, tuple):
                out, err = result
            else:
                out, err = (result or ""), ERR_OK
            self._ledger_record(line, argv, pargs, out, err)
            return out, err
        resolved = self.path_lookup_executable(argv[0])
        if resolved and self.is_script_node(resolved):
            out, err = self.run_script_node(resolved, [argv[0]] + argv[1:])
            self.ledger.record_ex(line or argv[0], argv[0], pargs, self.user,
                                  self.procs.tick_count, err, "script exec", 0)
            return out, err
        out = f"amosoz: command not found: {argv[0]}"
        self.ledger.record_ex(line or argv[0], argv[0], pargs, self.user,
                              self.procs.tick_count, ERR_NOT_FOUND, "not found", 0)
        return out, ERR_NOT_FOUND

    def _ledger_record(self, line, argv, pargs, out, err):
        if self.pending_ledger.get("active"):
            pl = self.pending_ledger
            self.ledger.record_ex(
                line or argv[0], argv[0], pargs, self.user, self.procs.tick_count, err,
                pl["explanation"], pl["reversible"], pl["undo_path"], pl["undo_content"],
                pl["undo_is_dir"], pl["undo_was_create"])
            self.ledger_meta_clear()
        else:
            self.ledger.record_ex(line or argv[0], argv[0], pargs, self.user,
                                  self.procs.tick_count, err, "Executed", 0)

    def _cmd_table(self):
        return {
            "help": self._cmd_help, "clear": self._cmd_clear, "uname": self._cmd_uname,
            "version": self._cmd_version, "boot": self._cmd_boot, "hw": self._cmd_hw,
            "devices": self._cmd_devices, "gpu": self._cmd_gpu, "mem": self._cmd_mem,
            "mmap": self._cmd_mmap, "alloc": self._cmd_alloc, "free": self._cmd_free,
            "ps": self._cmd_ps, "run": self._cmd_run, "kill": self._cmd_kill,
            "tick": self._cmd_tick, "status": self._cmd_status, "pwd": self._cmd_pwd,
            "cd": self._cmd_cd, "ls": self._cmd_ls, "cat": self._cmd_cat,
            "touch": self._cmd_touch, "write": self._cmd_write, "append": self._cmd_append,
            "rm": self._cmd_rm, "mkdir": self._cmd_mkdir, "rmdir": self._cmd_rmdir,
            "mv": self._cmd_mv, "cp": self._cmd_cp, "chmod": self._cmd_chmod,
            "stat": self._cmd_stat, "tree": self._cmd_tree, "echo": self._cmd_echo,
            "env": self._cmd_env, "set": self._cmd_set, "unset": self._cmd_unset,
            "date": self._cmd_date, "history": self._cmd_history, "selftest": self._cmd_selftest,
            "oz": self._cmd_oz, "slots": self._cmd_slots, "modules": self._cmd_modules,
            "overhead": self._cmd_overhead, "hooks": self._cmd_hooks,
            "contracts": self._cmd_contracts, "loadmod": self._cmd_loadmod,
            "unloadmod": self._cmd_unloadmod, "call": self._cmd_call,
            "trace": self._cmd_trace, "replay": self._cmd_replay, "undo": self._cmd_undo,
            "whoami": self._cmd_whoami, "motd": self._cmd_motd, "syscall": self._cmd_syscall,
            "which": self._cmd_which, "exec": self._cmd_exec,
            "grep": self._cmd_grep, "head": self._cmd_head, "tail": self._cmd_tail,
            "wc": self._cmd_wc, "test": self._cmd_test, "[": self._cmd_test,
            "export": self._cmd_export, "source": self._cmd_source,
            "uptime": self._cmd_uptime, "dmesg": self._cmd_dmesg, "hostname": self._cmd_hostname,
            "id": self._cmd_id, "true": self._cmd_true, "false": self._cmd_false,
            "ln": self._cmd_ln, "find": self._cmd_find,
            "jobs": self._cmd_jobs, "fg": self._cmd_fg, "nohup": self._cmd_nohup,
            "spec": self._cmd_spec, "doctor": self._cmd_doctor,
            "reset": self._cmd_reset, "save": self._cmd_save, "load": self._cmd_load,
            "exit": self._cmd_exit, "fortune": self._cmd_fortune,
        }

    # ─── Command Implementations ─────────────────────────────────────────────

    def _cmd_help(self, argv):
        args = argv[1:]
        cmds = sorted([
            "help", "clear", "uname", "version", "boot", "hw", "devices", "gpu",
            "mem", "mmap", "alloc", "free", "ps", "run", "kill", "tick", "status",
            "pwd", "cd", "ls", "cat", "touch", "write", "append", "rm", "mkdir",
            "rmdir", "mv", "cp", "chmod", "stat", "tree", "echo", "env", "set",
            "unset", "date", "history", "selftest", "oz", "slots", "modules",
            "overhead", "hooks", "contracts", "loadmod", "unloadmod", "call",
            "trace", "replay", "reset", "save", "load", "exit", "fortune"
        ])
        return "amosOZ commands:\n  " + "  ".join(cmds)

    def _cmd_clear(self, argv):
        args = argv[1:]
        return "\033[2J\033[H"

    def _cmd_uname(self, argv):
        args = argv[1:]
        return f"{self.name} {self.version} {self.hw['machine']} {self.hw['platform']}"

    def _cmd_version(self, argv):
        args = argv[1:]
        return f"{self.name} version {self.version} (build {BUILD_DATE})"

    def _cmd_boot(self, argv):
        args = argv[1:]
        elapsed = int(time.time() - self.boot_time)
        return f"Boot time: {time.ctime(self.boot_time)}\nUptime: {elapsed}s"

    def _cmd_hw(self, argv):
        args = argv[1:]
        lines = ["Hardware Profile:"]
        for k, v in self.hw.items():
            lines.append(f"  {k}: {v}")
        return "\n".join(lines)

    def _cmd_devices(self, argv):
        args = argv[1:]
        lines = ["Devices:"]
        for d in self.devices:
            lines.append(f"  {d['name']:12} {d['type']:8} {d['status']}")
        return "\n".join(lines)

    def _cmd_gpu(self, argv):
        args = argv[1:]
        return "GPU: not available (Python runtime, no GPU abstraction)"

    def _cmd_mem(self, argv):
        args = argv[1:]
        s = self.mem.stats()
        return (f"Memory: {s['total_kb']} KB total, {s['used_kb']} KB used, "
                f"{s['free_kb']} KB free, {s['blocks']} blocks")

    def _cmd_mmap(self, argv):
        args = argv[1:]
        blocks = self.mem.mmap()
        if not blocks:
            return "No memory blocks allocated."
        lines = ["ID    Size(KB)  Flags  Owner"]
        for bid, info in blocks:
            lines.append(f"{bid:<5} {info['size']:<9} {info['flags']:<6} {info['owner']}")
        return "\n".join(lines)

    def _cmd_alloc(self, argv):
        args = argv[1:]
        if not args:
            return "Usage: alloc <size_kb>"
        try:
            size = int(args[0])
        except ValueError:
            return "Error: size must be integer"
        bid, err = self.mem.alloc(size, self.user)
        if err != ERR_OK:
            return "Error: out of memory"
        return f"Allocated block {bid} ({size} KB)"

    def _cmd_free(self, argv):
        args = argv[1:]
        if not args:
            return "Usage: free <block_id>"
        try:
            bid = int(args[0])
        except ValueError:
            return "Error: block_id must be integer"
        err = self.mem.free(bid)
        if err != ERR_OK:
            return f"Error: block {bid} not found"
        return f"Freed block {bid}"

    def _cmd_ps(self, argv):
        args = argv[1:]
        procs = self.procs.ps()
        if not procs:
            return "No processes."
        lines = ["PID   Name          State     Ticks"]
        for pid, info in procs:
            lines.append(f"{pid:<5} {info['name']:<13} {info['state']:<9} {info['ticks']}")
        return "\n".join(lines)

    def _cmd_run(self, argv):
        args = argv[1:]
        if not args:
            return "Usage: run <name>"
        name = args[0]
        pid = self.procs.spawn(name)
        return f"Started process '{name}' with PID {pid}"

    def _cmd_kill(self, argv):
        args = argv[1:]
        if not args:
            return "Usage: kill <pid>"
        try:
            pid = int(args[0])
        except ValueError:
            return "Error: pid must be integer"
        err = self.procs.kill(pid)
        if err != ERR_OK:
            return f"Error: process {pid} not found"
        return f"Killed process {pid}"

    def _cmd_tick(self, argv):
        args = argv[1:]
        t = self.procs.tick()
        return f"Tick: {t}"

    def _cmd_status(self, argv):
        args = argv[1:]
        if not args:
            return "Usage: status <pid>"
        try:
            pid = int(args[0])
        except ValueError:
            return "Error: pid must be integer"
        info, err = self.procs.status(pid)
        if err != ERR_OK:
            return f"Error: process {pid} not found"
        return f"PID {pid}: {info['name']} state={info['state']} ticks={info['ticks']}"

    def _cmd_pwd(self, argv):
        args = argv[1:]
        return self.fs.cwd

    def _cmd_cd(self, argv):
        args = argv[1:]
        path = args[0] if args else self.env_get("HOME") or "/home/user"
        out, err = self.kernel_syscall("chdir", [path])
        if err == ERR_PERMISSION:
            return f"cd: {path}: Permission denied", err
        if err != ERR_OK:
            return f"cd: no such directory: {path}", err
        return "", ERR_OK

    def _cmd_ls(self, argv):
        args = argv[1:]
        path = args[0] if args else None
        entries, err = self.fs.ls(path)
        if err != ERR_OK:
            return "ls: no such directory"
        if not entries:
            return ""
        lines = []
        for name, typ, perms in entries:
            marker = "/" if typ == "dir" else ""
            lines.append(f"{perms} {name}{marker}")
        return "\n".join(lines)

    def _cmd_cat(self, argv):
        args = argv[1:]
        if not args:
            if self.shell_stdin_buf:
                return self.shell_stdin_buf, ERR_OK
            return "Usage: cat <file>", ERR_INVALID
        out, err = self.kernel_syscall("read", [args[0]])
        if err == ERR_PERMISSION:
            return f"cat: {args[0]}: Permission denied", err
        if err != ERR_OK:
            return f"cat: {args[0]}: not found or not a file", err
        return out, ERR_OK

    def _cmd_touch(self, argv):
        args = argv[1:]
        if not args:
            return "Usage: touch <file>"
        err = self.fs.touch(args[0])
        if err != ERR_OK:
            return f"touch: cannot create {args[0]}"
        return ""

    def _cmd_write(self, argv):
        args = argv[1:]
        if len(args) < 2:
            return "Usage: write <file> <content...>", ERR_INVALID
        resolved = self.fs._resolve(args[0])
        old = ""
        was_create = resolved not in self.fs.tree
        if resolved in self.fs.tree:
            if self.access_node(resolved, "w") != ERR_OK:
                return f"write: {args[0]}: Permission denied", ERR_PERMISSION
            old = self.fs.tree[resolved].get("content", "")
        _, err = self.kernel_syscall("write", args)
        if err == ERR_OK:
            self.ledger_meta_set(1, "fs write", resolved, old, 0, was_create)
        return "", err

    def _cmd_append(self, argv):
        args = argv[1:]
        if len(args) < 2:
            return "Usage: append <file> <content...>"
        path = args[0]
        content = " ".join(args[1:])
        err = self.fs.append(path, content)
        if err != ERR_OK:
            return f"append: error: {path} not found"
        return ""

    def _cmd_rm(self, argv):
        args = argv[1:]
        if not args:
            return "Usage: rm <file>", ERR_INVALID
        resolved = self.fs._resolve(args[0])
        node = self.fs.tree.get(resolved)
        if not node or node["type"] == "dir":
            return f"rm: cannot remove {args[0]}", ERR_NOT_FOUND
        if self.access_node(resolved, "w") != ERR_OK:
            return f"rm: {args[0]}: Permission denied", ERR_PERMISSION
        backup = node.get("content", "")
        del self.fs.tree[resolved]
        self.ledger_meta_set(1, "fs delete", resolved, backup, 0, 0)
        return "", ERR_OK

    def _cmd_mkdir(self, argv):
        args = argv[1:]
        if not args:
            return "Usage: mkdir <dir>"
        err = self.fs.mkdir(args[0])
        if err != ERR_OK:
            return f"mkdir: cannot create {args[0]}"
        return ""

    def _cmd_rmdir(self, argv):
        args = argv[1:]
        if not args:
            return "Usage: rmdir <dir>"
        err = self.fs.rmdir(args[0])
        if err != ERR_OK:
            return f"rmdir: cannot remove {args[0]}"
        return ""

    def _cmd_mv(self, argv):
        args = argv[1:]
        if len(args) < 2:
            return "Usage: mv <src> <dst>"
        err = self.fs.mv(args[0], args[1])
        if err != ERR_OK:
            return f"mv: error"
        return ""

    def _cmd_cp(self, argv):
        args = argv[1:]
        if len(args) < 2:
            return "Usage: cp <src> <dst>"
        err = self.fs.cp(args[0], args[1])
        if err != ERR_OK:
            return f"cp: error"
        return ""

    def _cmd_chmod(self, argv):
        args = argv[1:]
        if len(args) < 2:
            return "Usage: chmod <perms> <path>"
        err = self.fs.chmod(args[1], args[0])
        if err != ERR_OK:
            return f"chmod: error"
        return ""

    def _cmd_stat(self, argv):
        args = argv[1:]
        if not args:
            return "Usage: stat <path>"
        info, err = self.fs.stat(args[0])
        if err != ERR_OK:
            return f"stat: {args[0]} not found"
        lines = [f"  {k}: {v}" for k, v in info.items()]
        return "\n".join(lines)

    def _cmd_tree(self, argv):
        args = argv[1:]
        path = args[0] if args else None
        lines = self.fs.tree_view(path)
        if not lines:
            return "(empty)"
        return "\n".join(lines)

    def _cmd_echo(self, argv):
        args = argv[1:]
        return " ".join(args)

    def _cmd_env(self, argv):
        args = argv[1:]
        lines = [f"{k}={v}" for k, v in sorted(self.env.items())]
        return "\n".join(lines)

    def _cmd_set(self, argv):
        args = argv[1:]
        if len(args) < 2:
            return "Usage: set <key> <value>"
        self.env[args[0]] = " ".join(args[1:])
        return ""

    def _cmd_unset(self, argv):
        args = argv[1:]
        if not args:
            return "Usage: unset <key>"
        self.env.pop(args[0], None)
        return ""

    def _cmd_date(self, argv):
        args = argv[1:]
        return time.strftime("%Y-%m-%d %H:%M:%S %Z")

    def _cmd_history(self, argv):
        args = argv[1:]
        if not self.history:
            return "(no history)"
        return "\n".join(f"{i+1}: {h}" for i, h in enumerate(self.history[-20:]))

    def _cmd_oz(self, argv):
        args = argv[1:]
        lines = ["OZ Layer — Extensibility Field"]
        lines.append(f"  Modules loaded: {len(self.oz.modules)}")
        lines.append(f"  Slots defined: {len(self.oz.slots)}")
        lines.append(f"  Hooks active: {sum(len(v) for v in self.oz.hooks.values())}")
        lines.append(f"  Contracts: {len(self.oz.contracts)}")
        lines.append(f"  Ledger entries: {self.ledger.size()}")
        ov = self.oz.total_overhead()
        lines.append(f"  Total overhead: {ov['dispatch_entries']} dispatch, {ov['hooks']} hooks, {ov['slots_occupied']} slot occupations")
        return "\n".join(lines)

    def _cmd_slots(self, argv):
        args = argv[1:]
        lines = ["Slots:"]
        for name, occupants in sorted(self.oz.slots.items()):
            occ = ", ".join(occupants) if occupants else "(empty)"
            lines.append(f"  {name}: {occ}")
        return "\n".join(lines)

    def _cmd_modules(self, argv):
        args = argv[1:]
        lines = ["Modules:"]
        for name, mod in sorted(self.oz.modules.items()):
            desc = getattr(mod, "description", "")
            cmds = ", ".join(getattr(mod, "commands", []))
            lines.append(f"  {name}: {desc} [commands: {cmds}]")
        return "\n".join(lines)

    def _cmd_overhead(self, argv):
        args = argv[1:]
        lines = ["Overhead Accounting:"]
        total = self.oz.total_overhead()
        lines.append(f"  Total modules: {total['modules']}")
        lines.append(f"  Total dispatch entries: {total['dispatch_entries']}")
        lines.append(f"  Total hooks: {total['hooks']}")
        lines.append(f"  Total slot occupations: {total['slots_occupied']}")
        lines.append("")
        lines.append("Per-module:")
        for name, ov in sorted(self.oz.overheads.items()):
            lines.append(f"  {name}: dispatch={ov['dispatch_entries']} hooks={ov['hooks']} slots={ov['slots_occupied']}")
        return "\n".join(lines)

    def _cmd_hooks(self, argv):
        args = argv[1:]
        lines = ["Hooks:"]
        for name, subscribers in sorted(self.oz.hooks.items()):
            subs = ", ".join(subscribers) if subscribers else "(none)"
            lines.append(f"  {name}: {subs}")
        return "\n".join(lines)

    def _cmd_contracts(self, argv):
        args = argv[1:]
        lines = ["Module Contracts:"]
        for name, contract in sorted(self.oz.contracts.items()):
            lines.append(f"  {name}:")
            lines.append(f"    provides: {contract.get('provides', [])}")
            lines.append(f"    requires: {contract.get('requires', [])}")
            lines.append(f"    version: {contract.get('version', '?')}")
        return "\n".join(lines)

    def _cmd_loadmod(self, argv):
        args = argv[1:]
        if not args:
            return "Usage: loadmod <module_name>\nAvailable for reload: fortune_ext"
        return f"loadmod: dynamic loading not supported in this version (modules are compiled-in)"

    def _cmd_unloadmod(self, argv):
        args = argv[1:]
        if not args:
            return "Usage: unloadmod <module_name>"
        err = self.oz.unregister_module(args[0])
        if err != ERR_OK:
            return f"unloadmod: module '{args[0]}' not found"
        return f"Unloaded module '{args[0]}'"

    def _cmd_call(self, argv):
        args = argv[1:]
        if len(args) < 2:
            return "Usage: call <module> <method> [args...]"
        mod_name = args[0]
        method = args[1]
        result, err = self.oz.call_module(mod_name, method)
        if err != ERR_OK:
            return f"call: error calling {mod_name}.{method}"
        return str(result)

    def _cmd_trace(self, argv):
        args = argv[1:]
        n = int(args[0]) if args else 10
        entries = self.ledger.trace(n)
        if not entries:
            return "(no trace entries)"
        lines = ["OZ Ledger Trace:"]
        for e in entries:
            lines.append(f"  [{e['tick']}] {e['command']} -> {e['result_code']} ({e['explanation']})")
        return "\n".join(lines)

    def _cmd_replay(self, argv):
        args = argv[1:]
        entries = self.ledger.replay_info()
        if not entries:
            return "(no replay data)", ERR_OK
        lines = ["Replay Log:"]
        for e in entries:
            lines.append(
                f"  tick={e['tick']} cmd={e['command']} parsed={e['parsed_cmd']} "
                f"args={e['parsed_args']} result={e['result_code']} rev={e['reversible']}")
        return "\n".join(lines), ERR_OK

    def _cmd_reset(self, argv):
        args = argv[1:]
        self._kernel_init()
        return "System reset complete.", ERR_OK

    def _cmd_save(self, argv):
        args = argv[1:]
        fname = args[0] if args else "amosoz.img"
        try:
            data = {
                "fs": json.loads(self.fs.serialize()),
                "env": self.env,
                "version": self.version,
            }
            with open(fname, "w") as f:
                json.dump(data, f)
            return f"System image saved to {fname}"
        except Exception as e:
            return f"save: error: {e}"

    def _cmd_load(self, argv):
        args = argv[1:]
        fname = args[0] if args else "amosoz.img"
        try:
            with open(fname, "r") as f:
                data = json.load(f)
            self.fs.deserialize(json.dumps(data["fs"]))
            self.env.update(data.get("env", {}))
            return f"System image loaded from {fname}"
        except FileNotFoundError:
            return f"load: {fname} not found"
        except Exception as e:
            return f"load: error: {e}"

    def _cmd_exit(self, argv):
        args = argv[1:]
        self.running = False
        return "Shutting down amosOZ.", ERR_OK

    def _cmd_undo(self, argv):
        args = argv[1:]
        for e in reversed(self.ledger.entries):
            if not e.get("reversible") or e.get("result_code") != ERR_OK:
                continue
            path = e.get("undo_path", "")
            if e.get("undo_was_create"):
                self.fs.tree.pop(path, None)
            elif e.get("undo_is_dir"):
                self.fs.tree[path] = self.fs._node("dir", perms="rwxr-xr-x", owner="root")
            else:
                self.fs.tree[path] = self.fs._node("file", content=e.get("undo_content", ""))
            e["reversible"] = 0
            return f"Undid: {e['parsed_cmd']} ({path})", ERR_OK
        return "undo: nothing reversible in ledger", ERR_NOT_FOUND

    def _cmd_whoami(self, argv):
        args = argv[1:]
        return self.user, ERR_OK

    def _cmd_motd(self, argv):
        args = argv[1:]
        out, err = self.kernel_syscall("read", ["/etc/motd"])
        return (out if err == ERR_OK else MOTD), ERR_OK

    def _cmd_syscall(self, argv):
        args = argv[1:]
        if not args:
            return ("Usage: syscall <op> [args...]", ERR_INVALID)
        out, err = self.kernel_syscall(args[0], args[1:])
        return out, err

    def _cmd_which(self, argv):
        args = argv[1:]
        if not args:
            return "Usage: which <name>", ERR_INVALID
        r = self.path_lookup_executable(args[0])
        if not r:
            return f"which: {args[0]}: not found", ERR_NOT_FOUND
        return r, ERR_OK

    def _cmd_exec(self, argv):
        args = argv[1:]
        if not args:
            return "Usage: exec <path> [args...]", ERR_INVALID
        r = self.path_lookup_executable(args[0])
        if not r:
            return f"exec: {args[0]}: not found", ERR_NOT_FOUND
        if not self.is_script_node(r):
            return f"exec: {args[0]}: not a script", ERR_INVALID
        return self.run_script_node(r, ["exec"] + args)

    def _cmd_grep(self, argv):
        args = argv[1:]
        insensitive = False
        ai = 0
        if len(args) > 1 and args[0] == "-i":
            insensitive, ai = True, 1
        if len(args) <= ai:
            return "Usage: grep [-i] PATTERN [file]", ERR_INVALID
        pat = args[ai]
        if len(args) > ai + 1:
            data, err = self.fs.read_to_buf(args[ai + 1])
        elif self.shell_stdin_buf:
            data, err = self.shell_stdin_buf, ERR_OK
        else:
            return "grep: no input", ERR_INVALID
        if err != ERR_OK:
            return "grep: cannot read input", err
        lines = []
        for line in data.split("\n"):
            hay = line.lower() if insensitive else line
            needle = pat.lower() if insensitive else pat
            if needle in hay:
                lines.append(line)
        return ("\n".join(lines) if lines else ""), ERR_OK

    def _cmd_head(self, argv):
        args = argv[1:]
        n, fi = 10, 0
        if len(args) > 2 and args[0] == "-n":
            n, fi = int(args[1]), 2
        if len(args) > fi:
            data, err = self.fs.read_to_buf(args[fi])
        elif self.shell_stdin_buf:
            data, err = self.shell_stdin_buf, ERR_OK
        else:
            return "Usage: head [-n N] [file]", ERR_INVALID
        if err != ERR_OK:
            return "", err
        return "\n".join(data.split("\n")[:n]) + ("\n" if data else ""), ERR_OK

    def _cmd_tail(self, argv):
        args = argv[1:]
        n, fi = 10, 0
        if len(args) > 2 and args[0] == "-n":
            n, fi = int(args[1]), 2
        if len(args) > fi:
            data, err = self.fs.read_to_buf(args[fi])
        elif self.shell_stdin_buf:
            data, err = self.shell_stdin_buf, ERR_OK
        else:
            return "Usage: tail [-n N] [file]", ERR_INVALID
        if err != ERR_OK:
            return "", err
        lines = data.split("\n")
        return "\n".join(lines[-n:]) + ("\n" if lines else ""), ERR_OK

    def _cmd_wc(self, argv):
        args = argv[1:]
        want_l = want_w = want_c = False
        fi = 0
        while fi < len(args) and args[fi].startswith("-"):
            if "l" in args[fi]:
                want_l = True
            if "w" in args[fi]:
                want_w = True
            if "c" in args[fi]:
                want_c = True
            fi += 1
        if not (want_l or want_w or want_c):
            want_l = True
        if fi < len(args):
            data, err = self.fs.read_to_buf(args[fi])
        elif self.shell_stdin_buf:
            data, err = self.shell_stdin_buf, ERR_OK
        else:
            return "Usage: wc [-l|-w|-c] [file]", ERR_INVALID
        if err != ERR_OK:
            return "", err
        lines = data.split("\n") if data else [""]
        words = sum(len(l.split()) for l in lines if l)
        parts = []
        if want_l:
            parts.append(str(len(lines) if data else 0))
        if want_w:
            parts.append(str(words))
        if want_c:
            parts.append(str(len(data)))
        return " ".join(parts), ERR_OK

    def _cmd_test(self, argv):
        args = argv[1:]
        if len(args) < 1:
            return "", ERR_INVALID
        if args and args[-1] == "]":
            args = args[:-1]
        if len(args) >= 2 and args[0] == "-f" and len(args) >= 2:
            r = self.fs.resolve_node(args[1])
            node = self.fs.tree.get(r)
            ok = node and node["type"] == "file"
            return "", ERR_OK if ok else ERR_INVALID
        if len(args) >= 2 and args[0] == "-d":
            r = self.fs.resolve_node(args[1])
            node = self.fs.tree.get(r)
            return "", ERR_OK if node and node["type"] == "dir" else ERR_INVALID
        if len(args) >= 2 and args[0] == "-e":
            return "", ERR_OK if self.fs.resolve_node(args[1]) in self.fs.tree else ERR_INVALID
        if len(args) >= 2 and args[0] == "-z":
            return "", ERR_OK if not args[1] else ERR_INVALID
        if len(args) >= 3 and args[1] == "=":
            return "", ERR_OK if args[0] == args[2] else ERR_INVALID
        return "", ERR_INVALID

    def _cmd_export(self, argv):
        args = argv[1:]
        if not args:
            return "Usage: export KEY=VAL", ERR_INVALID
        if "=" not in args[0]:
            return f"{args[0]}={self.env_get(args[0]) or ''}", ERR_OK
        k, v = args[0].split("=", 1)
        self.env_set(k, v)
        return "", ERR_OK

    def _cmd_source(self, argv):
        args = argv[1:]
        if not args:
            return "Usage: source <file>", ERR_INVALID
        r = self.fs._resolve(args[0])
        node = self.fs.tree.get(r)
        if not node:
            return f"source: {args[0]}: not found", ERR_NOT_FOUND
        body = node.get("content", "")
        if body.startswith("#!"):
            body = body.split("\n", 1)[1] if "\n" in body else ""
        outs = []
        for line in body.split("\n"):
            line = str_trim(line)
            if line and not line.startswith("#"):
                o = self.shell_execute_line(line)
                if o:
                    outs.append(o)
        return "\n".join(outs), ERR_OK

    def _cmd_uptime(self, argv):
        args = argv[1:]
        e = int(time.time() - self.boot_time)
        return f"up {e} seconds, 1 users, load: 0.00 0.00 0.00", ERR_OK

    def _cmd_dmesg(self, argv):
        args = argv[1:]
        return self.boot_log, ERR_OK

    def _cmd_hostname(self, argv):
        args = argv[1:]
        if args:
            return "hostname: read-only virtual hostname", ERR_INVALID
        out, err = self.kernel_syscall("read", ["/etc/hostname"])
        return (out.strip() if err == ERR_OK else self.hw.get("hostname", "amosoz")), ERR_OK

    def _cmd_id(self, argv):
        args = argv[1:]
        u = self.user
        return f"uid={u}({u}) gid={u}({u}) groups={u}", ERR_OK

    def _cmd_true(self, argv):
        args = argv[1:]
        return "", ERR_OK

    def _cmd_false(self, argv):
        args = argv[1:]
        return "", ERR_INVALID

    def _cmd_ln(self, argv):
        args = argv[1:]
        if len(args) < 3 or args[0] != "-s":
            return "Usage: ln -s <target> <linkpath>", ERR_INVALID
        err = self.fs.add_symlink(args[2], args[1])
        if err != ERR_OK:
            return "ln: failed", err
        return "", ERR_OK

    def _cmd_find(self, argv):
        args = argv[1:]
        root, pattern = "/", "*"
        i = 0
        while i < len(args):
            if args[i] == "-name" and i + 1 < len(args):
                pattern = args[i + 1]
                i += 2
            elif args[i].startswith("/"):
                root = args[i]
                i += 1
            else:
                i += 1
        prefix = "/" if root == "/" else root + "/"
        hits = []
        for path in sorted(self.fs.tree):
            if path != root and not path.startswith(prefix):
                continue
            base = path.rsplit("/", 1)[-1]
            if patmatch(pattern, base):
                hits.append(path)
        return ("\n".join(hits) + ("\n" if hits else "")), ERR_OK

    def _cmd_jobs(self, argv):
        args = argv[1:]
        return "[1]+ Running (stub) amossh (pid 1)\n[2]- Running (stub) init (pid 2)", ERR_OK

    def _cmd_fg(self, argv):
        args = argv[1:]
        return "fg: job control stub — foreground is always the shell", ERR_OK

    def _cmd_nohup(self, argv):
        args = argv[1:]
        if not args:
            return "Usage: nohup <command>", ERR_INVALID
        out = self.shell_execute_line(" ".join(args))
        return f"nohup: ignoring SIGHUP (stub)\n{out}", ERR_OK

    def _cmd_spec(self, argv):
        args = argv[1:]
        return (
            f"amosOZ spec {VERSION}\n"
            "features: permissions,syscall,/proc,ledger,undo,shell,scripts,PATH\n"
            "shell: >,>>,|,<  text: grep,head,tail,wc,test  fs: ln,find  meta: export,source\n"
            "stubs: jobs,fg,nohup  oz: fortune oz, /usr/share/amosoz\n"
            "selftest: 45+  canonical: C\n"
        ), ERR_OK

    def _cmd_doctor(self, argv):
        args = argv[1:]
        issues = 0
        lines = ["amosOZ doctor:"]
        if self.validate_module_contracts() != ERR_OK:
            lines.append("  [FAIL] contracts")
            issues += 1
        else:
            lines.append("  [OK] contracts")
        if self.hook_boot_fired <= 0:
            lines.append("  [FAIL] boot hooks")
            issues += 1
        else:
            lines.append(f"  [OK] boot hooks ({self.hook_boot_fired})")
        if "/proc/uptime" not in self.fs.tree:
            lines.append("  [FAIL] /proc")
            issues += 1
        else:
            lines.append("  [OK] /proc")
        if "/usr/share/amosoz/quotes.txt" in self.fs.tree:
            lines.append("  [OK] oz quotes")
        else:
            lines.append("  [WARN] oz quotes")
        lines.append("  STATUS: healthy" if not issues else "  STATUS: needs attention")
        return "\n".join(lines), ERR_INVALID if issues else ERR_OK

    FORTUNES = [
        "The system is the territory.",
        "Every overhead has a name.",
        "A module is a guest with manners.",
        "Slots are finite; ambition is not.",
        "Trace everything. Regret nothing.",
    ]

    def _cmd_fortune(self, argv):
        args = argv[1:]
        if args and args[0] == "oz":
            data, err = self.fs.read_to_buf("/usr/share/amosoz/quotes.txt")
            if err != ERR_OK:
                return "fortune oz: quotes not found", ERR_NOT_FOUND
            qlines = [l for l in data.split("\n") if l.strip()]
            if not qlines:
                return "(no oz quotes)", ERR_OK
            line = qlines[self.fortune_oz_idx % len(qlines)]
            self.fortune_oz_idx += 1
            return line, ERR_OK
        line = self.FORTUNES[self.fortune_idx % len(self.FORTUNES)]
        self.fortune_idx += 1
        return line, ERR_OK

    # ─── Selftest ────────────────────────────────────────────────────────────
    def _cmd_selftest(self, argv):
        args = argv[1:]
        results = []
        passed = 0

        def check(name, cond):
            nonlocal passed
            ok = bool(cond)
            if ok:
                passed += 1
            results.append(f"  [{'PASS' if ok else 'FAIL'}] {name}")
            return ok

        check("boot_state", self.boot_time > 0)
        check("hw_profile", len(self.hw.get("platform", "")) > 0)
        check("version", VERSION == "0.4.0")

        self.fs.write("/tmp/selftest_file", "hello")
        c, _ = self.fs.read("/tmp/selftest_file")
        check("fs_write_read", c == "hello")
        self.fs.append("/tmp/selftest_file", " world")
        c, _ = self.fs.read("/tmp/selftest_file")
        check("fs_append", c == "hello world")
        self.fs.rm("/tmp/selftest_file")
        check("fs_delete", "/tmp/selftest_file" not in self.fs.tree)

        self.fs.mkdir("/tmp/st_dir")
        check("fs_mkdir", "/tmp/st_dir" in self.fs.tree)
        self.fs.rmdir("/tmp/st_dir")
        check("fs_rmdir", "/tmp/st_dir" not in self.fs.tree)

        self.fs.write("/tmp/ptest", "data")
        self.fs.chmod("/tmp/ptest", "r--------")
        info, _ = self.fs.stat("/tmp/ptest")
        check("permission_mutation", info["perms"] == "r--------")
        check("permission_enforced", self.fs.access("/tmp/ptest", "user", "w") == ERR_PERMISSION)
        self.fs.rm("/tmp/ptest")

        self.proc_refresh_all()
        up, _ = self.fs.read("/proc/uptime")
        check("proc_uptime", up and "." in up)
        osr, _ = self.fs.read("/etc/os-release")
        check("os_release", osr and "ID=amosoz" in osr)

        sc, err = self.kernel_syscall("read", ["/etc/hostname"])
        check("syscall_read", err == ERR_OK and "amosoz" in sc)
        check("hook_boot_fired", self.hook_boot_fired > 0)
        check("contract_validation", self.validate_module_contracts() == ERR_OK)

        bid, err = self.mem.alloc(100, "selftest")
        check("mem_alloc", bid and err == ERR_OK)
        check("mem_free", self.mem.free(bid) == ERR_OK)

        check("command_dispatch", "selftest_token" in self.dispatch("echo selftest_token"))

        pid = self.procs.spawn("test_proc")
        check("process_spawn", pid > 0)
        self.procs.kill(pid)
        check("process_kill", pid not in self.procs.processes)
        check("module_registered", "coreutils" in self.oz.modules)
        check("slot_occupation", len(self.oz.slots["shell.commands"]) > 0)
        check("overhead_accounting", self.oz.total_overhead()["modules"] > 0)
        check("oz_layer", len(self.oz.slots) == 10)
        check("oz_ledger", self.ledger.size() > 0)
        check("ledger_parsed", any(e.get("parsed_cmd") for e in self.ledger.entries))

        self.dispatch("write /tmp/undo_me undo_payload")
        u = self.dispatch("undo")
        check("ledger_undo", "Undid" in u or "/tmp/undo_me" not in self.fs.tree)

        self.dispatch("echo treaty_redir > /tmp/shell_redir")
        c, _ = self.fs.read("/tmp/shell_redir")
        check("shell_redirect", c and "treaty_redir" in c)
        self.dispatch("echo appended >> /tmp/shell_redir")
        c, _ = self.fs.read("/tmp/shell_redir")
        check("shell_append", c and "appended" in c)
        check("shell_pipe", "pipe_token" in self.dispatch("echo pipe_token | cat"))
        check("path_which", "/bin/echo" in self.dispatch("which echo"))
        ex = self.dispatch("exec /home/user/hello.amos resonance")
        check("script_exec", "Script greeting" in ex and "resonance" in ex)

        self.dispatch("echo grepme > /tmp/grep.txt")
        check("cmd_grep", "grepme" in self.dispatch("grep grepme /tmp/grep.txt"))
        check("cmd_head", bool(self.dispatch("head -n 1 /etc/motd")))
        wc = self.dispatch("wc -l /etc/motd")
        check("cmd_wc", wc.strip().isdigit() and int(wc.strip()) >= 1)
        _, terr = self._cmd_test(["test", "-f", "/etc/hostname"])
        check("cmd_test", terr == ERR_OK)
        self.dispatch("ln -s /etc/motd /tmp/motd_link")
        check("cmd_ln", self.fs.resolve_node("/tmp/motd_link") in self.fs.tree)
        check("cmd_find", "quotes.txt" in self.dispatch("find /usr -name quotes.txt"))
        self.dispatch("export TIER=v4")
        check("cmd_export", "TIER=v4" in self.dispatch("export TIER"))
        check("fortune_oz", bool(self.dispatch("fortune oz")))
        check("cmd_spec", VERSION in self.dispatch("spec"))
        check("cmd_doctor", "healthy" in self.dispatch("doctor"))
        check("shell_stdin_redirect", "amosoz" in self.dispatch("cat < /etc/hostname"))

        total = len(results)
        out = [f"amosOZ Selftest ({passed}/{total} passed):"] + results
        out.append("\n  ALL TESTS PASSED" if passed == total else f"\n  {total - passed} TESTS FAILED")
        return "\n".join(out), ERR_OK


# ─── Main Loop ───────────────────────────────────────────────────────────────

def main():
    kernel = AmosOZKernel()
    print(MOTD, end="")

    while kernel.running:
        try:
            prompt = f"{kernel.user}@amosoz:{kernel.fs.cwd}$ "
            line = input(prompt)
            result = kernel.dispatch(line)
            if result:
                print(result)
        except (EOFError, KeyboardInterrupt):
            print("\nShutting down amosOZ.")
            break

if __name__ == "__main__":
    main()
