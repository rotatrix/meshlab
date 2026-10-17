"""Check the deployed Qt TLS runtime without a GUI, network or user settings."""
import ctypes
from pathlib import Path
import sys


def check(appdir):
    lib = Path(appdir).resolve() / 'usr/lib'
    crypto = list(lib.glob('libcrypto.so.*'))
    if len(crypto) != 1:
        raise RuntimeError(f'Expected one bundled crypto library, found {crypto}')
    ssl = crypto[0].with_name(crypto[0].name.replace('libcrypto', 'libssl', 1))
    if not ssl.is_file():
        raise RuntimeError(f'Missing bundled SSL library: {ssl}')
    # Match MeshLab startup: its linked crypto is loaded before Qt opens SSL.
    ctypes.CDLL(str(crypto[0]), mode=ctypes.RTLD_GLOBAL)
    ctypes.CDLL(str(ssl), mode=ctypes.RTLD_GLOBAL)
    qt = ctypes.CDLL(str(lib / 'libQt5Network.so.5'))
    supports_ssl = qt._ZN10QSslSocket11supportsSslEv
    supports_ssl.restype = ctypes.c_bool
    if not supports_ssl():
        raise RuntimeError('Packaged QSslSocket::supportsSsl() returned false')
    print('Packaged Qt TLS support: OK')


if __name__ == '__main__':
    check(sys.argv[1])
