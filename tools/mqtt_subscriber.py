#!/usr/bin/env python3
"""
MQTT Subscriber for BME680 Sensor Data Testing
This script subscribes to MQTT topics and displays incoming sensor data.

Requirements:
    pip install paho-mqtt

Usage:
    python mqtt_subscriber.py
    python mqtt_subscriber.py --host 192.168.1.100
"""

import argparse
import json
import sys
from datetime import datetime

try:
    import paho.mqtt.client as mqtt
except ImportError:
    print("Error: paho-mqtt not installed. Run: pip install paho-mqtt")
    sys.exit(1)


# MQTT Topics (must match ESP32 configuration)
TOPICS = [
    ("sensor/bme680/data", 0),
    ("sensor/bme680/iaq", 0),
    ("sensor/bme680/status", 0),
    ("sensor/bme680/alert", 0),
]


def on_connect(client, userdata, flags, rc, properties=None):
    """Callback when connected to MQTT broker"""
    if rc == 0:
        print(f"\n{'='*60}")
        print(f"✅ Connected to MQTT Broker successfully!")
        print(f"{'='*60}")
        
        # Subscribe to all topics
        for topic, qos in TOPICS:
            client.subscribe(topic, qos)
            print(f"📡 Subscribed to: {topic}")
        print(f"{'='*60}\n")
        print("Waiting for sensor data...\n")
    else:
        print(f"❌ Failed to connect, return code {rc}")


def on_disconnect(client, userdata, rc, *args):
    """Callback when disconnected from MQTT broker"""
    print(f"\n⚠️  Disconnected from MQTT Broker (code: {rc})")


def on_message(client, userdata, msg):
    """Callback when message is received"""
    timestamp = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
    topic = msg.topic
    
    try:
        payload = json.loads(msg.payload.decode())
        
        print(f"\n{'─'*60}")
        print(f"📨 [{timestamp}] Topic: {topic}")
        print(f"{'─'*60}")
        
        if topic == "sensor/bme680/data":
            print(f"🌡️  Temperature  : {payload.get('temperature', 'N/A'):.2f} °C")
            print(f"💧 Humidity     : {payload.get('humidity', 'N/A'):.2f} %")
            print(f"🔵 Pressure     : {payload.get('pressure', 'N/A'):.2f} hPa")
            print(f"💨 Gas Resist.  : {payload.get('gas_resistance', 'N/A'):.0f} Ohms")
            print(f"✓  Gas Valid    : {payload.get('gas_valid', 'N/A')}")
            
        elif topic == "sensor/bme680/iaq":
            iaq_score = payload.get('iaq_score', 0)
            iaq_text = payload.get('iaq_text', 'Unknown')
            
            # Color indicator based on IAQ level
            if iaq_score <= 50:
                indicator = "🟢"
            elif iaq_score <= 100:
                indicator = "🟡"
            elif iaq_score <= 150:
                indicator = "🟠"
            else:
                indicator = "🔴"
            
            print(f"{indicator} IAQ Score    : {iaq_score:.1f} [{iaq_text}]")
            print(f"🔢 IAQ Level    : {payload.get('iaq_level', 'N/A')}")
            print(f"🎯 Accuracy     : {payload.get('accuracy', 'N/A')}")
            print(f"🌫️  CO2 Equiv.   : {payload.get('co2_equivalent', 'N/A'):.0f} ppm")
            print(f"🧪 VOC Equiv.   : {payload.get('voc_equivalent', 'N/A'):.2f} ppm")
            print(f"✓  Calibrated   : {payload.get('is_calibrated', 'N/A')}")
            
        elif topic == "sensor/bme680/status":
            status = payload.get('status', 'unknown')
            client_id = payload.get('client_id', 'unknown')
            icon = "🟢" if status == "online" else "🔴"
            print(f"{icon} Status: {status}")
            print(f"📟 Client ID: {client_id}")
            
        elif topic == "sensor/bme680/alert":
            print(f"⚠️  Alert Type  : {payload.get('type', 'N/A')}")
            print(f"📢 Message     : {payload.get('message', 'N/A')}")
            print(f"📟 Client ID   : {payload.get('client_id', 'N/A')}")
        
        else:
            print(f"Payload: {json.dumps(payload, indent=2)}")
            
    except json.JSONDecodeError:
        print(f"Raw message: {msg.payload.decode()}")
    except Exception as e:
        print(f"Error processing message: {e}")


def main():
    parser = argparse.ArgumentParser(description='MQTT Subscriber for BME680 Sensor')
    parser.add_argument('--host', default='localhost', help='MQTT broker host (default: localhost)')
    parser.add_argument('--port', type=int, default=1883, help='MQTT broker port (default: 1883)')
    args = parser.parse_args()
    
    print(f"\n{'='*60}")
    print(f"🚀 BME680 MQTT Subscriber")
    print(f"{'='*60}")
    print(f"Connecting to: {args.host}:{args.port}")
    
    # Create MQTT client
    client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2)
    
    # Set callbacks
    client.on_connect = on_connect
    client.on_disconnect = on_disconnect
    client.on_message = on_message
    
    try:
        client.connect(args.host, args.port, 60)
        client.loop_forever()
    except KeyboardInterrupt:
        print("\n\n👋 Disconnecting...")
        client.disconnect()
    except Exception as e:
        print(f"\n❌ Error: {e}")
        sys.exit(1)


if __name__ == "__main__":
    main()
