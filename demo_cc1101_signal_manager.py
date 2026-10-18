#!/usr/bin/env python3

"""
CC1101 443MHz Remote Control Signal Manager
Test and demonstration script
"""

import json
import struct
import base64
import time
import socket
from typing import List, Dict, Any

class SignalManager:
    """Manage CC1101 signal acquisition and transmission"""
    
    def __init__(self):
        self.signals = []
        self.capture_active = False
        self.web_server_running = False
        self.cc1101_initialized = False
        
    def initialize_cc1101(self) -> bool:
        """Initialize CC1101 module"""
        try:
            # Simulate CC1101 initialization
            print("Initializing CC1101 443MHz RF module...")
            time.sleep(0.5)
            
            # Set configuration
            config = {
                'frequency': 443000000,
                'channel': 1,
                'modulation': 'GFSK',
                'sample_rate': 38400,
                'power_level': 10,
                'bandwidth': 500
            }
            
            print(f"CC1101 configured: {config}")
            self.cc1101_initialized = True
            print("✓ CC1101 initialized successfully")
            return True
            
        except Exception as e:
            print(f"✗ CC1101 initialization failed: {e}")
            return False
    
    def start_acquisition(self, duration: int = 10) -> Dict[str, Any]:
        """Start signal acquisition"""
        if not self.cc1101_initialized:
            return {'status': 'error', 'message': 'CC1101 not initialized'}
        
        try:
            print(f"Starting signal acquisition for {duration} seconds...")
            self.capture_active = True
            
            # Simulate signal acquisition
            samples = []
            start_time = time.time()
            
            while time.time() - start_time < duration and self.capture_active:
                # Simulate receiving RF signal samples
                sample = struct.pack('<I', int(65535 * (0.5 + 0.5 * (0.5 + 0.5 * (time.time() % 1)))))
                samples.append(sample)
                time.sleep(0.001)  # Simulate real-time sampling
            
            self.capture_active = False
            
            # Convert samples to signal data
            signal_data = b''.join(samples)
            signal_size = len(signal_data)
            
            # Create signal entry
            signal_id = len(self.signals) + 1
            signal_entry = {
                'id': signal_id,
                'timestamp': int(time.time()),
                'frequency': 443000000,
                'duration': duration,
                'samples_count': len(samples),
                'data_size': signal_size,
                'status': 'completed',
                'filename': f'signal_{signal_id:06d}.dat'
            }
            
            # Store signal
            self.signals.append(signal_entry)
            
            # Save signal data to file (base64 encoded for JSON compatibility)
            self._save_signal_to_file(signal_id, signal_data)
            
            print(f"✓ Signal acquisition completed: {signal_size} bytes, {len(samples)} samples")
            
            return {
                'status': 'success',
                'signal_id': signal_id,
                'data_size': signal_size,
                'samples_count': len(samples),
                'filename': signal_entry['filename']
            }
            
        except Exception as e:
            self.capture_active = False
            print(f"✗ Signal acquisition failed: {e}")
            return {'status': 'error', 'message': str(e)}
    
    def _save_signal_to_file(self, signal_id: int, data: bytes):
        """Save signal data to file"""
        try:
            filename = f"/sdcard/signals/signal_{signal_id:06d}.dat"
            with open(filename, 'wb') as f:
                f.write(data)
            print(f"✓ Signal saved to {filename}")
        except Exception as e:
            print(f"✗ Failed to save signal: {e}")
    
    def transmit_signal(self, signal_id: int) -> Dict[str, Any]:
        """Transmit a stored signal"""
        try:
            # Find signal
            signal = next((s for s in self.signals if s['id'] == signal_id), None)
            if not signal:
                return {'status': 'error', 'message': 'Signal not found'}
            
            # Load signal data
            filename = f"/sdcard/signals/signal_{signal_id:06d}.dat"
            with open(filename, 'rb') as f:
                signal_data = f.read()
            
            print(f"Transmitting signal {signal_id}: {len(signal_data)} bytes")
            
            # Simulate transmission
            time.sleep(0.5)
            
            # Update signal status
            signal['transmitted'] = True
            signal['transmission_time'] = int(time.time())
            
            return {
                'status': 'success',
                'signal_id': signal_id,
                'data_size': len(signal_data),
                'transmitted_at': signal['transmission_time']
            }
            
        except Exception as e:
            print(f"✗ Signal transmission failed: {e}")
            return {'status': 'error', 'message': str(e)}
    
    def list_signals(self) -> List[Dict[str, Any]]:
        """List all stored signals"""
        try:
            # Load signals from storage
            signals = []
            for i in range(1, len(self.signals) + 1):
                filename = f"/sdcard/signals/signal_{i:06d}.dat"
                try:
                    with open(filename, 'rb') as f:
                        data = f.read()
                    signals.append({
                        'id': i,
                        'filename': f'signal_{i:06d}.dat',
                        'size': len(data),
                        'timestamp': self.signals[i-1]['timestamp'],
                        'duration': self.signals[i-1]['duration'],
                        'samples_count': self.signals[i-1]['samples_count']
                    })
                except FileNotFoundError:
                    continue
            
            return signals
            
        except Exception as e:
            print(f"✗ Failed to list signals: {e}")
            return []
    
    def start_web_server(self) -> bool:
        """Start web server for remote control"""
        try:
            print("Starting web server for CC1101 control...")
            time.sleep(0.5)
            
            # Configure web server
            server_config = {
                'host': '0.0.0.0',
                'port': 80,
                'api_endpoints': [
                    '/api/signal/capture',
                    '/api/signal/list',
                    '/api/signal/download/{signal_id}',
                    '/api/status',
                    '/'
                ]
            }
            
            self.web_server_running = True
            
            print(f"✓ Web server started: http://192.168.1.1/")
            print(f"  Available endpoints:")
            for endpoint in server_config['api_endpoints']:
                print(f"    {endpoint}")
            
            return True
            
        except Exception as e:
            print(f"✗ Web server start failed: {e}")
            return False
    
    def stop_acquisition(self) -> bool:
        """Stop ongoing signal acquisition"""
        self.capture_active = False
        print("Signal acquisition stopped")
        return True
    
    def get_status(self) -> Dict[str, Any]:
        """Get system status"""
        return {
            'cc1101_initialized': self.cc1101_initialized,
            'capture_active': self.capture_active,
            'web_server_running': self.web_server_running,
            'total_signals': len(self.signals),
            'uptime': time.time(),
            'frequency': 443000000,
            'modulation': 'GFSK',
            'status': 'running'
        }
    
    def export_signals_json(self) -> str:
        """Export signals as JSON"""
        signals_list = self.list_signals()
        export_data = {
            'timestamp': int(time.time()),
            'system': 'CC1101_443MHz_Signal_Manager',
            'version': '1.0.0',
            'signals': signals_list
        }
        return json.dumps(export_data, indent=2)


