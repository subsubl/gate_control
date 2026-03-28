import http.server
import socketserver
import json
import os
import time
import socket
import threading

try:
    import paho.mqtt.client as mqtt
    MQTT_AVAILABLE = True
except ImportError:
    MQTT_AVAILABLE = False
    print("WARNING: paho-mqtt not installed. MQTT features disabled.")

PORT = 7777
DATA_DIR = os.path.join(os.path.dirname(__file__), 'data')

# MQTT Config
MQTT_BROKER = "test.mosquitto.org"
MQTT_TOPIC_CMD = "antigravity_gate/cmd"
MQTT_TOPIC_STATUS = "antigravity_gate/status"
mqtt_client = None

# Mock Database
# Mock Database
users = {}
logs = []

# Security State
failed_attempts = 0
lockout_timestamp = 0

class CustomHandler(http.server.SimpleHTTPRequestHandler):
    def translate_path(self, path):
        # Handle remappings here to be robust
        if path == '/admin':
            path = '/admin.html'
        elif path == '/':
            path = '/index.html'

        # Serve from the 'data' directory
        res = super().translate_path(path)
        relpath = os.path.relpath(res, os.getcwd())
        final_path = res
        if not relpath.startswith('data'):
             final_path = os.path.join(DATA_DIR, os.path.basename(res))

        print(f"DEBUG: Request={path} Orig={res} Rel={relpath} Final={final_path}")
        return final_path

    def do_GET(self):
        if self.path.startswith('/api/admin/users'):
            self.send_response(200)
            self.send_header('Content-type', 'application/json')
            self.end_headers()
            self.wfile.write(json.dumps(list(users.values())).encode())
            return
        elif self.path.startswith('/api/admin/logs'):
            self.send_response(200)
            self.send_header('Content-type', 'application/json')
            self.end_headers()
            self.wfile.write(json.dumps(logs).encode())
            return
        elif self.path == '/api/admin/logs/download':
            self.send_response(200)
            self.send_header('Content-Type', 'text/csv')
            self.send_header('Content-Disposition', 'attachment; filename="access_logs.csv"')
            self.end_headers()
            csv_data = "Time,User,Granted,Details\n"
            for log in logs:
                csv_data += f"{log['time']},{log['user']},{log['granted']},{log['details']}\n"
            self.wfile.write(csv_data.encode())
            return

        # Default file serving
        if self.path == '/':
            self.path = '/index.html'
        elif self.path == '/admin':
            self.path = '/admin.html'

        return super().do_GET()

    def do_POST(self):
        print(f"POST Received: {self.path}")
        global failed_attempts, lockout_timestamp, users, MQTT_BROKER, MQTT_TOPIC_CMD, MQTT_TOPIC_STATUS
        length = int(self.headers.get('content-length', 0))
        body = self.rfile.read(length).decode()
        data = json.loads(body) if body else {}

        if self.path == '/api/auth/login':
            password = data.get('password')
            if password == 'Baracuda1106': # Mock check
                self.send_json({'status': 'ok'})
            else:
                self.send_error(401, 'Invalid password')

        elif self.path == '/api/access/verify':
            pin = data.get('pin')

            # Mock Brute Force Protection
            global failed_attempts, lockout_timestamp
            current_time = int(time.time())

            if lockout_timestamp > 0:
                if current_time < lockout_timestamp:
                    self.send_response(403)
                    self.send_header('Content-Type', 'application/json')
                    self.end_headers()
                    self.wfile.write(b'{"status":"locked", "details":"System Locked Out"}')
                    return
                else:
                    lockout_timestamp = 0
                    failed_attempts = 0

            # Validate against users
            user = users.get(pin)
            valid = (user is not None)
            u = user

            # Global Security Checks (Lockout) & Brute Force Counting
            if valid:
                failed_attempts = 0
            else:
                failed_attempts += 1
                if failed_attempts >= 5:
                    lockout_timestamp = current_time + 300
                    log_entry = {
                        'time': current_time,
                        'user': 'System',
                        'granted': False,
                        'details': 'Security Lockout'
                    }
                    logs.insert(0, log_entry)
                    self.send_response(403)
                    self.send_header('Content-Type', 'application/json')
                    self.end_headers()
                    self.wfile.write(b'{"status":"locked"}')
                    return

            # Check Schedule
            if valid:
                current_struct = time.localtime(current_time)

                # Check Days
                allowed_days = u.get('days', 0)
                if allowed_days != 0:
                    bit_idx = (current_struct.tm_wday + 1) % 7
                    if not (allowed_days & (1 << bit_idx)):
                        valid = False

                # Check Time Window
                start_time = u.get('start', 0)
                end_time = u.get('end', 0)

                if start_time != end_time:
                    current_mins = current_struct.tm_hour * 60 + current_struct.tm_min
                    in_window = False
                    if start_time < end_time:
                        if start_time <= current_mins < end_time:
                            in_window = True
                    else: # Crossover
                        if current_mins >= start_time or current_mins < end_time:
                            in_window = True

                    if not in_window:
                        valid = False

            # Check Limits (Count / OTP)
            if valid and u.get('type') in [2, 3]: # Count or One-Time
                remaining = u.get('remaining', 0)
                if remaining <= 0:
                     valid = False
                else:
                    u['remaining'] = remaining - 1
                    if u['type'] == 3 and u['remaining'] == 0:
                        # Deactivate/Remove OTP user
                        del users[u['pin']]

            log_entry = {
                'time': current_time,
                'user': u['name'] if u else 'Unknown',
                'granted': valid,
                'details': 'Access Granted' if valid else 'Denied (Schedule/PIN)'
            }
            logs.insert(0, log_entry)

            if valid:
                self.send_json({'status': 'granted'})
            else:
                self.send_response(401)
                self.send_header('Content-Type', 'application/json')
                self.end_headers()
                self.wfile.write(b'{"status":"denied"}')

        elif self.path == '/api/admin/open':
            # Mock gate opening
            print("GATE OPENED via Admin Console")
            self.send_json({'status': 'ok'})

        elif self.path == '/api/admin/users':
            # Add user
            import random
            def generate_pin():
                while True:
                    p = str(random.randint(10000, 99999))
                    if p not in users:
                        return p

            new_user = {
                'name': data.get('name'),
                'pin': generate_pin(),
                'type': data.get('type'),
                'expiry': 0,
                'remaining': data.get('limit', 0),
                'start': data.get('start', 0),
                'end': data.get('end', 0),
                'days': data.get('days', 0)
            }
            users[new_user['pin']] = new_user
            self.send_json({'status': 'ok', 'pin': new_user['pin']}) # Return PIN so UI can show it if needed

        elif self.path == '/api/admin/mqtt':
            if self.command == 'GET':
                cfg = {
                    'uri': MQTT_BROKER,
                    'cmd_topic': MQTT_TOPIC_CMD,
                    'status_topic': MQTT_TOPIC_STATUS
                }
                self.send_json(cfg)
            elif self.command == 'POST':
                # Data already parsed at top of do_POST
                # global MQTT_BROKER, MQTT_TOPIC_CMD, MQTT_TOPIC_STATUS -- Already declared
                if 'uri' in data: MQTT_BROKER = data['uri'].replace("mqtt://", "") # Paho needs host, not URI
                if 'cmd_topic' in data: MQTT_TOPIC_CMD = data['cmd_topic']
                if 'status_topic' in data: MQTT_TOPIC_STATUS = data['status_topic']

                print(f"MQTT Config Updated: Broker={MQTT_BROKER}, Cmd={MQTT_TOPIC_CMD}")

                # Restart MQTT
                restart_mqtt()
                self.send_json({'status': 'ok'})
            return

        elif self.path == '/api/admin/users?delete=true': # Handle DELETE as POST if needed or check method
             pass # Logic below in DELETE

    def do_DELETE(self):
        print(f"DELETE Request: {self.path}")
        if self.path.startswith('/api/admin/users'):
             length = int(self.headers.get('content-length', 0))
             body = self.rfile.read(length).decode()
             data = json.loads(body)
             pin_to_del = data.get('pin')

             print(f"Deleting user pin: {pin_to_del}")

             if pin_to_del in users:
                 del users[pin_to_del]
                 self.send_json({'status': 'ok'})
             else:
                 self.send_error(404, 'User not found')

    def do_PUT(self):
        print(f"PUT Request: {self.path}")
        if self.path.startswith('/api/admin/users'):
             length = int(self.headers.get('content-length', 0))
             body = self.rfile.read(length).decode()
             data = json.loads(body)

             pin = data.get('pin')
             print(f"Updating user pin: {pin}")

             if pin in users:
                 u = users[pin]
                 u['name'] = data.get('name', u['name'])
                 u['type'] = data.get('type', u['type'])
                 u['remaining'] = data.get('limit', u['remaining'])
                 u['start'] = data.get('start', u['start'])
                 u['end'] = data.get('end', u['end'])
                 u['days'] = data.get('days', u['days'])
                 self.send_json({'status': 'ok'})
             else:
                 self.send_error(404, 'User not found')

    def send_json(self, data):
        self.send_response(200)
        self.send_header('Content-type', 'application/json')
        self.end_headers()
        self.wfile.write(json.dumps(data).encode())

