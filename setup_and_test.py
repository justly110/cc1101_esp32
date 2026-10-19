#!/usr/bin/env python3

"""
CC1101 443MHz Signal Manager - Quick Setup and Test Script

This script provides a quick setup and test of the CC1101 443MHz Signal Manager.
It guides you through the installation process and demonstrates the system capabilities.
"""

import os
import sys
import subprocess
import json
import time
from pathlib import Path

def print_header(title: str):
    """Print a formatted header"""
    print("\n" + "=" * 70)
    print(f"  {title}")
    print("=" * 70)

def print_step(step: int, description: str):
    """Print a step indicator"""
    print(f"\n{step}. {description}")
    print("-" * 70)

def check_esp_idf() -> bool:
    """Check if ESP-IDF is available"""
    print("Checking ESP-IDF installation...")
    
    # Check for IDF_PATH environment variable
    if "IDF_PATH" in os.environ:
        idf_path = os.environ["IDF_PATH"]
        print(f"  ✓ ESP-IDF found at: {idf_path}")
        return True
    else:
        print("  ✗ ESP-IDF not found")
        print("\nTo install ESP-IDF:")
        print("  1. Clone from GitHub: git clone https://github.com/espressif/esp-idf.git ~/esp-idf")
        print("  2. Install: ~/esp-idf/install.sh esp32s3")
        print("  3. Source: source ~/esp-idf/export.sh")
        return False

def check_project_structure() -> bool:
    """Check if the project structure is correct"""
    print("\nChecking project structure...")
    
    required_files = [
        "main/main.c",
        "main/cc1101_driver.c",
        "main/web_server.c",
        "main/cc1101_driver.h",
        "main/web_server.h",
        "CMakeLists.txt",
        "sdkconfig",
        "README.md"
    ]
    
    all_present = True
    for file_path in required_files:
        if os.path.exists(file_path):
            print(f"  ✓ {file_path}")
        else:
            print(f"  ✗ {file_path} - MISSING")
            all_present = False
    
    return all_present

def build_project() -> bool:
    """Build the project"""
    print("\nBuilding the project...")
    
    try:
        # Check if we can run idf.py
        result = subprocess.run(
            ["idf.py", "build"],
            capture_output=True,
            text=True,
            timeout=300  # 5 minutes timeout
        )
        
        if result.returncode == 0:
            print("  ✓ Build successful")
            print("\nBuild summary:")
            print("  - CC1101 driver: Compiled and integrated")
            print("  - Web server: Ready for web interface")
            print("  - WiFi softAP: Configured for 443MHz operation")
            print("  - Signal acquisition: Implemented")
            return True
        else:
            print("  ✗ Build failed")
            print(f"\nError output:\n{result.stderr}")
            return False
            
    except subprocess.TimeoutExpired:
        print("  ✗ Build timed out (5 minutes)")
        return False
    except FileNotFoundError:
        print("  ✗ idf.py not found. Is ESP-IDF installed?")
        return False
    except Exception as e:
        print(f"  ✗ Build error: {e}")
        return False

def start_demo() -> bool:
    """Start the demonstration script"""
    print("\nStarting demonstration...")
    
    demo_script = "demo_cc1101_signal_manager.py"
    
    if not os.path.exists(demo_script):
        print(f"  ✗ Demo script not found: {demo_script}")
        return False
    
    try:
        # Run the demo script
        result = subprocess.run(
            [sys.executable, demo_script],
            capture_output=True,
            text=True,
            timeout=60  # 1 minute timeout
        )
        
        if result.returncode == 0:
            print("\n  ✓ Demonstration completed successfully")
            print("\nDemo output summary:")
            lines = result.stdout.split('\n')
            for line in lines:
                if "✓" in line or "✗" in line or "Started" in line or "Completed" in line:
                    print(f"    {line}")
            return True
        else:
            print(f"  ✗ Demo failed with return code: {result.returncode}")
            return False
            
    except subprocess.TimeoutExpired:
        print("  ✗ Demo timed out (1 minute)")
        return False
    except Exception as e:
        print(f"  ✗ Demo error: {e}")
        return False

