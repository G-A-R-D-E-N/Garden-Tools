import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import socket
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--dotnet', default='dotnet')
    parser.add_argument('--build-dir', type=Path)
    parser.add_argument('--network-isolated', action='store_true')
    args = parser.parse_args()
    bindings = Path(__file__).resolve().parents[1]
    feed = bindings / 'packages'
    manifest = json.loads((feed / 'manifest.json').read_text(encoding='utf-8'))['packages']
    if {item['file'] for item in manifest} != {path.name for path in feed.glob('*.nupkg')}:
        raise RuntimeError('Offline package inventory differs from its manifest')
    for item in manifest:
        if hashlib.sha256((feed / item['file']).read_bytes()).hexdigest() != item['sha256']:
            raise RuntimeError(f"Offline package hash mismatch: {item['file']}")
    env = os.environ.copy()
    if args.build_dir:
        listing = subprocess.check_output(
            ['ctest', '--test-dir', str(args.build_dir), '--show-only=json-v1'], text=True)
        test = next(test for test in json.loads(listing)['tests']
                    if test['name'] == 'radicle_csharp_bindings')
        values = next(prop['value'] for prop in test['properties']
                      if prop['name'] == 'ENVIRONMENT')
        env.update(value.split('=', 1) for value in values)
    if args.network_isolated:
        if socket.if_nameindex() != [(1, 'lo')]:
            raise RuntimeError('Offline proof requires a fresh network namespace with only loopback')
        subprocess.run(['ip', 'link', 'set', 'lo', 'up'], check=True)
        try:
            with socket.create_connection(('1.1.1.1', 443), timeout=1):
                raise RuntimeError('External networking is available')
        except OSError as error:
            if error.errno != 101:
                raise
        print('Network proof: loopback only; external connect returned ENETUNREACH', flush=True)
    with tempfile.TemporaryDirectory(prefix='radicle-managed-') as temporary:
        fresh = Path(temporary)
        for name in ('Radicle.Interop', 'Radicle.Interop.Tests'):
            shutil.copytree(bindings / name, fresh / name,
                            ignore=shutil.ignore_patterns('bin', 'obj'))
        for name in ('global.json', 'Directory.Build.props'):
            shutil.copy2(bindings / name, fresh / name)
        config = bindings / 'NuGet.Config'
        for key, name in [('DOTNET_CLI_HOME', 'cli'), ('NUGET_PACKAGES', 'packages'),
                          ('NUGET_HTTP_CACHE_PATH', 'http'), ('NUGET_PLUGINS_CACHE_PATH', 'plugins')]:
            directory = fresh / name
            directory.mkdir()
            env[key] = str(directory)
        env.update(DOTNET_CLI_TELEMETRY_OPTOUT='1', DOTNET_SKIP_FIRST_TIME_EXPERIENCE='1',
                   DOTNET_GENERATE_ASPNET_CERTIFICATE='false',
                   DOTNET_CLI_DO_NOT_USE_MSBUILD_SERVER='1')
        project = str(fresh / 'Radicle.Interop.Tests' / 'Radicle.Interop.Tests.csproj')
        print('Fresh projects and empty package/HTTP/plugin caches; source: radicle-offline', flush=True)
        subprocess.run([args.dotnet, '--version'], cwd=fresh, env=env, check=True)
        subprocess.run([args.dotnet, 'restore', project, '--configfile', str(config),
                        '--locked-mode', '--no-cache', '--verbosity', 'minimal'],
                       cwd=fresh, env=env, check=True)
        subprocess.run([args.dotnet, 'test', project, '--no-restore', '--nologo', '-v', 'minimal',
                        '-p:UseSharedCompilation=false', '--disable-build-servers'],
                       cwd=fresh, env=env, check=True)


if __name__ == '__main__':
    main()
