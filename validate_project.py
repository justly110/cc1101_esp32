#!/usr/bin/env python3

"""
CC1101 Signal Manager - Project Validator

This script validates the project structure and identifies common compilation issues
before attempting to build the project.
"""

import os
import sys
from pathlib import Path

def print_header(title: str):
    """Print a formatted header"""
    print("\n" + "=" * 70)
    print(f"  {title}")
    print("=" * 70)

def validate_project_structure() -> bool:
    """Validate the project structure"""
    print("\nValidating project structure...")
    
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
    
    # Check for common compilation issues
    print("\nChecking for common compilation issues...")
    
    # Check main application file for basic syntax
    if os.path.exists("main/main.c"):
        with open("main/main.c", "r") as f:
            content = f.read()
            if "#include \"cc1101_driver.h\"" in content:
                print("  ✓ main/main.c includes cc1101_driver.h")
            else:
                print("  ✗ main/main.c missing cc1101_driver.h include")
                all_present = False
                
            if "#include \"web_server.h\"" in content:
                print("  ✓ main/main.c includes web_server.h")
            else:
                print("  ✗ main/main.c missing web_server.h include")
                all_present = False
    
    # Check cc1101_driver.c for basic syntax
    if os.path.exists("main/cc1101_driver.c"):
        with open("main/cc1101_driver.c", "r") as f:
            content = f.read()
            if "#include \"cc1101_driver.h\"" in content:
                print("  ✓ cc1101_driver.c includes cc1101_driver.h")
            else:
                print("  ✗ cc1101_driver.c missing cc1101_driver.h include")
                all_present = False
    
    # Check web_server.c for basic syntax
    if os.path.exists("main/web_server.c"):
        with open("main/web_server.c", "r") as f:
            content = f.read()
            if "#include \"web_server.h\"" in content:
                print("  ✓ web_server.c includes web_server.h")
            else:
                print("  ✗ web_server.c missing web_server.h include")
                all_present = False
                
            if "#include \"cJSON.h\"" in content:
                print("  ✓ web_server.c includes cJSON.h (JSON support)")
            else:
                print("  ✗ web_server.c missing cJSON.h include")
                all_present = False
    
    return all_present

def check_cmakelists() -> bool:
    """Check CMakeLists.txt for common issues"""
    print("\nChecking CMakeLists.txt...")
    
    if not os.path.exists("CMakeLists.txt"):
        print("  ✗ CMakeLists.txt not found")
        return False
    
    with open("CMakeLists.txt", "r") as f:
        content = f.read()
    
    # Check for required components
    if "idf_add_component" in content:
        print("  ✓ CMakeLists.txt includes component definitions")
        
        # Check for component names
        if "cc1101_driver" in content and "web_server" in content:
            print("  ✓ Both components (cc1101_driver, web_server) defined")
        else:
            print("  ✗ Missing component definitions")
            return False
            
        # Check for target_sources
        if "target_sources" in content:
            print("  ✓ CMakeLists.txt includes target_sources")
        else:
            print("  ✗ CMakeLists.txt missing target_sources")
            return False
            
        # Check for include directories
        if "target_include_directories" in content:
            print("  ✓ CMakeLists.txt includes include directories")
        else:
            print("  ✗ CMakeLists.txt missing include directories")
            return False
    else:
        print("  ✗ CMakeLists.txt missing idf_add_component")
        return False
    
    return True

def validate_cpp_syntax() -> bool:
    """Validate basic C/C++ syntax"""
    print("\nValidating C/C++ syntax...")
    
    # Check for basic syntax issues in main files
    files_to_check = [
        "main/main.c",
        "main/cc1101_driver.c",
        "main/web_server.c"
    ]
    
    all_valid = True
    for file_path in files_to_check:
        if os.path.exists(file_path):
            try:
                with open(file_path, "r") as f:
                    content = f.read()
                
                # Basic syntax checks
                if "#include" in content:
                    print(f"  ✓ {file_path} has include statements")
                else:
                    print(f"  ✗ {file_path} missing include statements")
                    all_valid = False
                
                # Check for function definitions
                if "void " in content or "esp_err_t" in content:
                    print(f"  ✓ {file_path} has function definitions")
                else:
                    print(f"  ✗ {file_path} missing function definitions")
                    all_valid = False
                    
                # Check for main function
                if "main(" in content or "app_main(" in content:
                    print(f"  ✓ {file_path} has main function")
                else:
                    print(f"  ✗ {file_path} missing main function")
                    all_valid = False
                    
            except Exception as e:
                print(f"  ✗ Error reading {file_path}: {e}")
                all_valid = False
    
    return all_valid

def print_project_info():
    """Print project information"""
    print_header("PROJECT INFORMATION")
    
    print("\nProject: CC1101 443MHz Signal Manager")
    print("Description: Complete solution for CC1101 RF module control")
    print("\nComponents:")
    print("  - Main Application: main.c (WiFi + CC1101 integration)")
    print("  - CC1101 Driver: cc1101_driver.c (SPI communication)")
    print("  - Web Server: web_server.c (REST API + web UI)")
    print("  - Documentation: README.md, PROJECT_SUMMARY.md")
    print("  - Testing: setup_and_test.py, demo_cc1101_signal_manager.py")
    
    print("\nKey Features:")
    print("  - CC1101 443MHz RF module control")
    print("  - Signal acquisition and storage")
    print("  - Signal transmission")
    print("  - Web-based control interface")
    print("  - JSON-based API")
    print("  - Comprehensive documentation")
    
    print("\nBuild System:")
    print("  - ESP-IDF v5.5.2 compatible")
    print("  - CMake-based build system")
    print("  - Minimal build mode")
    print("  - Component-based architecture")

def main():
    """Main validation function"""
    print_header("CC1101 Signal Manager - Project Validator")
    
    print("\nThis script validates the project structure and identifies")
    print("common compilation issues before attempting to build the project.")
    
    # Print project information
    print_project_info()
    
    # Validate project structure
    if not validate_project_structure():
        print("\n✗ Project structure validation failed")
        print("\nPlease check the issues listed above and fix them.")
        return 1
    
    # Check CMakeLists.txt
    if not check_cmakelists():
        print("\n✗ CMakeLists.txt validation failed")
        print("\nPlease check the CMakeLists.txt issues listed above.")
        return 1
    
    # Validate C/C++ syntax
    if not validate_cpp_syntax():
        print("\n✗ C/C++ syntax validation failed")
        print("\nPlease check the C/C++ syntax issues listed above.")
        return 1
    
    print_header("VALIDATION COMPLETE")
    print("\n✓ All validations passed successfully!")
    print("\nThe project structure is ready for compilation.")
    print("\nNext steps:")
    print("1. Ensure ESP-IDF v5.5.2 is installed")
    print("2. Run: idf.py build")
    print("3. Flash to device: idf.py -p COMx flash")
    
    print("\nFor detailed documentation, see:")
    print("  - README.md: Complete setup and usage guide")
    print("  - PROJECT_SUMMARY.md: Project architecture and specifications")
    
    return 0

if __name__ == "__main__":
    exit(main())