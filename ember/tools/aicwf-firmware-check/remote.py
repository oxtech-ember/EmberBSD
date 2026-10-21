# SPDX-License-Identifier: BSD-2-Clause
# Origin: EmberBSD; bounded GDB remote operations for the owned CPU fixture.
import socket
import time


class Remote:
    def __init__(self, path, deadline, owner):
        self.socket = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        self.deadline = deadline
        self.owner = owner
        self.socket.settimeout(0.2)
        try:
            while True:
                self.remaining()
                try:
                    self.socket.connect(str(path))
                    break
                except (FileNotFoundError, ConnectionRefusedError):
                    time.sleep(0.02)
        except BaseException:
            self.socket.close()
            raise

    def remaining(self):
        if self.owner.poll() is not None:
            raise RuntimeError('QEMU exited before a complete observation')
        value = self.deadline - time.monotonic()
        if value <= 0:
            raise TimeoutError('CPU fixture deadline expired')
        return value

    def close(self):
        self.socket.close()

    def _byte(self):
        while True:
            self.remaining()
            try:
                value = self.socket.recv(1)
            except socket.timeout:
                continue
            if not value:
                raise RuntimeError('GDB remote connection closed')
            return value

    def command(self, text):
        data = text.encode('ascii')
        packet = b'$' + data + b'#' + format(sum(data) & 255, '02x').encode()
        self.socket.sendall(packet)
        if self._byte() != b'+':
            raise RuntimeError('GDB remote did not acknowledge the command')
        while True:
            if self._byte() != b'$':
                raise RuntimeError('Malformed GDB remote reply')
            data = bytearray()
            while True:
                value = self._byte()
                if value == b'#':
                    break
                if len(data) >= 65536:
                    raise ValueError('GDB remote reply exceeded bound')
                data += value
            checksum = self._byte() + self._byte()
            if checksum != format(sum(data) & 255, '02x').encode():
                raise RuntimeError('GDB remote reply checksum mismatch')
            self.socket.sendall(b'+')
            if data.startswith(b'O') and len(data) > 1 and len(data) % 2 == 1 and all(value in b'0123456789abcdefABCDEF' for value in data[1:]):
                # Console output is not an instruction acceptance record.
                continue
            return bytes(data)

    def memory(self, address, size):
        result = bytearray()
        while len(result) < size:
            count = min(512, size - len(result))
            data = self.command('m' + format(address + len(result), 'x') + ',' + format(count, 'x'))
            if len(data) != 2 * count:
                raise RuntimeError('Incomplete GDB memory read')
            result += bytes.fromhex(data.decode('ascii'))
        return bytes(result)

    def breakpoint(self, address):
        if self.command('Z1,' + format(address, 'x') + ',2') != b'OK':
            raise RuntimeError('Non-modifying TCG breakpoint is unavailable')
