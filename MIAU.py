#!/usr/bin/env python3
"""
MIAU - Monolithic Ino Aggregation Utility 🐱
MicroKernel Assembler Utility
Assembles multiple .txt module files into a single Arduino .ino file
This utility is mostly AI-made. I use C++, don't know Python well...
"""

import os
import sys
import re
import hashlib
import argparse
from pathlib import Path
from datetime import datetime


def print_banner():
    """Print the MIAU banner"""
    print("\n" + "="*60)
    print("  MIAU - Monolithic Ino Aggregation Utility 🐱")
    print("="*60 + "\n")


def format_size(bytes_size):
    """Format bytes into human-readable size"""
    if bytes_size < 1024:
        return f"{bytes_size} bytes"
    elif bytes_size < 1024 * 1024:
        return f"{bytes_size / 1024:.2f} KB"
    else:
        return f"{bytes_size / (1024 * 1024):.2f} MB"


def calculate_hash(file_path):
    """Calculate SHA256 hash of a file"""
    sha256 = hashlib.sha256()
    with open(file_path, 'rb') as f:
        for chunk in iter(lambda: f.read(4096), b''):
            sha256.update(chunk)
    return sha256.hexdigest()


def validate_project_structure(project_path):
    """Validate that project has required folders"""
    inc_path = project_path / "inc"
    excludes_path = project_path / "excludes"
    
    if not inc_path.exists() or not inc_path.is_dir():
        print(f"[ERROR] 'inc' folder not found in {project_path}")
        return None, None
    
    if not excludes_path.exists() or not excludes_path.is_dir():
        print(f"[ERROR] 'excludes' folder not found in {project_path}")
        return None, None
    
    return inc_path, excludes_path


def get_numbered_files(inc_path):
    """Get all numbered module files in sequence"""
    files = []
    pattern = re.compile(r'^0*(\d+)_.*\.txt$')
    
    for file in sorted(inc_path.glob("*.txt")):
        match = pattern.match(file.name)
        if match:
            num = int(match.group(1))
            files.append((num, file))
    
    files.sort(key=lambda x: x[0])
    return files


def validate_file_sequence(files):
    """Ensure files are numbered sequentially from 01 to N"""
    if not files:
        print("[ERROR] No numbered module files found in inc folder")
        return False
    
    expected = 1
    for num, file in files:
        if num != expected:
            print(f"[ERROR] File sequence broken. Expected {expected:02d}, found {num:02d}")
            print(f"        Missing: {expected:02d}_*.txt")
            return False
        expected += 1
    
    max_num = files[-1][0]
    print(f"[OK] Found {max_num} module files in correct sequence")
    return True


def check_file_encoding(file_path):
    """Check if file is valid UTF-8"""
    try:
        with open(file_path, 'r', encoding='utf-8') as f:
            f.read()
        return True
    except UnicodeDecodeError:
        return False


def validate_file_contents(files):
    """Validate that all module files are readable and have content"""
    all_valid = True
    total_size = 0
    
    print("\n[INFO] Validating module files...")
    
    for num, file in files:
        if not check_file_encoding(file):
            print(f"[WARNING] {file.name} has encoding issues (not UTF-8)")
            all_valid = False
            continue
        
        size = file.stat().st_size
        if size == 0:
            print(f"[WARNING] {file.name} is empty")
        
        total_size += size
        print(f"  [{num:02d}] {file.name}: {format_size(size)}")
    
    print(f"\n[INFO] Total source size: {format_size(total_size)}")
    
    return all_valid


def get_output_filename(excludes_path, custom_name=None):
    """Read output filename from VersionMetadata.txt or use custom name"""
    if custom_name:
        filename = custom_name
        if not filename.endswith('.ino'):
            filename += '.ino'
        print(f"[OK] Using custom output filename: {filename}")
        return filename
    
    metadata_file = excludes_path / "VersionMetadata.txt"
    
    if not metadata_file.exists():
        print(f"[ERROR] VersionMetadata.txt not found in excludes folder")
        return None
    
    try:
        content = metadata_file.read_text(encoding='utf-8').strip()
        if not content:
            print("[ERROR] VersionMetadata.txt is empty")
            return None
        
        lines = [line.strip() for line in content.split('\n') if line.strip()]
        if not lines:
            print("[ERROR] VersionMetadata.txt contains no valid filename")
            return None
        
        filename = lines[0]
        filename = filename.replace('/', '_').replace('\\', '_')
        
        if not filename.endswith('.ino'):
            filename += '.ino'
        
        print(f"[OK] Output filename: {filename}")
        return filename
    
    except Exception as e:
        print(f"[ERROR] Failed to read VersionMetadata.txt: {e}")
        return None


