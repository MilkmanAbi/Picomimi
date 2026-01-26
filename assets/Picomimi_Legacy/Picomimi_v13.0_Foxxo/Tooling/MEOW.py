#!/usr/bin/env python3
"""
MEOW - MicroOS Engineering Orchestration Workbench
The unified Picomimi development toolchain

Combines: MIAU, MRRP, NYAA, MROW
Made with determination ฅ(•ㅅ•❀)ฅ
"""

import sys
import os
import json
import re
from pathlib import Path
from typing import List, Dict, Tuple, Optional

# ============================================================================
# COLOR CODES
# ============================================================================
class Color:
    RESET = '\033[0m'
    WHITE = '\033[97m'
    RED = '\033[91m'
    GREEN = '\033[92m'
    ORANGE = '\033[38;5;214m'
    PURPLE = '\033[95m'
    BOLD = '\033[1m'
    
    @staticmethod
    def success(msg): return f"{Color.GREEN}{msg}{Color.RESET}"
    @staticmethod
    def error(msg): return f"{Color.RED}{msg}{Color.RESET}"
    @staticmethod
    def warning(msg): return f"{Color.ORANGE}{msg}{Color.RESET}"
    @staticmethod
    def unsure(msg): return f"{Color.PURPLE}{msg}{Color.RESET}"
    @staticmethod
    def info(msg): return f"{Color.WHITE}{msg}{Color.RESET}"
    @staticmethod
    def bold(msg): return f"{Color.BOLD}{msg}{Color.RESET}"

