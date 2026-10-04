#!/usr/bin/env python3
"""Verify ordinary desktop Debug/Release exports with matching templates."""

import argparse
import json
import shutil
import struct
import subprocess
import sys
import tempfile
from pathlib import Path


def read_pack(path):
    """Read only the unencrypted standalone PCK directory used by this fixture."""
    data = path.read_bytes()
    magic, version, _major, _minor, _patch, flags = struct.unpack_from("<6I", data)
    assert magic == 0x43504447 and version in (3, 4) and not flags & 1
    file_base, directory = struct.unpack_from("<QQ", data, 24)
    count = struct.unpack_from("<I", data, directory)[0]
    position = directory + 4
    files = {}
    for _ in range(count):
        length = struct.unpack_from("<I", data, position)[0]
        position += 4
        name = data[position : position + length].rstrip(b"\0").decode()
        position += length
        offset, size = struct.unpack_from("<QQ", data, position)
        entry_flags = struct.unpack_from("<I", data, position + 32)[0]
        assert not entry_flags
        position += 36
        assert file_base + offset + size <= len(data)
        files[name.removeprefix("res://")] = data[file_base + offset : file_base + offset + size]
    return files


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("editor", type=Path)
    parser.add_argument("--debug-template", type=Path)
    parser.add_argument("--release-template", type=Path)
    parser.add_argument("--architecture", choices=("x86_64", "x86_32", "arm64"), default="x86_64")
    parser.add_argument("--platform", choices=("windows", "linux"), default="windows")
    parser.add_argument("--wsl-distribution", help="Run a Linux export in this WSL distribution from a Windows host.")
    parser.add_argument("--log-directory", type=Path, required=True)
    args = parser.parse_args()
    assert args.debug_template or args.release_template, "Select at least one target profile."
    assert not args.wsl_distribution or (sys.platform == "win32" and args.platform == "linux")
    editor = str(args.editor.resolve())
    args.log_directory.mkdir(parents=True, exist_ok=True)

    def run(label, command, success=True):
        result = subprocess.run(command, capture_output=True, text=True, timeout=120)
        log = result.stdout + result.stderr
        (args.log_directory / f"{label}.log").write_text(log, encoding="utf-8")
        if success:
            assert result.returncode == 0, (label, log[-6000:])
            assert not any(marker in log for marker in ["ERROR:", "WARNING:", "SCRIPT ERROR:"]), (label, log[-6000:])
        else:
            assert result.returncode != 0, (label, log[-6000:])
        return log

    with tempfile.TemporaryDirectory(prefix="godot-compiled-export-") as temporary:
        root = Path(temporary)
        project = root / "project"
        shutil.copytree(Path(__file__).parent / "export_runtime", project, ignore=shutil.ignore_patterns(".godot"))
        preset = project / "export_presets.cfg"
        base_preset = preset.read_text(encoding="utf-8")
        base_preset = base_preset.replace(
            'binary_format/architecture="x86_64"', f'binary_format/architecture="{args.architecture}"'
        )
        preset_name = "Windows Compiled" if args.platform == "windows" else "Linux Compiled"
        if args.platform == "linux":
            base_preset = base_preset.replace('platform="Windows Desktop"', 'platform="Linux"').replace(
                "Windows Compiled", preset_name
            )
            base_preset = base_preset.replace("codesign/enable=false\n", "").replace(
                "application/modify_resources=false\n", ""
            )
        template_options = ""
        for profile, template in [("debug", args.debug_template), ("release", args.release_template)]:
            if template:
                template_options += f"\ncustom_template/{profile}={json.dumps(template.resolve().as_posix())}\n"
        preset.write_text(base_preset + template_options, encoding="utf-8")
        with (project / "project.godot").open("a", encoding="utf-8") as file:
            file.write("\n[debug]\ngdscript/compiled_load_profile=true\n")
        run("import", [editor, "--headless", "--path", str(project), "--editor", "--import"])
        for profile, template in [("debug", args.debug_template), ("release", args.release_template)]:
            if not template:
                continue
            for compression in (0, 1):
                preset.write_text(
                    (base_preset + template_options).replace(
                        "script/compiled_compression=1", f"script/compiled_compression={compression}"
                    ),
                    encoding="utf-8",
                )
                stem = f"{profile}-{compression}"
                extension = "exe" if args.platform == "windows" else args.architecture
                executable = root / f"{stem}.{extension}"
                run(
                    stem + "-export",
                    [
                        editor,
                        "--headless",
                        "--path",
                        str(project),
                        f"--export-{profile}",
                        preset_name,
                        str(executable),
                    ],
                )
                files = read_pack(executable.with_suffix(".pck"))
                assert not any(name.endswith((".gd", ".gdc")) for name in files), list(files)
                manifest = json.loads(files[".godot/compiled/gdscript.manifest.json"])
                assert manifest["debug"] == (profile == "debug")
                assert manifest["compression"] == ("zstd" if compression else "none")
                assert len(manifest["script_paths"]) == 4, manifest
                for source in ("main", "peer", "data", "startup"):
                    assert b"gdscript.gdbc" in files[f"{source}.gd.remap"]
                if args.wsl_distribution:
                    runner = ["wsl", "-d", args.wsl_distribution, "--exec"]
                    linux_path = subprocess.check_output(
                        runner + ["wslpath", "-a", executable.as_posix()], text=True, timeout=15
                    ).strip()
                    command = runner + [linux_path]
                else:
                    command = [str(executable)]
                log = run(stem + "-run", command + ["--headless", "--", "--verify"])
                report = next(
                    json.loads(line.split(" ", 1)[1])
                    for line in log.splitlines()
                    if line.startswith("COMPILED_EXPORT_RESULT ")
                )
                assert report == {"compiled": True, "failures": [], "architecture": args.architecture}, report
                profiles = [
                    json.loads(line.split(" ", 1)[1])
                    for line in log.splitlines()
                    if line.startswith("GDSCRIPT_BYTECODE_LOAD ")
                ]
                if profile == "debug":
                    assert profiles and all(
                        item["succeeded"] and item["source_pipeline_entries"] == 0 for item in profiles
                    )
                else:
                    assert not profiles
                print(
                    f"PASS {args.platform} {args.architecture} {profile}, "
                    f"compression={manifest['compression']}, source-free runtime"
                )
        preset.write_text(
            (base_preset + template_options).replace('exclude_filter=""', 'exclude_filter="peer.gd"'),
            encoding="utf-8",
        )
        log = run(
            "missing-dependency",
            [
                editor,
                "--headless",
                "--path",
                str(project),
                "--export-pack",
                preset_name,
                str(root / "invalid.pck"),
            ],
            success=False,
        )
        assert "absent from the export selection" in log, log[-6000:]
        print("PASS missing selected dependency is rejected")


if __name__ == "__main__":
    main()