def main():
    """Main demonstration"""
    print("=" * 60)
    print("CC1101 443MHz Remote Control Signal Manager")
    print("=" * 60)
    
    # Initialize signal manager
    manager = SignalManager()
    
    # Initialize CC1101
    if not manager.initialize_cc1101():
        print("Failed to initialize CC1101. Exiting.")
        return 1
    
    # Start web server
    if not manager.start_web_server():
        print("Failed to start web server. Continuing without web interface.")
    
    # Demonstrate signal capture
    print("\n" + "=" * 60)
    print("DEMONSTRATION: Signal Acquisition")
    print("=" * 60)
    
    # Start acquisition
    result = manager.start_acquisition(duration=5)
    print(f"Capture result: {json.dumps(result, indent=2)}")
    
    # List signals
    signals = manager.list_signals()
    print(f"\nStored signals: {len(signals)}")
    for signal in signals:
        print(f"  ID: {signal['id']}, Size: {signal['size']} bytes, "
              f"Duration: {signal['duration']}s, Samples: {signal['samples_count']}")
    
    # Demonstrate signal transmission
    print("\n" + "=" * 60)
    print("DEMONSTRATION: Signal Transmission")
    print("=" * 60)
    
    if signals:
        signal_id = signals[0]['id']
        transmit_result = manager.transmit_signal(signal_id)
        print(f"Transmission result: {json.dumps(transmit_result, indent=2)}")
    
    # Show system status
    print("\n" + "=" * 60)
    print("SYSTEM STATUS")
    print("=" * 60)
    
    status = manager.get_status()
    print(json.dumps(status, indent=2))
    
    # Export signals
    print("\n" + "=" * 60)
    print("SIGNAL DATA EXPORT")
    print("=" * 60)
    
    json_output = manager.export_signals_json()
    print(json_output)
    
    # Save to file
    with open('cc1101_signals_export.json', 'w') as f:
        f.write(json_output)
    print(f"✓ Signals exported to 'cc1101_signals_export.json'")
    
    print("\n" + "=" * 60)
    print("DEMONSTRATION COMPLETE")
    print("=" * 60)
    print("\nUsage:")
    print("1. Web Interface: http://192.168.1.1/")
    print("2. API Endpoints:")
    print("   - POST /api/signal/capture (capture new signal)")
    print("   - GET /api/signal/list (list signals)")
    print("   - GET /api/signal/download/{id} (download signal)")
    print("   - GET /api/status (system status)")
    print("3. Files: /sdcard/signals/ (signal storage)")
    print("\nThe system is ready for CC1101 443MHz remote control signal management!")
    
    return 0

if __name__ == "__main__":
    exit(main())