#!/usr/bin/env python3
"""
Arduino CLI wrapper for serial monitoring with automatic port management.
Monitors serial port continuously, but closes it during compile/upload operations.
"""

import argparse
import json
import os
import signal
import socket
import subprocess
import sys
import threading
import time
from pathlib import Path

try:
    import serial
    import serial.tools.list_ports
except ImportError:
    print("Error: pyserial is required. Install with: pip install pyserial", file=sys.stderr)
    sys.exit(1)

SOCKET_PATH = "/tmp/arduwrap.sock"

# Project root is parent of scripts directory
PROJECT_ROOT = Path(__file__).parent.parent
ARDUINO_CONFIG = PROJECT_ROOT / "arduino-cli.yaml"


class SerialMonitor:
    MAX_LOG_SIZE = 64 * 1024  # 64KB log buffer
    RECONNECT_INTERVAL = 0.5  # seconds between reconnect attempts (normal)
    FAST_RECONNECT_INTERVAL = 0.05  # seconds between reconnect attempts (after resume)

    def __init__(self, port, baud):
        self.port = port
        self.baud = baud
        self.serial_conn = None
        self.running = False
        self.paused = False  # True during compile/upload
        self.eager_reconnect = False  # Try to reconnect rapidly after resume
        self.lock = threading.Lock()
        self.log_buffer = bytearray()
        self.log_lock = threading.Lock()

    def open(self):
        """Open serial port for monitoring."""
        with self.lock:
            if self.serial_conn is not None:
                return True
            try:
                self.serial_conn = serial.Serial(self.port, self.baud, timeout=1)
                print(f"[Monitor] Opened {self.port} at {self.baud} baud", file=sys.stderr)
                return True
            except Exception as e:
                # Don't print error for expected "port not found" during reconnect
                return False

    def close(self):
        """Close serial port."""
        with self.lock:
            if self.serial_conn is not None:
                try:
                    self.serial_conn.close()
                    print(f"[Monitor] Closed {self.port}", file=sys.stderr)
                except Exception as e:
                    print(f"[Monitor] Error closing port: {e}", file=sys.stderr)
                finally:
                    self.serial_conn = None

    def pause(self):
        """Pause monitoring (for compile/upload)."""
        self.paused = True
        self.close()

    def resume(self):
        """Resume monitoring after compile/upload."""
        self.paused = False
        self.eager_reconnect = True  # Use fast reconnect interval until connected

    def monitor_loop(self):
        """Monitor serial port and print data to stdout, with auto-reconnect."""
        last_reconnect_attempt = 0

        while self.running:
            # If paused (during compile), just wait
            if self.paused:
                time.sleep(0.1)
                continue

            # Try to open if not connected
            with self.lock:
                is_connected = self.serial_conn is not None and self.serial_conn.is_open

            if not is_connected:
                now = time.time()
                # Use fast interval after resume until connected, then normal interval
                interval = self.FAST_RECONNECT_INTERVAL if self.eager_reconnect else self.RECONNECT_INTERVAL
                if now - last_reconnect_attempt >= interval:
                    last_reconnect_attempt = now
                    if self.open():
                        print(f"[Monitor] Reconnected to {self.port}", file=sys.stderr)
                        self.eager_reconnect = False  # Connected, back to normal mode
                    # else: keep trying (fast if eager_reconnect, slow otherwise)
                time.sleep(0.02)  # Short sleep for responsive reconnect
                continue

            # Read data
            try:
                with self.lock:
                    if self.serial_conn is None or not self.serial_conn.is_open:
                        continue
                    if self.serial_conn.in_waiting > 0:
                        data = self.serial_conn.read(self.serial_conn.in_waiting)
                        sys.stdout.buffer.write(data)
                        sys.stdout.buffer.flush()
                        # Buffer the log
                        with self.log_lock:
                            self.log_buffer.extend(data)
                            # Trim if too large
                            if len(self.log_buffer) > self.MAX_LOG_SIZE:
                                self.log_buffer = self.log_buffer[-self.MAX_LOG_SIZE:]
            except (OSError, serial.SerialException) as e:
                # Device disconnected
                print(f"[Monitor] Disconnected: {e}", file=sys.stderr)
                self.close()
                continue
            except Exception as e:
                if self.running:
                    print(f"[Monitor] Error: {e}", file=sys.stderr)
                self.close()
                continue

            time.sleep(0.01)

    def get_log(self):
        """Get buffered log content."""
        with self.log_lock:
            return bytes(self.log_buffer)

    def clear_log(self):
        """Clear log buffer."""
        with self.log_lock:
            self.log_buffer.clear()

    def is_open(self):
        """Check if serial port is open."""
        with self.lock:
            return self.serial_conn is not None and self.serial_conn.is_open


