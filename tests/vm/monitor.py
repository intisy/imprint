"""Sends QEMU monitor commands over a unix socket: monitor.py SOCKET COMMAND..."""
import socket
import sys
import time

connection = socket.socket(socket.AF_UNIX)
for attempt in range(60):
    try:
        connection.connect(sys.argv[1])
        break
    except OSError:
        time.sleep(1)
time.sleep(0.3)
connection.recv(65536)
for command in sys.argv[2:]:
    connection.sendall((command + "\n").encode())
    time.sleep(0.15)
    connection.recv(65536)
