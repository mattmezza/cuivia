#!/usr/bin/env python3
"""Bundle Homebrew/system dev dependencies into a relocatable macOS app."""
import plistlib
import shutil
import subprocess
import sys
from pathlib import Path


def run(*args):
    return subprocess.check_output(args, text=True)


def bundle(executable, destination):
    contents = destination / 'Contents'
    binaries = contents / 'MacOS'
    frameworks = contents / 'Frameworks'
    binaries.mkdir(parents=True, exist_ok=True)
    frameworks.mkdir(parents=True, exist_ok=True)
    target = binaries / 'sprinkle-trivia'
    shutil.copy2(executable, target)
    with (contents / 'Info.plist').open('wb') as stream:
        plistlib.dump({
            'CFBundleExecutable': target.name,
            'CFBundleIdentifier': 'co.merola.sprinkle-trivia',
            'CFBundleName': 'Sprinkle Trivia',
            'CFBundlePackageType': 'APPL',
            'CFBundleShortVersionString': '1.0',
            'CFBundleVersion': '1',
            'NSHighResolutionCapable': True,
        }, stream)
    copied = {}

    def collect(binary, original):
        for line in run('otool', '-L', str(binary)).splitlines()[1:]:
            dependency = line.strip().split(' (', 1)[0]
            if dependency == '@rpath/' + binary.name:
                continue
            if dependency.startswith(('/usr/lib/', '/System/Library/')):
                continue
            if dependency.startswith(('@rpath/', '@loader_path/')):
                source = original.parent / dependency.split('/', 1)[1]
            else:
                source = Path(dependency)
            if not source.is_absolute() or not source.is_file():
                raise RuntimeError(f'Unresolved dependency: {dependency} in {binary}')
            name = source.name
            if name in copied and copied[name] != source:
                raise RuntimeError(f'Duplicate library name: {name}')
            library = frameworks / name
            if name not in copied:
                copied[name] = source
                shutil.copy2(source, library)
                library.chmod(0o755)
                run('install_name_tool', '-id', '@rpath/' + name, str(library))
                collect(library, source)
            run('install_name_tool', '-change', dependency,
                '@executable_path/../Frameworks/' + name, str(binary))

    collect(target, executable.resolve())
    for library in frameworks.iterdir():
        if library.name not in copied:
            library.unlink()
            continue
        run('codesign', '--force', '--sign', '-', str(library))
    run('codesign', '--force', '--sign', '-', str(destination))
    run('codesign', '--verify', '--deep', '--strict', str(destination))
    print(destination)


if __name__ == '__main__':
    bundle(Path(sys.argv[1]), Path(sys.argv[2]))
