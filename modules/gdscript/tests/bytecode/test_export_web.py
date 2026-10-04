#!/usr/bin/env python3
"""Exercise standard precompiled Web exports in a headless Chromium browser.

Requires Playwright for Python and a Chromium installation. Serves only the
temporary exported fixture on loopback, with headers required by threaded Web
templates. No user browser profile or game project is touched.
"""

import argparse
import functools
import http.server
import json
import shutil
import subprocess
import tempfile
import threading
from pathlib import Path

from playwright.sync_api import sync_playwright
from test_export_runtime import read_pack


class ExportHandler(http.server.SimpleHTTPRequestHandler):
    def end_headers(self):
        self.send_header("Cross-Origin-Opener-Policy", "same-origin")
        self.send_header("Cross-Origin-Embedder-Policy", "require-corp")
        super().end_headers()

    def log_message(self, *_args):
        pass


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("editor", type=Path)
    parser.add_argument("--debug-template", type=Path)
    parser.add_argument("--release-template", type=Path)
    parser.add_argument("--browser", type=Path, help="Chromium executable; otherwise use Playwright's installation.")
    parser.add_argument("--threads", action="store_true")
    parser.add_argument("--architecture", choices=("wasm32", "wasm64"), default="wasm32")
    parser.add_argument("--log-directory", type=Path, required=True)
    args = parser.parse_args()
    assert args.debug_template or args.release_template, "Select at least one target profile."
    args.log_directory.mkdir(parents=True, exist_ok=False)
    editor = str(args.editor.resolve())
    results = []

    def run(label, command):
        result = subprocess.run(command, capture_output=True, text=True, timeout=180)
        log = result.stdout + result.stderr
        (args.log_directory / f"{label}.log").write_text(log, encoding="utf-8")
        diagnostics = [line for line in log.splitlines() if any(marker in line for marker in ("ERROR:", "WARNING:"))]
        assert result.returncode == 0, (label, result.returncode, diagnostics[:12] or log[-1200:])
        assert not diagnostics, (label, diagnostics[:12])

    with tempfile.TemporaryDirectory(prefix="godot-compiled-web-") as temporary:
        root = Path(temporary)
        project = root / "project"
        output = root / "export"
        output.mkdir()
        shutil.copytree(Path(__file__).parent / "export_runtime", project, ignore=shutil.ignore_patterns(".godot"))
        with (project / "project.godot").open("a", encoding="utf-8") as file:
            file.write("\n[debug]\ngdscript/compiled_load_profile=true\n")
        run("import", [editor, "--headless", "--path", str(project), "--editor", "--import"])
        handler = functools.partial(ExportHandler, directory=str(output))
        server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), handler)
        server_thread = threading.Thread(target=server.serve_forever, daemon=True)
        server_thread.start()
        try:
            with sync_playwright() as playwright:
                browser = playwright.chromium.launch(
                    executable_path=str(args.browser.resolve()) if args.browser else None,
                    headless=True,
                    args=["--use-angle=swiftshader", "--enable-unsafe-swiftshader"],
                )
                try:
                    for profile, template in (("debug", args.debug_template), ("release", args.release_template)):
                        if not template:
                            continue
                        for compression in (0, 1):
                            stem = f"{profile}-{compression}"
                            preset = (
                                '[preset.0]\nname="Web Compiled"\nplatform="Web"\n'
                                'runnable=true\nexport_filter="all_resources"\nscript_export_mode=3\n'
                                'include_filter=""\nexclude_filter=""\nexport_path=""\n'
                                f"[preset.0.options]\ncustom_template/{profile}={json.dumps(template.resolve().as_posix())}\n"
                                f"variant/thread_support={str(args.threads).lower()}\n"
                                f"script/compiled_compression={compression}\n"
                            )
                            (project / "export_presets.cfg").write_text(preset, encoding="utf-8")
                            html = output / f"{stem}.html"
                            run(
                                stem + "-export",
                                [
                                    editor,
                                    "--headless",
                                    "--path",
                                    str(project),
                                    f"--export-{profile}",
                                    "Web Compiled",
                                    str(html),
                                ],
                            )
                            files = read_pack(html.with_suffix(".pck"))
                            assert not any(name.endswith((".gd", ".gdc")) for name in files), list(files)
                            manifest = json.loads(files[".godot/compiled/gdscript.manifest.json"])
                            assert manifest["debug"] == (profile == "debug"), manifest
                            assert manifest["compression"] == ("zstd" if compression else "none"), manifest
                            assert len(manifest["script_paths"]) == 4, manifest
                            console = []
                            page_errors = []
                            page = browser.new_page()
                            page.on("console", lambda message: console.append(message.text))
                            page.on("pageerror", lambda error: page_errors.append(str(error)))
                            try:
                                with page.expect_console_message(
                                    predicate=lambda message: message.text.startswith("COMPILED_EXPORT_RESULT "),
                                    timeout=120000,
                                ) as completion:
                                    page.goto(f"http://127.0.0.1:{server.server_port}/{html.name}")
                                report = json.loads(completion.value.text.split(" ", 1)[1])
                                assert report == {
                                    "compiled": True,
                                    "failures": [],
                                    "architecture": args.architecture,
                                }, report
                                assert not page_errors, page_errors
                                assert not any("ERROR:" in line or "WARNING:" in line for line in console), console
                                profiles = [
                                    json.loads(line.split(" ", 1)[1])
                                    for line in console
                                    if line.startswith("GDSCRIPT_BYTECODE_LOAD ")
                                ]
                                if profile == "debug":
                                    assert profiles and all(
                                        item["succeeded"] and item["source_pipeline_entries"] == 0 for item in profiles
                                    ), profiles
                                else:
                                    assert not profiles, profiles
                                results.append({"profile": profile, "compression": manifest["compression"], **report})
                                print(
                                    f"PASS Web {args.architecture} {profile}, threads={args.threads}, {manifest['compression']}"
                                )
                            finally:
                                (args.log_directory / f"{stem}-browser.log").write_text(
                                    "\n".join(console + page_errors), encoding="utf-8"
                                )
                                page.close()
                finally:
                    browser.close()
        finally:
            server.shutdown()
            server.server_close()
            server_thread.join()
    (args.log_directory / "summary.json").write_text(json.dumps(results, indent=2) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