# ============================================================================
# MRRP - Monolithic Repartition & Refactor Program
# ============================================================================
class MRRP:
    """
    Splits monolithic .ino into modules based on config
    
    Config format:
    {
        "modules": [
            {
                "name": "Module Name",
                "filename": "01_ModuleName.txt",
                "start_marker": "// MODULE 1:",
                "end_marker": "// END MODULE 1"
            }
        ]
    }
    """
    
    def __init__(self):
        self.config = None
        
    def load_config(self, config_path: str) -> bool:
        """Load MRRP configuration file"""
        try:
            with open(config_path, 'r', encoding='utf-8') as f:
                self.config = json.load(f)
            print(Color.success(f"✓ Loaded config: {config_path} ฅ(•ㅅ•❀)ฅ"))
            print(Color.info(f"  Modules defined: {len(self.config.get('modules', []))}"))
            return True
        except FileNotFoundError:
            print(Color.error(f"✗ Config file not found: {config_path}"))
            return False
        except json.JSONDecodeError as e:
            print(Color.error(f"✗ Invalid JSON in config: {e}"))
            return False
        except Exception as e:
            print(Color.error(f"✗ Failed to load config: {e}"))
            return False
    
    def split(self, source_file: str, output_dir: str) -> bool:
        """Split monolithic file into modules"""
        if not self.config:
            print(Color.error("✗ No config loaded! Use 'load <config.json>' first"))
            return False
        
        print(Color.info(f"\n{'='*70}"))
        print(Color.bold("MRRP - Starting module extraction ฅ(•ㅅ•❀)ฅ"))
        print(Color.info(f"{'='*70}\n"))
        
        # Read source file
        try:
            with open(source_file, 'r', encoding='utf-8', errors='replace') as f:
                lines = f.readlines()
            print(Color.success(f"✓ Loaded source: {source_file}"))
            print(Color.info(f"  Total lines: {len(lines)}"))
        except Exception as e:
            print(Color.error(f"✗ Failed to read source: {e}"))
            return False
        
        # Create output directory
        Path(output_dir).mkdir(parents=True, exist_ok=True)
        print(Color.success(f"✓ Output directory: {output_dir}\n"))
        
        # Extract modules based on config
        modules_extracted = 0
        total_lines_extracted = 0
        
        for module_cfg in self.config.get('modules', []):
            lines_extracted = self._extract_module(lines, module_cfg, output_dir)
            if lines_extracted > 0:
                modules_extracted += 1
                total_lines_extracted += lines_extracted
        
        print(Color.info(f"\n{'='*70}"))
        print(Color.success(f"✓ Extraction complete! ฅ(•ㅅ•❀)ฅ"))
        print(Color.info(f"  Modules extracted: {modules_extracted}/{len(self.config.get('modules', []))}"))
        print(Color.info(f"  Total lines: {total_lines_extracted}"))
        print(Color.info(f"{'='*70}\n"))
        
        return modules_extracted > 0
    
    def _extract_module(self, lines: List[str], module_cfg: Dict, output_dir: str) -> int:
        """Extract a single module based on config"""
        name = module_cfg['name']
        filename = module_cfg['filename']
        start_marker = module_cfg.get('start_marker', '')
        end_marker = module_cfg.get('end_marker', '')
        
        # If no markers, use line ranges
        if 'start_line' in module_cfg and 'end_line' in module_cfg:
            start_idx = module_cfg['start_line'] - 1
            end_idx = module_cfg['end_line']
            module_lines = lines[start_idx:end_idx]
            
            output_path = os.path.join(output_dir, filename)
            try:
                with open(output_path, 'w', encoding='utf-8') as f:
                    f.writelines(module_lines)
                print(Color.success(f"  ✓ {name}: {len(module_lines)} lines → {filename}"))
                return len(module_lines)
            except Exception as e:
                print(Color.error(f"  ✗ Failed to write {filename}: {e}"))
                return 0
        
        # Use markers
        print(Color.info(f"  Searching: {name}..."))
        
        start_idx = None
        end_idx = None
        
        for i, line in enumerate(lines):
            if start_marker and start_marker in line:
                start_idx = i
                print(Color.unsure(f"    ? Found start at line {i+1}"))
            if end_marker and end_marker in line and start_idx is not None:
                end_idx = i + 1
                print(Color.unsure(f"    ? Found end at line {i+1}"))
                break
        
        if start_idx is None:
            print(Color.warning(f"  ⚠ Could not find start marker for {name}"))
            print(Color.warning(f"    Looking for: '{start_marker}'"))
            return 0
        
        if end_idx is None:
            print(Color.warning(f"  ⚠ Could not find end marker for {name}"))
            print(Color.warning(f"    Looking for: '{end_marker}'"))
            # Use end of file
            end_idx = len(lines)
        
        # Extract module content
        module_lines = lines[start_idx:end_idx]
        
        # Write to file
        output_path = os.path.join(output_dir, filename)
        try:
            with open(output_path, 'w', encoding='utf-8') as f:
                f.writelines(module_lines)
            print(Color.success(f"  ✓ {name}: {len(module_lines)} lines → {filename}"))
            return len(module_lines)
        except Exception as e:
            print(Color.error(f"  ✗ Failed to write {filename}: {e}"))
            return 0
    
    def verify(self, output_dir: str) -> bool:
        """Verify extracted modules"""
        if not self.config:
            print(Color.error("✗ No config loaded!"))
            return False
        
        print(Color.info(f"\n{'='*70}"))
        print(Color.bold("MRRP - Verifying extracted modules ฅ(•ㅅ•❀)ฅ"))
        print(Color.info(f"{'='*70}\n"))
        
        all_ok = True
        total_lines = 0
        
        for module_cfg in self.config.get('modules', []):
            filename = module_cfg['filename']
            filepath = os.path.join(output_dir, filename)
            
            if not os.path.exists(filepath):
                print(Color.error(f"  ✗ Missing: {filename}"))
                all_ok = False
            else:
                with open(filepath, 'r', encoding='utf-8', errors='replace') as f:
                    lines = len(f.readlines())
                total_lines += lines
                print(Color.success(f"  ✓ {filename}: {lines} lines"))
        
        print(Color.info(f"\n{'='*70}"))
        if all_ok:
            print(Color.success(f"✓ All modules verified! ฅ(•ㅅ•❀)ฅ"))
            print(Color.info(f"  Total lines across all modules: {total_lines}"))
        else:
            print(Color.error(f"✗ Some modules missing!"))
        print(Color.info(f"{'='*70}\n"))
        
        return all_ok