class ArduinoWrapperServer:
    def __init__(self, port, baud):
        self.monitor = SerialMonitor(port, baud)
        self.server_socket = None
        self.running = False
        self.monitor_thread = None

    def start_monitor(self):
        """Start serial monitoring in background thread."""
        self.monitor.running = True
        self.monitor.open()
        self.monitor_thread = threading.Thread(target=self.monitor.monitor_loop, daemon=True)
        self.monitor_thread.start()

    def stop_monitor(self):
        """Stop serial monitoring completely."""
        self.monitor.running = False
        self.monitor.close()

    def pause_monitor(self):
        """Pause monitoring (for compile/upload)."""
        self.monitor.pause()

    def resume_monitor(self):
        """Resume monitoring after compile/upload."""
        self.monitor.resume()

    def handle_compile_request(self, request_data):
        """Handle compile/upload request."""
        try:
            args = request_data.get("args", [])
            if not args:
                return {"success": False, "error": "No arguments provided"}

            # Always use server's port (remove any user-specified port)
            filtered_args = []
            skip_next = False
            for arg in args:
                if skip_next:
                    skip_next = False
                    continue
                if arg in ("-p", "--port"):
                    skip_next = True  # Skip the next arg (port value)
                    continue
                if arg.startswith("-p=") or arg.startswith("--port="):
                    continue  # Skip --port=xxx format
                filtered_args.append(arg)
            args = ["-p", self.monitor.port] + filtered_args

            # Pause monitor before compile and clear log for fresh boot log
            self.pause_monitor()
            self.monitor.clear_log()

            # Run arduino-cli compile (--upload is already added by cmd_compile if needed)
            monitor_resumed = False
            try:
                cmd = ["arduino-cli"]
                # Use project-specific config if available
                if ARDUINO_CONFIG.exists():
                    cmd.extend(["--config-file", str(ARDUINO_CONFIG)])
                cmd.extend(["compile"] + args)
                print(f"[Compile] Running: {' '.join(cmd)}", file=sys.stderr)

                process = subprocess.Popen(
                    cmd,
                    stdout=subprocess.PIPE,
                    stderr=subprocess.STDOUT,
                    text=True,
                    bufsize=1,
                    cwd=str(PROJECT_ROOT)  # Use project root for relative config paths
                )

                # Capture all output to send back to client
                output_lines = []
                for line in process.stdout:
                    output_lines.append(line)
                    print(line, end='', flush=True)  # Also print locally

                    # Detect when ESP32 is reset - immediately start reconnecting
                    # arduino-cli prints this right after triggering RTS reset
                    if not monitor_resumed and "Hard resetting via RTS pin" in line:
                        print("[Compile] Device reset detected, resuming monitor...", file=sys.stderr)
                        self.resume_monitor()
                        monitor_resumed = True

                process.wait()
                exit_code = process.returncode

                return {
                    "success": exit_code == 0,
                    "exit_code": exit_code,
                    "output": ''.join(output_lines)
                }

            finally:
                # Ensure monitor is resumed even if reset message wasn't detected
                if not monitor_resumed:
                    self.resume_monitor()

        except Exception as e:
            # Resume monitor even on error
            self.resume_monitor()
            return {"success": False, "error": str(e)}

    def handle_log_request(self, request_data):
        """Handle log request."""
        clear = request_data.get("clear", False)
        log_data = self.monitor.get_log()
        if clear:
            self.monitor.clear_log()
        return {"success": True, "log": log_data.decode('utf-8', errors='replace')}

    def handle_stop_request(self):
        """Handle stop request."""
        self.running = False
        self.stop_monitor()
        return {"success": True}

    def run(self):
        """Run server loop."""
        # Remove existing socket if present
        if os.path.exists(SOCKET_PATH):
            try:
                os.unlink(SOCKET_PATH)
            except:
                pass

        # Create Unix domain socket
        self.server_socket = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        self.server_socket.bind(SOCKET_PATH)
        self.server_socket.listen(1)
        self.running = True

        # Set socket permissions (readable/writable by user)
        os.chmod(SOCKET_PATH, 0o600)

        print(f"[Server] Listening on {SOCKET_PATH}", file=sys.stderr)
        print(f"[Server] Started monitoring {self.monitor.port} at {self.monitor.baud} baud", file=sys.stderr)

        # Start monitoring
        self.start_monitor()

        # Signal handlers for graceful shutdown
        def signal_handler(sig, frame):
            print("\n[Server] Shutting down...", file=sys.stderr)
            self.running = False
            self.stop_monitor()
            if self.server_socket:
                self.server_socket.close()
            if os.path.exists(SOCKET_PATH):
                try:
                    os.unlink(SOCKET_PATH)
                except:
                    pass
            sys.exit(0)

        signal.signal(signal.SIGINT, signal_handler)
        signal.signal(signal.SIGTERM, signal_handler)

        # Accept connections
        while self.running:
            try:
                self.server_socket.settimeout(1.0)
                conn, _ = self.server_socket.accept()
            except socket.timeout:
                continue
            except Exception as e:
                if self.running:
                    print(f"[Server] Error accepting connection: {e}", file=sys.stderr)
                break

            try:
                # Receive request
                data = conn.recv(4096).decode('utf-8')
                request = json.loads(data)

                # Handle request
                command = request.get("command")
                if command == "compile":
                    response = self.handle_compile_request(request)
                elif command == "log":
                    response = self.handle_log_request(request)
                elif command == "stop":
                    response = self.handle_stop_request()
                else:
                    response = {"success": False, "error": f"Unknown command: {command}"}

                # Send response
                conn.sendall(json.dumps(response).encode('utf-8'))
                conn.close()

            except Exception as e:
                print(f"[Server] Error handling request: {e}", file=sys.stderr)
                try:
                    conn.close()
                except:
                    pass

        # Cleanup
        self.stop_monitor()
        if self.server_socket:
            self.server_socket.close()
        if os.path.exists(SOCKET_PATH):
            try:
                os.unlink(SOCKET_PATH)
            except:
                pass


