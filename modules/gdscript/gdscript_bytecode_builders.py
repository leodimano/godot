"""Build-time compatibility identity for GDScript bytecode."""

import hashlib
from pathlib import Path


def source_fingerprint(records):
    digest = hashlib.sha256(b"godot-gdscript-vm-sources-v1\0")
    for name, content in sorted(records):
        name_bytes = name.encode("utf-8")
        canonical = content.replace(b"\r\n", b"\n")
        digest.update(len(name_bytes).to_bytes(8, "little"))
        digest.update(name_bytes)
        digest.update(len(canonical).to_bytes(8, "little"))
        digest.update(canonical)
    return digest.hexdigest()


def fingerprint_files(source):
    root = Path(__file__).resolve().parents[2]
    records = [
        (Path(str(item)).resolve().relative_to(root).as_posix(), Path(str(item)).read_bytes()) for item in source
    ]
    return source_fingerprint(records)


def make_compatibility_header(target, source, env):
    fingerprint = source[0].read()
    with open(str(target[0]), "w", encoding="utf-8", newline="\n") as output:
        output.write("// Generated shared VM source identity. Do not edit.\n#pragma once\n")
        output.write(f'#define GDSCRIPT_BYTECODE_VM_SOURCE_SHA256 "{fingerprint}"\n')