# ============================================================================
# MIAU - Monolithic INO Aggregator Utility
# ============================================================================
class MIAU:
    """
    Assembles modules back into monolithic .ino
    
    Config format (same as MRRP):
    {
        "header": ["// Header comment\n", "// Version info\n"],
        "modules": [...]
    }
    """
    
    def __init__(self):
        self.config = None
    
    def load_config(self, config_path: str) -> bool:
        """Load MIAU configuration"""
        try:
            with open(config_path, 'r', encoding='utf-8') as f:
                self.config = json.load(f)
            print(Color.success(f"✓ Loaded config: {config_path} ฅ(•ㅅ•❀)ฅ"))
            print(Color.info(f"  Modules to assemble: {len(self.config.get('modules', []))}"))
            return True
        except Exception as e:
            print(Color.error(f"✗ Failed to load config: {e}"))
            return False
    
    def assemble(self, module_dir: str, output_file: str) -> bool:
        """Assemble modules into monolithic file"""
        if not self.config:
            print(Color.error("✗ No config loaded! Use 'load <config.json>' first"))
            return False
        
        print(Color.info(f"\n{'='*70}"))
        print(Color.bold("MIAU - Assembling modules ฅ(•ㅅ•❀)ฅ"))
        print(Color.info(f"{'='*70}\n"))
        
        assembled_lines = []
        
        # Add header if present
        if 'header' in self.config:
            header = self.config['header']
            if isinstance(header, str):
                assembled_lines.append(header + '\n')
            else:
                assembled_lines.extend(header)
            assembled_lines.append('\n')
            print(Color.success(f"  ✓ Added header"))
        
        # Assemble modules in order
        missing_modules = []
        for module_cfg in self.config.get('modules', []):
            filename = module_cfg['filename']
            filepath = os.path.join(module_dir, filename)
            
            if not os.path.exists(filepath):
                print(Color.error(f"  ✗ Missing: {filename}"))
                missing_modules.append(filename)
                continue
            
            with open(filepath, 'r', encoding='utf-8', errors='replace') as f:
                module_lines = f.readlines()
            
            # Add module marker comment
            assembled_lines.append(f"\n// ========================================\n")
            assembled_lines.append(f"// MODULE: {module_cfg['name']}\n")
            assembled_lines.append(f"// ========================================\n\n")
            
            assembled_lines.extend(module_lines)
            assembled_lines.append('\n')
            print(Color.success(f"  ✓ {module_cfg['name']}: {len(module_lines)} lines"))
        
        if missing_modules:
            print(Color.error(f"\n✗ Cannot assemble: {len(missing_modules)} modules missing"))
            return False
        
        # Write output
        try:
            with open(output_file, 'w', encoding='utf-8') as f:
                f.writelines(assembled_lines)
            print(Color.info(f"\n{'='*70}"))
            print(Color.success(f"✓ Assembly complete! ฅ(•ㅅ•❀)ฅ"))
            print(Color.info(f"  Output: {output_file}"))
            print(Color.info(f"  Total lines: {len(assembled_lines)}"))
            print(Color.info(f"{'='*70}\n"))
            return True
        except Exception as e:
            print(Color.error(f"\n✗ Failed to write output: {e}"))
            return False
    
    def compare(self, assembled_file: str, original_file: str) -> bool:
        """Compare assembled file with original"""
        print(Color.info(f"\n{'='*70}"))
        print(Color.bold("MIAU - Comparing files ฅ(•ㅅ•❀)ฅ"))
        print(Color.info(f"{'='*70}\n"))
        
        try:
            with open(assembled_file, 'r', encoding='utf-8', errors='replace') as f:
                assembled_lines = f.readlines()
            with open(original_file, 'r', encoding='utf-8', errors='replace') as f:
                original_lines = f.readlines()
            
            # Remove comments for comparison
            def strip_comments(lines):
                return [l for l in lines if not l.strip().startswith('//')]
            
            assembled_code = strip_comments(assembled_lines)
            original_code = strip_comments(original_lines)
            
            if assembled_code == original_code:
                print(Color.success("✓ Files are IDENTICAL (ignoring comments)! ฅ(•ㅅ•❀)ฅ"))
                return True
            else:
                print(Color.warning("⚠ Files differ"))
                print(Color.info(f"  Assembled: {len(assembled_code)} lines"))
                print(Color.info(f"  Original: {len(original_code)} lines"))
                print(Color.info(f"  Difference: {abs(len(assembled_code) - len(original_code))} lines"))
                return False
                
        except Exception as e:
            print(Color.error(f"✗ Comparison failed: {e}"))
            return False