def send_request(command, **kwargs):
    """Send request to server via Unix domain socket."""
    if not os.path.exists(SOCKET_PATH):
        print(f"Error: Server not running (socket not found: {SOCKET_PATH})", file=sys.stderr)
        print("Start server with: arduwrap serve --port <port> --baud <baud>", file=sys.stderr)
        sys.exit(1)

    try:
        sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        sock.connect(SOCKET_PATH)

        request = {"command": command, **kwargs}
        sock.sendall(json.dumps(request).encode('utf-8'))

        # Receive response - read all data until socket is closed
        chunks = []
        while True:
            chunk = sock.recv(8192)
            if not chunk:
                break
            chunks.append(chunk)
        sock.close()

        response_data = b''.join(chunks).decode('utf-8')
        response = json.loads(response_data)

        return response

    except Exception as e:
        print(f"Error communicating with server: {e}", file=sys.stderr)
        sys.exit(1)


def cmd_serve(args):
    """Serve command: Start monitoring server."""
    if not args.port:
        print("Error: --port is required", file=sys.stderr)
        sys.exit(1)
    if not args.baud:
        print("Error: --baud is required", file=sys.stderr)
        sys.exit(1)

    server = ArduinoWrapperServer(args.port, args.baud)
    server.run()


def cmd_compile(args):
    """Compile command: Send compile request to server."""
    # Pass through all arguments to arduino-cli
    # args.unknown_args contains all arguments that weren't recognized by argparse
    compile_args = args.unknown_args.copy() if hasattr(args, 'unknown_args') else []

    # Always add --upload flag (this is the main purpose of the wrapper)
    if "--upload" not in compile_args and "-u" not in compile_args:
        compile_args.append("--upload")

    # Convert sketch path to absolute path (last non-flag argument is typically the sketch)
    # This ensures the server can find the sketch regardless of its working directory
    resolved_args = []
    for i, arg in enumerate(compile_args):
        # Check if this looks like a sketch path (ends with .ino or is a directory)
        if arg.endswith('.ino') or (not arg.startswith('-') and os.path.isdir(arg)):
            resolved_args.append(os.path.abspath(arg))
        else:
            resolved_args.append(arg)

    response = send_request("compile", args=resolved_args)

    # Print compile output to caller
    output = response.get("output", "")
    if output:
        print(output, end='')

    exit_code = response.get("exit_code", 1)

    if not response.get("success"):
        error = response.get("error")
        if error:
            print(f"Error: {error}", file=sys.stderr)
        sys.exit(exit_code)

    sys.exit(exit_code)


