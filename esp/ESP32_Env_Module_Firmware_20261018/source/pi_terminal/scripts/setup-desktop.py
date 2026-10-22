#!/usr/bin/env python3
"""Install a user-owned SDK; never needs sudo or changes the system Qt."""
import os
import platform
from pathlib import Path
import shutil
import subprocess
import venv

if platform.system() != 'Linux' or platform.machine() != 'x86_64':
    raise SystemExit('This desktop SDK installer targets Linux x86_64; on ARM use native Qt and OPI_QT_ROOT.')
home = Path(os.environ.get('OPI_SDK_HOME', str(Path.home() / '.local/share/orange-pi-terminal-sdk'))).expanduser().resolve()
home.mkdir(parents=True, exist_ok=True)
tools = home / 'tools'
if not (tools / 'bin/python').exists():
    venv.EnvBuilder(with_pip=True).create(tools)
python = str(tools / 'bin/python')
subprocess.run([python, '-m', 'pip', 'install', 'cmake>=3.21,<5', 'aqtinstall==3.3.0', 'cryptography'], check=True)
qt = home / 'qt/6.5.3/gcc_64'
cached = Path('/tmp/opencode/qt/6.5.3/gcc_64')
if not qt.exists() and cached.exists():
    shutil.copytree(cached, qt, symlinks=True)
required = ('Core', 'Quick', 'QuickControls2', 'Bluetooth', 'SerialPort', 'WebEngineQuick', 'TextToSpeech', 'StateMachine', 'ShaderTools')
if any(not (qt / f'lib/cmake/Qt6{module}/Qt6{module}Config.cmake').exists() for module in required):
    subprocess.run([python, '-m', 'aqt', 'install-qt', 'linux', 'desktop', '6.5.3', 'gcc_64',
                    '-O', str(home / 'qt'), '-m', 'qtconnectivity', 'qtserialport',
                    'qtwebengine', 'qtwebchannel', 'qtpositioning', 'qtspeech', 'qtscxml', 'qtshadertools'], check=True)
print(f'SDK ready: {home}')
print('Next: bash scripts/build.sh && bash scripts/run-desktop.sh')