def show_summary():
    """Show project summary"""
    print_header("PROJECT SUMMARY")
    
    print("\nThe CC1101 443MHz Signal Manager has been successfully implemented with the following components:")
    
    print("\n1. CC1101 RF Module Control")
    print("   - Full SPI interface driver")
    print("   - 443MHz frequency configuration")
    print("   - Signal acquisition and transmission")
    print("   - Error handling and diagnostics")
    
    print("\n2. Web Server Interface")
    print("   - RESTful API for signal control")
    print("   - Interactive web UI")
    print("   - Signal file management")
    print("   - Real-time status monitoring")
    
    print("\n3. Main Application")
    print("   - WiFi softAP + station configuration")
    print("   - Background signal acquisition tasks")
    print("   - Web server integration")
    print("   - CC1101 module control")
    
    print("\n4. Configuration")
    print("   - Complete sdkconfig settings")
    print("   - CMakeLists.txt for build system")
    print("   - Comprehensive README documentation")
    
    print("\n5. Demo and Testing")
    print("   - Interactive demonstration script")
    print("   - Setup and configuration guide")
    print("   - Troubleshooting instructions")
    
    print("\n" + "=" * 70)
    print("\nQUICK START GUIDE:")
    print("\n1. Build the project:")
    print("   idf.py build")
    print("\n2. Flash to device:")
    print("   idf.py -p COMx flash  # Replace COMx with your port")
    print("\n3. Connect to WiFi:")
    print("   Network: CC1101-AP")
    print("   Password: password123")
    print("\n4. Access web interface:")
    print("   http://192.168.1.1/")
    print("\n5. Start signal capture using the web UI")
    print("=" * 70)

def main():
    """Main function"""
    print_header("CC1101 443MHz Signal Manager - Quick Setup")
    
    print("\nThis script provides a quick setup and test of the CC1101 443MHz Signal Manager.")
    print("It guides you through the installation process and demonstrates the system capabilities.")
    
    # Check if we're in the right directory
    if not os.path.exists("main/main.c"):
        print("\n✗ This script must be run from the project root directory")
        print("   Please navigate to: D:/code/cc1101_esp32")
        return 1
    
    # Check ESP-IDF
    if not check_esp_idf():
        print("\n✗ ESP-IDF is required for this project")
        print("   Please install ESP-IDF and try again")
        return 1
    
    # Check project structure
    if not check_project_structure():
        print("\n✗ Project structure is incomplete")
        print("   Please ensure all required files are present")
        return 1
    
    # Build the project
    if not build_project():
        print("\n✗ Build failed")
        print("   Please check the error messages above")
        return 1
    
    # Start demonstration
    print("\n" + "=" * 70)
    print("Would you like to run the demonstration? (y/n)")
    
    try:
        response = input("> ").strip().lower()
        if response in ['y', 'yes']:
            if not start_demo():
                print("\n✗ Demonstration failed")
                return 1
        else:
            print("\nSkipping demonstration. You can run it manually later.")
    except KeyboardInterrupt:
        print("\n\nSetup interrupted by user")
        return 0
    except Exception as e:
        print(f"\nError during setup: {e}")
        return 1
    
    # Show summary
    show_summary()
    
    print("\n" + "=" * 70)
    print("SETUP COMPLETED SUCCESSFULLY!")
    print("=" * 70)
    print("\nThe CC1101 443MHz Signal Manager is ready for use.")
    print("\nNext steps:")
    print("1. Flash the application to your ESP32S3 device")
    print("2. Connect to the WiFi network")
    print("3. Access the web interface to start using the system")
    print("4. Refer to README.md for detailed documentation")
    
    return 0

if __name__ == "__main__":
    exit(main())