# ============================================================================
# NYAA - Normalize Your Architecture Automatically
# ============================================================================
class NYAA:
    """
    Surgical code editor using JSON manifests
    
    Manifest format:
    {
        "version": "1.0",
        "description": "What this manifest does",
        "remove_lines": [
            {"start": 100, "end": 150, "reason": "why"}
        ],
        "insert_code": [
            {
                "after_line": 200,
                "code": ["line1\n", "line2\n"],
                "reason": "why"
            }
        ],
        "replace_text": [
            {
                "old": "old_text",
                "new": "new_text",
                "reason": "why"
            }
        ]
    }
    """
    
    def apply_manifest(self, source_file: str, manifest_file: str, output_file: str) -> bool:
        """Apply NYAA manifest to source file"""
        print(Color.info(f"\n{'='*70}"))
        print(Color.bold("NYAA - Applying surgical edits ฅ(•ㅅ•❀)ฅ"))
        print(Color.info(f"{'='*70}\n"))
        
        # Load manifest
        try:
            with open(manifest_file, 'r', encoding='utf-8') as f:
                manifest = json.load(f)
            print(Color.success(f"✓ Loaded manifest: {manifest_file}"))
            if 'description' in manifest:
                print(Color.info(f"  Description: {manifest['description']}"))
        except Exception as e:
            print(Color.error(f"✗ Failed to load manifest: {e}"))
            return False
        
        # Load source
        try:
            with open(source_file, 'r', encoding='utf-8', errors='replace') as f:
                lines = f.readlines()
            print(Color.success(f"✓ Loaded source: {source_file} ({len(lines)} lines)\n"))
        except Exception as e:
            print(Color.error(f"✗ Failed to read source: {e}"))
            return False
        
        edits_applied = 0
        
        # 1. Text replacements (do first)
        if 'replace_text' in manifest:
            for replacement in manifest['replace_text']:
                old_text = replacement['old']
                new_text = replacement['new']
                reason = replacement.get('reason', 'No reason given')
                
                replaced = 0
                for i in range(len(lines)):
                    if old_text in lines[i]:
                        lines[i] = lines[i].replace(old_text, new_text)
                        replaced += 1
                
                if replaced > 0:
                    print(Color.success(f"  ✓ Replaced '{old_text}' → '{new_text}' ({replaced} occurrences)"))
                    edits_applied += 1
                else:
                    print(Color.warning(f"  ⚠ Text not found: '{old_text}'"))
        
        # 2. Remove lines (process in reverse order)
        if 'remove_lines' in manifest:
            removals = sorted(manifest['remove_lines'], key=lambda x: x['start'], reverse=True)
            for removal in removals:
                start = removal['start'] - 1  # 0-indexed
                end = removal['end']
                reason = removal.get('reason', 'No reason given')
                
                if start < 0 or end > len(lines):
                    print(Color.error(f"  ✗ Invalid line range: {start+1}-{end}"))
                    continue
                
                removed_count = end - start
                del lines[start:end]
                edits_applied += 1
                print(Color.success(f"  ✓ Removed lines {removal['start']}-{removal['end']} ({removed_count} lines)"))
                print(Color.info(f"    Reason: {reason}"))
        
        # 3. Insert code (process in reverse order by line number)
        if 'insert_code' in manifest:
            insertions = sorted(manifest['insert_code'], key=lambda x: x['after_line'], reverse=True)
            for insertion in insertions:
                after_line = insertion['after_line']
                code = insertion['code']
                reason = insertion.get('reason', 'No reason given')
                
                if isinstance(code, str):
                    code = [code]
                
                # Ensure newlines
                code = [line if line.endswith('\n') else line + '\n' for line in code]
                
                if after_line < 0 or after_line > len(lines):
                    print(Color.error(f"  ✗ Invalid insertion point: line {after_line}"))
                    continue
                
                lines[after_line:after_line] = code
                edits_applied += 1
                print(Color.success(f"  ✓ Inserted {len(code)} lines after line {after_line}"))
                print(Color.info(f"    Reason: {reason}"))
        
        # Write output
        try:
            with open(output_file, 'w', encoding='utf-8') as f:
                f.writelines(lines)
            print(Color.info(f"\n{'='*70}"))
            print(Color.success(f"✓ Edits applied successfully! ฅ(•ㅅ•❀)ฅ"))
            print(Color.info(f"  Output: {output_file}"))
            print(Color.info(f"  Final line count: {len(lines)}"))
            print(Color.info(f"  Edits applied: {edits_applied}"))
            print(Color.info(f"{'='*70}\n"))
            return True
        except Exception as e:
            print(Color.error(f"\n✗ Failed to write output: {e}"))
            return False

