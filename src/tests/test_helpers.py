import os
from os import path
import sys
import errno
import socket
import subprocess
from subprocess import PIPE
import signal
import uuid
from functools import wraps
import tempfile
from shutil import copyfile, move

def get_open_port():
    sock = socket.socket(socket.AF_INET)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)

    sock.bind(('', 0))
    port = sock.getsockname()[1]
    sock.close()
    return port


def make_sure_path_exists(target_path):
    try:
        os.makedirs(target_path)
    except OSError as exception:
        if exception.errno != errno.EEXIST:
            raise


def print_cmd(cmd):
    if isinstance(cmd, list):
        cmd_to_print = ' '.join(cmd).strip()
    elif isinstance(cmd, (str, unicode)):
        cmd_to_print = cmd.strip()
    else:
        cmd_to_print = ''

    if cmd_to_print:
        sys.stderr.write('$ %s\n' % cmd_to_print)


def call(cmd, **kwargs):
    print_cmd(cmd)
    return subprocess.call(cmd, **kwargs)


def check_call(cmd, **kwargs):
    print_cmd(cmd)
    return subprocess.check_call(cmd, **kwargs)


def check_output(cmd, **kwargs):
    print_cmd(cmd)
    return subprocess.check_output(cmd, **kwargs)


def Popen(cmd, **kwargs):
    print_cmd(cmd)
    return subprocess.Popen(cmd, **kwargs)


class TimeoutError(Exception):
    pass


# from https://stackoverflow.com/a/2282656
def timeout(seconds=10, error_message=os.strerror(errno.ETIME)):
    def decorator(func):
        def _handle_timeout(signum, frame):
            raise TimeoutError(error_message)

        def wrapper(*args, **kwargs):
            signal.signal(signal.SIGALRM, _handle_timeout)
            signal.alarm(seconds)
            try:
                result = func(*args, **kwargs)
            finally:
                signal.alarm(0)
            return result

        return wraps(func)(wrapper)

    return decorator


def create_tmp_and_move_to(directory, ext='.ext'):
    fd, tmp_filepath = tempfile.mkstemp()
    os.close(fd)

    tmp_filename = path.basename(tmp_filepath) + ext

    new_filepath = path.join(directory, tmp_filename)
    move(tmp_filepath, new_filepath)

    return tmp_filename


def copy_move(src_path, dst_path):
    tmp_path = path.join(tempfile.gettempdir(), path.basename(src_path))
    copyfile(src_path, tmp_path)
    move(tmp_path, dst_path)


def touch(filename, times=None):
    with open(filename, 'a'):
        os.utime(filename, times)