# MQTT Functions (Global)
def start_mqtt():
    if not MQTT_AVAILABLE:
        return

    global mqtt_client, MQTT_BROKER, MQTT_TOPIC_CMD, MQTT_TOPIC_STATUS

    def on_connect(client, userdata, flags, rc, properties=None):
        print(f"MQTT Connected (rc={rc})")
        client.subscribe(MQTT_TOPIC_CMD)
        client.publish(MQTT_TOPIC_STATUS, "ONLINE", retain=True)

    def on_message(client, userdata, msg):
        if msg.topic == MQTT_TOPIC_CMD:
            payload = msg.payload.decode()
            print(f"MQTT Command received: {payload}")
            if payload == "OPEN":
                print("GATE OPENING via MQTT")
                client.publish(MQTT_TOPIC_STATUS, "OPENING")
                # Log it
                log_entry = {
                    'time': int(time.time()),
                    'user': 'MQTT',
                    'granted': True,
                    'details': 'Remote Open'
                }
                logs.insert(0, log_entry)

    mqtt_client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2)
    mqtt_client.on_connect = on_connect
    mqtt_client.on_message = on_message
    try:
        print(f"Connecting to MQTT Broker: {MQTT_BROKER}...")
        mqtt_client.connect(MQTT_BROKER, 1883, 60)
        mqtt_client.loop_start()
    except Exception as e:
        print(f"MQTT Failed: {e}")

def restart_mqtt():
    def _restart():
        global mqtt_client
        if mqtt_client:
            print("Stopping MQTT...")
            mqtt_client.loop_stop()
            mqtt_client.disconnect()
        start_mqtt()

    threading.Thread(target=_restart, daemon=True).start()

if __name__ == "__main__":
    # PORT is defined globally as 7777
    class ThreadingHTTPServer(socketserver.ThreadingTCPServer):
        allow_reuse_address = True
        address_family = socket.AF_INET # Force IPv4

    os.chdir(os.path.dirname(os.path.abspath(__file__)))

    threading.Thread(target=start_mqtt, daemon=True).start()

    try:
        with ThreadingHTTPServer(("0.0.0.0", PORT), CustomHandler) as httpd:
            print(f"Starting Host Simulation Server at http://127.0.0.1:{PORT}")
            httpd.serve_forever()
    except KeyboardInterrupt:
        print("\nStopping server.")
        if mqtt_client: mqtt_client.loop_stop()