def cmd_log(args):
    """Log command: Get buffered log from server."""
    response = send_request("log", clear=args.clear)

    if not response.get("success"):
        error = response.get("error", "Unknown error")
        print(f"Error: {error}", file=sys.stderr)
        sys.exit(1)

    log_content = response.get("log", "")
    if not log_content:
        print("(no log data)", file=sys.stderr)
        return

    lines = log_content.splitlines(keepends=True)

    # Apply filter if specified
    if args.filter:
        import re
        try:
            pattern = re.compile(args.filter, re.IGNORECASE if args.ignore_case else 0)
        except re.error as e:
            print(f"Invalid regex pattern: {e}", file=sys.stderr)
            sys.exit(1)
        lines = [line for line in lines if pattern.search(line)]

    # Apply max lines limit
    if args.lines and len(lines) > args.lines:
        lines = lines[-args.lines:]

    if lines:
        print(''.join(lines), end='')
    else:
        print("(no matching lines)", file=sys.stderr)


def cmd_stop(args):
    """Stop command: Stop monitoring server."""
    response = send_request("stop")

    if not response.get("success"):
        error = response.get("error", "Unknown error")
        print(f"Error: {error}", file=sys.stderr)
        sys.exit(1)


def main():
    parser = argparse.ArgumentParser(
        description="Arduino CLI wrapper for serial monitoring with automatic port management"
    )
    subparsers = parser.add_subparsers(dest="command", help="Command to execute")

    # serve command
    serve_parser = subparsers.add_parser("serve", help="Start monitoring server")
    serve_parser.add_argument("--port", required=True, help="Serial port (e.g., /dev/cu.usbmodem1101)")
    serve_parser.add_argument("--baud", required=True, type=int, help="Baud rate (e.g., 115200)")
    serve_parser.set_defaults(func=cmd_serve)

    # compile command - pass through all arguments to arduino-cli
    # No arguments defined here - all args will be passed through
    compile_parser = subparsers.add_parser("compile", help="Compile and upload (via server). All arguments are passed to arduino-cli compile (--upload is added automatically)")
    compile_parser.set_defaults(func=cmd_compile)

    # log command
    log_parser = subparsers.add_parser("log", help="Get buffered log from server (since last reboot or clear)")
    log_parser.add_argument("--clear", "-c", action="store_true", help="Clear log buffer after retrieving")
    log_parser.add_argument("--filter", "-f", metavar="PATTERN", help="Filter log lines by regex pattern")
    log_parser.add_argument("--ignore-case", "-i", action="store_true", help="Case-insensitive filter matching")
    log_parser.add_argument("--lines", "-n", type=int, metavar="N", help="Show only last N lines")
    log_parser.set_defaults(func=cmd_log)

    # stop command
    stop_parser = subparsers.add_parser("stop", help="Stop monitoring server")
    stop_parser.set_defaults(func=cmd_stop)

    args, unknown = parser.parse_known_args()

    if not args.command:
        parser.print_help()
        sys.exit(1)

    # For compile command, store unknown args (which are the arduino-cli args)
    if args.command == "compile":
        args.unknown_args = unknown
    else:
        args.unknown_args = []

    args.func(args)


if __name__ == "__main__":
    main()
