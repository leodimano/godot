#!/usr/bin/env python3
"""Exercise remote stepping and frame inspection in a precompiled Debug export."""

import argparse
import json
import os
import shutil
import socket
import subprocess
import time
from pathlib import Path

from test_export_runtime import read_pack


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("editor", type=Path)
    parser.add_argument("--debug-template", type=Path, required=True)
    parser.add_argument("--log-directory", type=Path, required=True)
    args = parser.parse_args()
    editor = str(args.editor.resolve())
    root = args.log_directory.resolve()
    root.mkdir(parents=True, exist_ok=False)
    fixture = Path(__file__).parent / "debug_runtime"
    project = root / "project"
    shutil.copytree(fixture, project, ignore=shutil.ignore_patterns(".godot"))
    (project / "export_presets.cfg").write_text(
        '[preset.0]\nname="Windows Compiled"\nplatform="Windows Desktop"\n'
        'export_filter="all_resources"\ninclude_filter=""\nexclude_filter=""\nscript_export_mode=3\n'
        "[preset.0.options]\nscript/compiled_compression=1\n"
        f"custom_template/debug={json.dumps(args.debug_template.resolve().as_posix())}\n",
        encoding="utf-8",
    )
    process_options = {"creationflags": subprocess.CREATE_NO_WINDOW} if os.name == "nt" else {}

    def run(label, command):
        result = subprocess.run(command, capture_output=True, text=True, timeout=120, **process_options)
        log = result.stdout + result.stderr
        (root / f"{label}.log").write_text(log, encoding="utf-8")
        assert result.returncode == 0, (label, log[-4000:])
        assert not any(marker in log for marker in ("ERROR:", "WARNING:", "SCRIPT ERROR:")), (label, log[-4000:])

    run("import", [editor, "--headless", "--editor", "--path", str(project), "--import"])
    executable = root / "debugger.exe"
    run("export", [editor, "--headless", "--path", str(project), "--export-debug", "Windows Compiled", str(executable)])
    files = read_pack(executable.with_suffix(".pck"))
    assert not any(name.endswith((".gd", ".gdc")) for name in files)
    assert json.loads(files[".godot/compiled/gdscript.manifest.json"])["debug"] is True
    with socket.socket() as reserved:
        reserved.bind(("127.0.0.1", 0))
        port = reserved.getsockname()[1]
    result_path = root / "result.json"
    host = None
    runtime = None
    try:
        with (
            (root / "host.log").open("w", encoding="utf-8") as host_log,
            (root / "runtime.log").open("w", encoding="utf-8") as runtime_log,
        ):
            host = subprocess.Popen(
                [
                    editor,
                    "--headless",
                    "--path",
                    str(project),
                    "--script",
                    str(Path(__file__).with_name("debugger_host.gd").resolve()),
                    "--",
                    str(port),
                    str(result_path),
                ],
                stdout=host_log,
                stderr=subprocess.STDOUT,
                **process_options,
            )
            deadline = time.monotonic() + 20
            while "COMPILED_DEBUG_LISTENING" not in (root / "host.log").read_text(encoding="utf-8"):
                assert host.poll() is None and time.monotonic() < deadline, (root / "host.log").read_text(
                    encoding="utf-8"
                )
                time.sleep(0.05)
            runtime = subprocess.Popen(
                [str(executable), "--headless", "--remote-debug", f"tcp://127.0.0.1:{port}"],
                stdout=runtime_log,
                stderr=subprocess.STDOUT,
                **process_options,
            )
            assert host.wait(timeout=100) == 0, (root / "host.log").read_text(encoding="utf-8")
            assert runtime.wait(timeout=20) == 0
    finally:
        for process in (runtime, host):
            if process is not None and process.poll() is None:
                process.terminate()
                process.wait(timeout=10)
    result = json.loads(result_path.read_text(encoding="utf-8"))
    assert result["passed"] and result["stops"] == 3, result
    runtime_log = (root / "runtime.log").read_text(encoding="utf-8")
    completion = [
        json.loads(line.split(" ", 1)[1])
        for line in runtime_log.splitlines()
        if line.startswith("COMPILED_DEBUG_RESULT ")
    ]
    assert completion == [{"compiled": True, "value": 43}], completion
    profiles = [
        json.loads(line.split(" ", 1)[1])
        for line in runtime_log.splitlines()
        if line.startswith("GDSCRIPT_BYTECODE_LOAD ")
    ]
    assert profiles and all(profile["succeeded"] and profile["source_pipeline_entries"] == 0 for profile in profiles)
    for name in ("host", "runtime"):
        log = (root / f"{name}.log").read_text(encoding="utf-8")
        assert not any(marker in log for marker in ("ERROR:", "WARNING:", "SCRIPT ERROR:")), (name, log[-4000:])
    print(
        "PASS: precompiled breakpoint, two steps, logical paths/lines, locals/members, continue, zero source compilation"
    )


if __name__ == "__main__":
    main()
