"""
Name:          parseLiveTelemetry.py
Description:   A script for parsing published messages from project displaying
               messages formatted for human readability.
Author:        Abanoub Salah
Version:       1.0.0
Dependencies:  modbus_pipeline, paho-mqtt
License:       MIT
"""

import argparse
import paho.mqtt.client as mqtt
from paho.mqtt.client import CallbackAPIVersion
import struct
from modbus_pipeline.serializer import Serializer

METRICS = [
    "RS485 overflow error(s)",
    "RS485 parity error(s)",
    "Modbus CRC error(s)",
    "Modbus overflow error(s)",
    "Modbus no_mem error(s)",
    "Telemetry publish error(s)",
    "Logger write error(s)",
    ]

# Callback function for when the client connects to the broker
def on_connect(client, userdata, flags, rc, properties=None):
    if rc == 0:
        print("Successfully connected to the MQTT broker.")
        # Subscribe to the topic after connection
        client.subscribe('#')
    else:
        print(f"Failed to connect, return code {rc}")

# Callback function for when a message is received
def on_message(client, userdata, msg):
    if msg.topic.endswith("/replay") or msg.topic.endswith("/live"):
        frame = Serializer.deserialize(msg.payload)
        print(f'{msg.topic}\n{frame}')
    elif msg.topic.endswith("/status"):
        # size = n metrics * 4 bytes (uint32_t)
        metrics_struct_size = len(METRICS) * 4
        if len(msg.payload) == metrics_struct_size:
            # '<' = little-endian (matching the ESP32)
            # 'nI' = n unsigned integer elements
            try:
                metrics_struct = struct.unpack(f'<{len(METRICS)}I', msg.payload)

                metrics_max_label = max(len(i) for i in METRICS)

                print("-" * (metrics_max_label + 20))
                print("Metrics Received:")
                for i, val in enumerate(metrics_struct):
                    print(f"\t{METRICS[i]:{metrics_max_label}}: {val:>10,}")
            except struct.error as e:
                print(f"Error unpacking payload: {e}")
        else:
            print(f"Received unexpected payload size: {len(msg.payload)} bytes (Expected {metrics_struct_size} bytes)")

def main():
    parser = argparse.ArgumentParser(description="Bridge Live Parser")
    parser.add_argument("--port", type=int, default=1883, help="Broker port")
    parser.add_argument("--address", type=str, default="localhost", help="Broker address")

    args = parser.parse_args()

    # Initialize the MQTT Client with v2 API
    client = mqtt.Client(CallbackAPIVersion.VERSION2)

    # Assign the callback functions
    client.on_connect = on_connect
    client.on_message = on_message

    # Connect to broker (running locally on the default port 1883)
    broker_address = args.address
    port = args.port
    client.connect(broker_address, port, keepalive=60)

    # Blocking call that processes network traffic, dispatches callbacks, and handles reconnecting.
    try:
        print("Listening for messages... Press Ctrl+C to exit.")
        client.loop_forever()
    except KeyboardInterrupt:
        print("\nDisconnecting from broker.")
        client.disconnect()

if __name__ == "__main__":
    main()