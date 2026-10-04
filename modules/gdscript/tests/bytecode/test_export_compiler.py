#!/usr/bin/env python3
"""Run isolated export-worker checks with a fully built editor."""

import argparse
import hashlib
import json
import subprocess
import tempfile
from pathlib import Path


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("editor", type=Path)
    args = parser.parse_args()
    project = Path(__file__).resolve().parent / "export_compiler"
    source_hash = hashlib.sha256((project / "subject.gd").read_bytes()).hexdigest()
    with tempfile.TemporaryDirectory(prefix="godot-export-compiler-") as temporary:
        output = Path(temporary)
        for debug, valid_hash in [(True, True), (False, True), (True, False)]:
            bundle = output / "scripts.gdbc"
            response = output / "response.json"
            request = output / "request.json"
            bundle.unlink(missing_ok=True)
            response.unlink(missing_ok=True)
            request.write_text(
                json.dumps({
                    "sources": {"res://subject.gd": source_hash if valid_hash else "invalid"},
                    "bundle": str(bundle),
                    "response": str(response),
                    "debug": debug,
                }),
                encoding="utf-8",
            )
            command = [
                str(args.editor.resolve()),
                "--headless",
                "--path",
                str(project),
                "--main-loop",
                "GDScriptExportCompiler",
                "--",
                "--gdscript-compile-only",
                "--gdscript-export-request",
                str(request),
            ]
            if not debug:
                command.append("--gdscript-target-release")
            result = subprocess.run(command, capture_output=True, text=True, timeout=60)
            log = result.stdout + result.stderr
            assert not any(marker in log for marker in ["ERROR:", "WARNING:", "SCRIPT ERROR:"]), log
            report = json.loads(response.read_text(encoding="utf-8"))
            assert report["debug"] == debug, report
            assert report["compiler_mode"] == "isolated_native_main_loop", report
            if valid_hash:
                assert result.returncode == 0 and report["error"] == 0, (report, log)
                assert report["script_paths"] == ["res://subject.gd"], report
                assert report["source_pipeline_entries"] > 0, report
                assert bundle.read_bytes()[:4] == b"GDBI"
            else:
                assert result.returncode != 0 and report["error"] != 0, report
                assert "differs from export input" in report["failure"], report
                assert not bundle.exists()
            print(f"PASS debug={debug} valid_source_hash={valid_hash}")


if __name__ == "__main__":
    main()