def get_dev_notes(excludes_path):
    """Read and format DevNotes.txt as comments if not empty"""
    devnotes_file = excludes_path / "DevNotes.txt"
    
    if not devnotes_file.exists():
        print("[INFO] DevNotes.txt not found, skipping")
        return None
    
    try:
        content = devnotes_file.read_text(encoding='utf-8').strip()
        if not content:
            print("[INFO] DevNotes.txt is empty, skipping")
            return None
        
        lines = content.split('\n')
        commented = ["/*", " * Developer Notes", " * " + "="*50]
        for line in lines:
            commented.append(" * " + line)
        commented.append(" */\n")
        
        print("[OK] DevNotes.txt will be included as comments")
        return '\n'.join(commented)
    
    except Exception as e:
        print(f"[WARNING] Failed to read DevNotes.txt: {e}")
        return None


def get_changelog(excludes_path):
    """Read CHANGELOG.txt if present"""
    changelog_file = excludes_path / "CHANGELOG.txt"
    
    if not changelog_file.exists():
        return None
    
    try:
        content = changelog_file.read_text(encoding='utf-8').strip()
        if not content:
            return None
        
        lines = content.split('\n')
        commented = ["/*", " * Change Log", " * " + "="*50]
        for line in lines:
            commented.append(" * " + line)
        commented.append(" */\n")
        
        print("[OK] CHANGELOG.txt will be included as comments")
        return '\n'.join(commented)
    
    except Exception as e:
        print(f"[WARNING] Failed to read CHANGELOG.txt: {e}")
        return None


def strip_comments(code):
    """Remove all comments from code (C/C++ style)"""
    code = re.sub(r'//.*?$', '', code, flags=re.MULTILINE)
    code = re.sub(r'/\*.*?\*/', '', code, flags=re.DOTALL)
    return code


def minimize_whitespace(code):
    """Remove all unnecessary whitespace while preserving syntax"""
    lines = []
    for line in code.split('\n'):
        line = line.strip()
        if line:
            line = re.sub(r'\s+', ' ', line)
            line = re.sub(r'\s*([{}()\[\];,=+\-*/<>!&|])\s*', r'\1', line)
            line = re.sub(r'\b(if|for|while|switch|return|else)\(', r'\1 (', line)
            lines.append(line)
    
    return '\n'.join(lines)


def check_output_exists(output_path):
    """Check if output file exists and offer to backup"""
    if not output_path.exists():
        return True
    
    print(f"\n[WARNING] Output file already exists: {output_path.name}")
    
    timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
    backup_name = output_path.stem + f"_backup_{timestamp}" + output_path.suffix
    backup_path = output_path.parent / backup_name
    
    try:
        import shutil
        shutil.copy2(output_path, backup_path)
        print(f"[OK] Created backup: {backup_name}")
        return True
    except Exception as e:
        print(f"[ERROR] Failed to create backup: {e}")
        
        response = input("Continue anyway? (y/n): ").strip().lower()
        return response == 'y'


def detect_arduino_keywords(content):
    """Detect common Arduino functions to verify it's Arduino code"""
    arduino_keywords = [
        'void setup()', 'void loop()', 'pinMode', 'digitalWrite', 
        'digitalRead', 'analogWrite', 'analogRead', 'Serial.begin',
        'delay', 'millis', 'micros'
    ]
    
    found = []
    for keyword in arduino_keywords:
        if keyword in content:
            found.append(keyword)
    
    return found


