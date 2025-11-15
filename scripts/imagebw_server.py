#!/usr/bin/env python3
"""
Python HTTP Server for receiving ImageBW data from Arduino
Receives binary ImageBW array (27,200 bytes) and saves as PNG image
"""

from http.server import HTTPServer, BaseHTTPRequestHandler
import json
import os
import sys
from datetime import datetime

# Add scripts directory to path for imports
script_dir = os.path.dirname(os.path.abspath(__file__))
if script_dir not in sys.path:
    sys.path.insert(0, script_dir)

from convert_imagebw import convert_imagebw_to_png

class ImageBWHandler(BaseHTTPRequestHandler):
    def do_POST(self):
        """Handle POST requests to /imagebw endpoint"""
        if self.path == '/imagebw':
            self.handle_imagebw()
        elif self.path == '/imagebw/base64':
            self.handle_imagebw_base64()
        else:
            self.send_error(404, "Not Found")

    def handle_imagebw(self):
        """Handle binary ImageBW data"""
        try:
            # Get content length
            content_length = int(self.headers.get('Content-Length', 0))

            if content_length != 27200:
                self.send_error(400, f"Invalid content length: {content_length}, expected 27200")
                return

            # Read binary data
            imagebw_data = self.rfile.read(content_length)

            if len(imagebw_data) != 27200:
                self.send_error(400, f"Invalid data length: {len(imagebw_data)}, expected 27200")
                return

            # Generate filename with timestamp
            timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
            filename = f"imagebw_{timestamp}.png"

            # Convert ImageBW to PNG
            output_path = convert_imagebw_to_png(imagebw_data, filename)

            # Send success response
            response = {
                "status": "success",
                "filename": filename,
                "size": len(imagebw_data)
            }

            self.send_response(200)
            self.send_header('Content-Type', 'application/json')
            self.end_headers()
            self.wfile.write(json.dumps(response).encode('utf-8'))

            print(f"[{datetime.now()}] Received ImageBW: {filename} ({len(imagebw_data)} bytes)")

        except Exception as e:
            print(f"[{datetime.now()}] Error handling ImageBW: {e}")
            self.send_error(500, f"Internal Server Error: {str(e)}")

    def handle_imagebw_base64(self):
        """Handle Base64 encoded ImageBW data (for debugging)"""
        try:
            import base64

            # Get content length
            content_length = int(self.headers.get('Content-Length', 0))

            # Read Base64 data
            base64_data = self.rfile.read(content_length).decode('utf-8')

            # Decode Base64
            imagebw_data = base64.b64decode(base64_data)

            if len(imagebw_data) != 27200:
                self.send_error(400, f"Invalid data length after decode: {len(imagebw_data)}, expected 27200")
                return

            # Generate filename with timestamp
            timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
            filename = f"imagebw_{timestamp}.png"

            # Convert ImageBW to PNG
            output_path = convert_imagebw_to_png(imagebw_data, filename)

            # Send success response
            response = {
                "status": "success",
                "filename": filename,
                "size": len(imagebw_data)
            }

            self.send_response(200)
            self.send_header('Content-Type', 'application/json')
            self.end_headers()
            self.wfile.write(json.dumps(response).encode('utf-8'))

            print(f"[{datetime.now()}] Received ImageBW (Base64): {filename} ({len(imagebw_data)} bytes)")

        except Exception as e:
            print(f"[{datetime.now()}] Error handling ImageBW (Base64): {e}")
            self.send_error(500, f"Internal Server Error: {str(e)}")

    def do_GET(self):
        """Handle GET requests for status"""
        if self.path == '/status':
            self.send_response(200)
            self.send_header('Content-Type', 'application/json')
            self.end_headers()
            response = {
                "status": "running",
                "endpoints": {
                    "/imagebw": "POST binary ImageBW data (27,200 bytes)",
                    "/imagebw/base64": "POST Base64 encoded ImageBW data",
                    "/status": "GET server status"
                }
            }
            self.wfile.write(json.dumps(response, indent=2).encode('utf-8'))
        else:
            self.send_error(404, "Not Found")

    def log_message(self, format, *args):
        """Override to use custom logging format"""
        print(f"[{datetime.now()}] {format % args}")

def run_server(port=8080, host='0.0.0.0'):
    """Run the HTTP server"""
    server_address = (host, port)
    httpd = HTTPServer(server_address, ImageBWHandler)

    print(f"ImageBW Server starting on http://{host}:{port}")
    print(f"Endpoints:")
    print(f"  POST /imagebw - Send binary ImageBW data")
    print(f"  POST /imagebw/base64 - Send Base64 encoded ImageBW data")
    print(f"  GET /status - Get server status")
    print(f"\nPress Ctrl+C to stop the server")

    try:
        httpd.serve_forever()
    except KeyboardInterrupt:
        print("\nServer stopped")
        httpd.shutdown()

if __name__ == '__main__':
    import argparse

    parser = argparse.ArgumentParser(description='ImageBW HTTP Server')
    parser.add_argument('--port', type=int, default=8080, help='Server port (default: 8080)')
    parser.add_argument('--host', type=str, default='0.0.0.0', help='Server host (default: 0.0.0.0)')
    args = parser.parse_args()

    run_server(port=args.port, host=args.host)