# ============================================================================
# MROW - Mend & Review Our Weirdness
# ============================================================================
class MROW:
    """Verifies code structure and syntax"""
    
    def verify(self, source_file: str) -> bool:
        """Verify source file structure"""
        print(Color.info(f"\n{'='*70}"))
        print(Color.bold("MROW - Verifying code structure ฅ(•ㅅ•❀)ฅ"))
        print(Color.info(f"{'='*70}\n"))
        
        try:
            with open(source_file, 'r', encoding='utf-8', errors='replace') as f:
                lines = f.readlines()
            print(Color.success(f"✓ Loaded: {source_file} ({len(lines)} lines)\n"))
        except Exception as e:
            print(Color.error(f"✗ Failed to read file: {e}"))
            return False
        
        issues = []
        
        # 1. Check brace balance
        print(Color.info("Checking brace balance..."))
        brace_balance = sum(line.count('{') - line.count('}') for line in lines)
        if brace_balance == 0:
            print(Color.success("  ✓ Braces balanced perfectly"))
        else:
            issues.append(f"Brace imbalance: {brace_balance}")
            print(Color.error(f"  ✗ Brace imbalance: {brace_balance}"))
        
        # 2. Check for duplicate function definitions
        print(Color.info("\nChecking for duplicate functions..."))
        funcs = {}
        for i, line in enumerate(lines, 1):
            match = re.match(r'^(void|bool|int|uint32_t|int32_t|float|uint64_t|const char\*)\s+(\w+)\s*\([^)]*\)\s*\{', line.strip())
            if match and not line.strip().startswith('//'):
                func = match.group(2)
                if func not in ['if', 'for', 'while', 'switch', 'else']:
                    if func not in funcs:
                        funcs[func] = []
                    funcs[func].append(i)
        
        dupes = {k: v for k, v in funcs.items() if len(v) > 1}
        if not dupes:
            print(Color.success(f"  ✓ No duplicate functions ({len(funcs)} unique functions found)"))
        else:
            issues.append(f"{len(dupes)} duplicate functions")
            print(Color.error(f"  ✗ Found {len(dupes)} duplicate functions:"))
            for func, locs in list(dupes.items())[:10]:
                print(Color.error(f"      {func}: lines {locs}"))
            if len(dupes) > 10:
                print(Color.error(f"      ... and {len(dupes) - 10} more"))
        
        # 3. Check for smart quotes
        print(Color.info("\nChecking for smart quotes..."))
        smart_quote_lines = []
        for i, line in enumerate(lines, 1):
            if any(c in line for c in [''', ''', '"', '"']):
                smart_quote_lines.append(i)
        
        if not smart_quote_lines:
            print(Color.success("  ✓ No smart quotes found"))
        else:
            issues.append(f"{len(smart_quote_lines)} lines with smart quotes")
            print(Color.warning(f"  ⚠ Found {len(smart_quote_lines)} lines with smart quotes"))
            if len(smart_quote_lines) <= 5:
                for line_num in smart_quote_lines:
                    print(Color.warning(f"      Line {line_num}"))
        
        # 4. Check for orphaned code
        print(Color.info("\nChecking for orphaned code..."))
        in_function = False
        brace_depth = 0
        orphaned = []
        for i, line in enumerate(lines, 1):
            stripped = line.strip()
            if not stripped or stripped.startswith('//') or stripped.startswith('/*'):
                continue
            
            # Check for function start
            if re.match(r'^(void|bool|int|uint32_t|int32_t|float|uint64_t|const char\*)\s+\w+\s*\([^)]*\)\s*\{', stripped):
                in_function = True
                brace_depth = 1
            elif in_function or brace_depth > 0:
                brace_depth += stripped.count('{') - stripped.count('}')
                if brace_depth == 0:
                    in_function = False
            elif not in_function and brace_depth == 0:
                # Code outside function
                if any(x in stripped for x in ['(', ';']) and not stripped.startswith('#') and not stripped.startswith('enum') and not stripped.startswith('struct'):
                    orphaned.append((i, stripped[:60]))
        
        if not orphaned:
            print(Color.success("  ✓ No orphaned code detected"))
        else:
            issues.append(f"{len(orphaned)} possible orphaned lines")
            print(Color.warning(f"  ⚠ Found {len(orphaned)} possibly orphaned lines"))
            for line_num, content in orphaned[:5]:
                print(Color.warning(f"      Line {line_num}: {content}"))
            if len(orphaned) > 5:
                print(Color.warning(f"      ... and {len(orphaned) - 5} more"))
        
        # Summary
        print(Color.info(f"\n{'='*70}"))
        if not issues:
            print(Color.success("✓ All checks passed! Code looks good! ฅ(•ㅅ•❀)ฅ"))
        else:
            print(Color.error(f"✗ Found {len(issues)} issue(s):"))
            for issue in issues:
                print(Color.error(f"  • {issue}"))
        print(Color.info(f"{'='*70}\n"))
        
        return len(issues) == 0