def assemble_ino(files, output_path, excludes_path, args):
    """Assemble all module files into single .ino file"""
    print(f"\n[INFO] Assembling modules into {output_path.name}...")
    
    if args.no_comments:
        print("[INFO] Comment stripping enabled")
    if args.minimal:
        print("[INFO] Minimal whitespace mode enabled")
    if args.no_changelog:
        print("[INFO] Changelog will be omitted")
    
    module_hashes = []
    arduino_detected = False
    
    try:
        with open(output_path, 'w', encoding='utf-8') as out:
            timestamp = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
            
            if not args.no_comments:
                out.write("/*\n")
                out.write(" * Monolithic INO - Auto-generated by MIAU 🐱\n")
                out.write(f" * Generated: {timestamp}\n")
                out.write(f" * Total Modules: {len(files)}\n")
                out.write(" * \n")
                out.write(" * DO NOT EDIT THIS FILE DIRECTLY\n")
                out.write(" * Edit individual module files in the inc/ folder instead\n")
                out.write(" */\n\n")
            
            if not args.no_changelog and not args.no_comments:
                changelog = get_changelog(excludes_path)
                if changelog:
                    out.write(changelog)
                    out.write("\n")
            
            if not args.no_comments:
                dev_notes = get_dev_notes(excludes_path)
                if dev_notes:
                    out.write(dev_notes)
                    out.write("\n")
                
                out.write("/*\n")
                out.write(" * MODULE INDEX\n")
                out.write(" * " + "="*50 + "\n")
                for num, file in files:
                    out.write(f" * [{num:02d}] {file.stem}\n")
                out.write(" */\n\n")
            
            for num, file in files:
                print(f"  [->] Adding {file.name}")
                
                content = file.read_text(encoding='utf-8')
                
                if not arduino_detected:
                    keywords = detect_arduino_keywords(content)
                    if keywords:
                        arduino_detected = True
                        print(f"       [Arduino code detected: {', '.join(keywords[:3])}]")
                
                if args.no_comments:
                    content = strip_comments(content)
                
                if args.minimal:
                    content = minimize_whitespace(content)
                
                file_hash = calculate_hash(file)
                module_hashes.append(f"{file.name}: {file_hash[:8]}")
                
                if not args.no_comments:
                    out.write(f"// {'='*70}\n")
                    out.write(f"// MODULE {num:02d}: {file.stem}\n")
                    out.write(f"// Source: {file.name}\n")
                    out.write(f"// Hash: {file_hash[:16]}...\n")
                    out.write(f"// {'='*70}\n\n")
                
                out.write(content)
                
                if not args.minimal:
                    if not content.endswith('\n\n'):
                        out.write('\n\n' if content.endswith('\n') else '\n\n')
                else:
                    if not content.endswith('\n'):
                        out.write('\n')
            
            if not args.no_comments:
                out.write(f"\n// {'='*70}\n")
                out.write("// END OF MONOLITHIC INO\n")
                out.write("// Generated by MIAU - Monolithic Ino Aggregation Utility\n")
                out.write(f"// Total modules assembled: {len(files)}\n")
                out.write(f"// Generated: {timestamp}\n")
                out.write(f"// {'='*70}\n")
        
        file_size = output_path.stat().st_size
        
        print(f"\n[SUCCESS] Output written to: {output_path}")
        print(f"[SUCCESS] Total size: {format_size(file_size)}")
        
        if arduino_detected:
            print("[INFO] Arduino code detected in modules")
        else:
            print("[WARNING] No obvious Arduino code detected. Verify module contents.")
        
        total_source = sum(f.stat().st_size for _, f in files)
        if file_size < total_source:
            savings = total_source - file_size
            print(f"[INFO] Size reduction: {format_size(savings)} ({savings/total_source*100:.1f}%)")
        else:
            overhead = file_size - total_source
            print(f"[INFO] Header/comment overhead: {format_size(overhead)} ({overhead/file_size*100:.1f}%)")
        
        return True
    
    except Exception as e:
        print(f"\n[ERROR] Failed to write output file: {e}")
        return False


def ensure_build_folder(project_path):
    """Create build folder if it doesn't exist"""
    build_path = project_path / "build"
    
    if not build_path.exists():
        try:
            build_path.mkdir(parents=True, exist_ok=True)
            print(f"[OK] Created build folder: {build_path}")
        except Exception as e:
            print(f"[ERROR] Failed to create build folder: {e}")
            return None
    else:
        print(f"[OK] Using existing build folder: {build_path}")
    
    return build_path


def parse_arguments():
    """Parse command line arguments"""
    parser = argparse.ArgumentParser(
        description='MIAU - Monolithic Ino Aggregation Utility 🐱',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  %(prog)s                           # Interactive mode
  %(prog)s /path/to/project          # Specify project path
  %(prog)s --custom-name MySketch    # Custom output name
  %(prog)s --no-comments --minimal   # Minimal output
  %(prog)s --no-changelog            # Omit changelog only

Flags:
  --no-comments    Remove ALL comments from output (including MIAU headers)
  --no-changelog   Omit CHANGELOG.txt (keeps other comments)
  --minimal        Remove all unnecessary whitespace
  --custom-name    Override VersionMetadata.txt filename
  --help, -h       Show this help message
        """
    )
    
    parser.add_argument(
        'project_path',
        nargs='?',
        default=None,
        help='Path to project directory (if omitted, will prompt)'
    )
    
    parser.add_argument(
        '--no-comments',
        action='store_true',
        help='Strip all comments from output (including MIAU-generated comments)'
    )
    
    parser.add_argument(
        '--custom-name',
        metavar='NAME',
        help='Use custom output filename instead of VersionMetadata.txt'
    )
    
    parser.add_argument(
        '--no-changelog',
        action='store_true',
        help='Omit CHANGELOG.txt from output'
    )
    
    parser.add_argument(
        '--minimal',
        action='store_true',
        help='Remove all unnecessary whitespace from code'
    )
    
    return parser.parse_args()


def show_config_summary(args, output_filename, build_path):
    """Show configuration summary and ask for confirmation"""
    print("\n" + "="*60)
    print("ASSEMBLY CONFIGURATION")
    print("="*60)
    print(f"Output folder:    {build_path}")
    print(f"Output file:      {output_filename}")
    print(f"Strip comments:   {'YES' if args.no_comments else 'NO'}")
    print(f"Minimal mode:     {'YES' if args.minimal else 'NO'}")
    print(f"Include changelog:{'NO' if args.no_changelog else 'YES'}")
    if args.custom_name:
        print(f"Custom name:      {args.custom_name}")
    print("="*60 + "\n")
    
    response = input("Proceed with assembly? (y/n): ").strip().lower()
    return response == 'y'


def print_interactive_help():
    """Print help for interactive mode"""
    print("\n" + "="*60)
    print("MIAU INTERACTIVE SHELL COMMANDS")
    print("="*60)
    print("  help              - Show this help message")
    print("  exit, quit        - Exit MIAU")
    print("  <path>            - Specify project directory path")
    print("  .                 - Use current directory")
    print("  clear, cls        - Clear the screen")
    print("\n  Examples:")
    print("    MIAU~> /home/user/myproject")
    print("    MIAU~> ~/Desktop/Arduino_Project")
    print("    MIAU~> .")
    print("="*60 + "\n")


def interactive_mode():
    """Interactive mode for getting project path with mini shell"""
    print("[INFO] Welcome to MIAU Interactive Shell!")
    print("[INFO] Type 'help' for commands or enter a project path\n")
    
    while True:
        try:
            user_input = input("MIAU~> ").strip()
        except (KeyboardInterrupt, EOFError):
            print("\n[INFO] Exiting MIAU...")
            return None
        
        if not user_input:
            continue
        
        command_lower = user_input.lower()
        
        if command_lower in ['help', 'h', '?']:
            print_interactive_help()
            continue
        
        if command_lower in ['exit', 'quit', 'q']:
            print("[INFO] Exiting MIAU... Goodbye! (=^･ω･^=)")
            return None
        
        if command_lower in ['clear', 'cls']:
            os.system('clear' if os.name != 'nt' else 'cls')
            print_banner()
            continue
        
        user_input = user_input.strip('\'"')
        
        if user_input == '.':
            user_input = os.getcwd()
        
        project_path = Path(user_input).expanduser().resolve()
        
        if project_path.exists() and project_path.is_dir():
            print(f"[OK] Selected: {project_path}")
            return project_path
        else:
            print(f"[ERROR] Invalid path: {project_path}")
            print("[INFO] Type 'help' for commands or try again")
            continue


def main():
    args = parse_arguments()
    
    print_banner()
    
    if args.project_path is None:
        project_path = interactive_mode()
        if project_path is None:
            print("[ABORT] No valid project path provided")
            sys.exit(1)
    else:
        project_path = Path(args.project_path).resolve()
    
    print(f"\n[INFO] Project path: {project_path}\n")
    
    if not project_path.exists() or not project_path.is_dir():
        print(f"[ERROR] Invalid project path: {project_path}")
        sys.exit(1)
    
    inc_path, excludes_path = validate_project_structure(project_path)
    if not inc_path or not excludes_path:
        sys.exit(1)
    
    files = get_numbered_files(inc_path)
    if not validate_file_sequence(files):
        sys.exit(1)
    
    if not validate_file_contents(files):
        response = input("\n[WARNING] Some files have issues. Continue? (y/n): ").strip().lower()
        if response != 'y':
            print("[ABORT] Assembly cancelled")
            sys.exit(1)
    
    output_filename = get_output_filename(excludes_path, args.custom_name)
    if not output_filename:
        sys.exit(1)
    
    build_path = ensure_build_folder(project_path)
    if not build_path:
        sys.exit(1)
    
    output_path = build_path / output_filename
    
    if not show_config_summary(args, output_filename, build_path):
        print("[ABORT] Assembly cancelled by user")
        sys.exit(1)
    
    if not check_output_exists(output_path):
        print("[ABORT] Assembly cancelled")
        sys.exit(1)
    
    if assemble_ino(files, output_path, excludes_path, args):
        print("\n" + "="*60)
        print("[DONE] MIAU says: Assembly complete! (=^･ω･^=)")
        print("="*60)
    else:
        print("\n[FAILED] MIAU says: Something went wrong... (>_<)")
        sys.exit(1)


if __name__ == "__main__":
    main()