# ============================================================================
# MEOW SHELL
# ============================================================================
class MEOWShell:
    """Main MEOW interactive shell"""
    
    def __init__(self):
        self.mode = None
        self.mrrp = MRRP()
        self.miau = MIAU()
        self.nyaa = NYAA()
        self.mrow = MROW()
        
    def print_banner(self):
        """Print MEOW banner"""
        banner = """
╔═══════════════════════════════════════════════════════════════════╗
║                                                                   ║
║   MEOW - MicroOS Engineering Orchestration Workbench             ║
║   Picomimi Development Toolchain                                 ║
║                                                                   ║
║   Made with determination ฅ(•ㅅ•❀)ฅ                               ║
║                                                                   ║
╚═══════════════════════════════════════════════════════════════════╝
"""
        print(Color.bold(banner))
    
    def print_main_menu(self):
        """Print main mode selection menu"""
        print(Color.info("\nChoose mode of operation:\n"))
        print(Color.info("  1) MIAU - Monolithic INO Aggregator Utility"))
        print(Color.info("     → Assemble modules into monolithic .ino"))
        print(Color.info(""))
        print(Color.info("  2) MRRP - Monolithic Repartition & Refactor Program"))
        print(Color.info("     → Split monolithic .ino into modules"))
        print(Color.info(""))
        print(Color.info("  3) NYAA - Normalize Your Architecture Automatically"))
        print(Color.info("     → Apply surgical edits via JSON manifests"))
        print(Color.info(""))
        print(Color.info("  4) MROW - Mend & Review Our Weirdness"))
        print(Color.info("     → Verify code structure and syntax"))
        print(Color.info(""))
        print(Color.info("  5) Exit"))
        print()
    
    def get_prompt(self) -> str:
        """Get current prompt string"""
        if self.mode:
            return f"{Color.BOLD}{self.mode}~>{Color.RESET} "
        return f"{Color.BOLD}MEOW~>{Color.RESET} "
    
    def print_help(self, mode: str):
        """Print help for specific mode"""
        helps = {
            'MIAU': """
╔═══════════════════════════════════════════════════════════════════╗
║ MIAU Commands                                                     ║
╚═══════════════════════════════════════════════════════════════════╝

  load <config.json>              Load MIAU configuration
  assemble <dir> <output.ino>     Assemble modules into monolithic file
  compare <assembled> <original>  Compare assembled with original
  help                            Show this help
  back                            Return to main menu
  exit                            Exit MEOW

MIAU assembles module files back into a single .ino file based on the
configuration. It ensures modules are combined in the correct order with
proper headers and separators.
            """,
            'MRRP': """
╔═══════════════════════════════════════════════════════════════════╗
║ MRRP Commands                                                     ║
╚═══════════════════════════════════════════════════════════════════╝

  load <config.json>              Load MRRP configuration
  split <source.ino> <outdir>     Split monolithic file into modules
  verify <outdir>                 Verify extracted modules
  help                            Show this help
  back                            Return to main menu
  exit                            Exit MEOW

MRRP splits a monolithic .ino file into separate module files based on
markers defined in the configuration file. This makes the codebase more
manageable and easier to navigate.

The config file defines:
- Module names and output filenames
- Start and end markers for extraction
- Optional line ranges for direct extraction
            """,
            'NYAA': """
╔═══════════════════════════════════════════════════════════════════╗
║ NYAA Commands                                                     ║
╚═══════════════════════════════════════════════════════════════════╝

  apply <source> <manifest> <output>  Apply manifest to source file
  help                                Show this help
  back                                Return to main menu
  exit                                Exit MEOW

NYAA applies surgical edits to code using JSON manifests. You can:
- Remove specific line ranges
- Insert code at specific locations
- Replace text throughout the file

Manifests support detailed reasons for each edit and are applied in a
specific order: text replacements, then removals, then insertions.
            """,
            'MROW': """
╔═══════════════════════════════════════════════════════════════════╗
║ MROW Commands                                                     ║
╚═══════════════════════════════════════════════════════════════════╝

  verify <source.ino>             Verify code structure
  help                            Show this help
  back                            Return to main menu
  exit                            Exit MEOW

MROW verifies the structural integrity of your code:
- Checks brace balance
- Detects duplicate function definitions
- Finds smart quotes that could break compilation
- Identifies orphaned code outside functions

Use MROW before and after using MRRP/MIAU to ensure code integrity.
            """
        }
        print(Color.info(helps.get(mode, "No help available")))
    
    def handle_miau(self, cmd: str, args: List[str]):
        """Handle MIAU commands"""
        if cmd == 'load':
            if len(args) < 1:
                print(Color.error("✗ Usage: load <config.json>"))
                return
            self.miau.load_config(args[0])
        
        elif cmd == 'assemble':
            if len(args) < 2:
                print(Color.error("✗ Usage: assemble <module_dir> <output.ino>"))
                return
            self.miau.assemble(args[0], args[1])
        
        elif cmd == 'compare':
            if len(args) < 2:
                print(Color.error("✗ Usage: compare <assembled.ino> <original.ino>"))
                return
            self.miau.compare(args[0], args[1])
        
        elif cmd == 'help':
            self.print_help('MIAU')
        
        elif cmd == 'back':
            self.mode = None
        
        else:
            print(Color.error(f"✗ Unknown command: {cmd}"))
            print(Color.info("Type 'help' for available commands"))
    
    def handle_mrrp(self, cmd: str, args: List[str]):
        """Handle MRRP commands"""
        if cmd == 'load':
            if len(args) < 1:
                print(Color.error("✗ Usage: load <config.json>"))
                return
            self.mrrp.load_config(args[0])
        
        elif cmd == 'split':
            if len(args) < 2:
                print(Color.error("✗ Usage: split <source.ino> <output_dir>"))
                return
            self.mrrp.split(args[0], args[1])
        
        elif cmd == 'verify':
            if len(args) < 1:
                print(Color.error("✗ Usage: verify <output_dir>"))
                return
            self.mrrp.verify(args[0])
        
        elif cmd == 'help':
            self.print_help('MRRP')
        
        elif cmd == 'back':
            self.mode = None
        
        else:
            print(Color.error(f"✗ Unknown command: {cmd}"))
            print(Color.info("Type 'help' for available commands"))
    
    def handle_nyaa(self, cmd: str, args: List[str]):
        """Handle NYAA commands"""
        if cmd == 'apply':
            if len(args) < 3:
                print(Color.error("✗ Usage: apply <source> <manifest> <output>"))
                return
            self.nyaa.apply_manifest(args[0], args[1], args[2])
        
        elif cmd == 'help':
            self.print_help('NYAA')
        
        elif cmd == 'back':
            self.mode = None
        
        else:
            print(Color.error(f"✗ Unknown command: {cmd}"))
            print(Color.info("Type 'help' for available commands"))
    
    def handle_mrow(self, cmd: str, args: List[str]):
        """Handle MROW commands"""
        if cmd == 'verify':
            if len(args) < 1:
                print(Color.error("✗ Usage: verify <source.ino>"))
                return
            self.mrow.verify(args[0])
        
        elif cmd == 'help':
            self.print_help('MROW')
        
        elif cmd == 'back':
            self.mode = None
        
        else:
            print(Color.error(f"✗ Unknown command: {cmd}"))
            print(Color.info("Type 'help' for available commands"))
    
    def run(self):
        """Main shell loop"""
        self.print_banner()
        
        while True:
            try:
                # Show menu if no mode selected
                if not self.mode:
                    self.print_main_menu()
                    choice = input(self.get_prompt()).strip()
                    
                    if choice == '1':
                        self.mode = 'MIAU'
                        print(Color.success("\n✓ Entered MIAU mode ฅ(•ㅅ•❀)ฅ"))
                        self.print_help('MIAU')
                    elif choice == '2':
                        self.mode = 'MRRP'
                        print(Color.success("\n✓ Entered MRRP mode ฅ(•ㅅ•❀)ฅ"))
                        self.print_help('MRRP')
                    elif choice == '3':
                        self.mode = 'NYAA'
                        print(Color.success("\n✓ Entered NYAA mode ฅ(•ㅅ•❀)ฅ"))
                        self.print_help('NYAA')
                    elif choice == '4':
                        self.mode = 'MROW'
                        print(Color.success("\n✓ Entered MROW mode ฅ(•ㅅ•❀)ฅ"))
                        self.print_help('MROW')
                    elif choice == '5' or choice.lower() in ['exit', 'quit', 'q']:
                        print(Color.success("\n✓ Goodbye! ฅ(•ㅅ•❀)ฅ\n"))
                        break
                    else:
                        print(Color.error("✗ Invalid choice"))
                    continue
                
                # Handle mode-specific commands
                user_input = input(self.get_prompt()).strip()
                if not user_input:
                    continue
                
                parts = user_input.split()
                cmd = parts[0].lower()
                args = parts[1:]
                
                if cmd in ['exit', 'quit', 'q']:
                    print(Color.success("\n✓ Goodbye! ฅ(•ㅅ•❀)ฅ\n"))
                    break
                
                if self.mode == 'MIAU':
                    self.handle_miau(cmd, args)
                elif self.mode == 'MRRP':
                    self.handle_mrrp(cmd, args)
                elif self.mode == 'NYAA':
                    self.handle_nyaa(cmd, args)
                elif self.mode == 'MROW':
                    self.handle_mrow(cmd, args)
                
            except KeyboardInterrupt:
                print(Color.warning("\n\n⚠ Use 'exit' to quit properly ฅ(•ㅅ•❀)ฅ"))
                continue
            except EOFError:
                print(Color.success("\n✓ Goodbye! ฅ(•ㅅ•❀)ฅ\n"))
                break
            except Exception as e:
                print(Color.error(f"\n✗ Error: {e}"))
                import traceback
                traceback.print_exc()
                continue

# ============================================================================
# MAIN
# ============================================================================
def main():
    """Entry point"""
    shell = MEOWShell()
    shell.run()

if __name__ == '__main__':
    main()
