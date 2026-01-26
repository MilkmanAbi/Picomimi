# MEOW - MicroOS Engineering Orchestration Workbench

**The unified Picomimi development toolchain**

Made with determination ฅ(•ㅅ•❀)ฅ

## What is MEOW?

MEOW combines four powerful tools into one cohesive development environment for working with the Picomimi MicroOS kernel:

- **MIAU** - Monolithic INO Aggregator Utility
- **MRRP** - Monolithic Repartition & Refactor Program  
- **NYAA** - Normalize Your Architecture Automatically
- **MROW** - Mend & Review Our Weirdness

## Installation

```bash
# Make executable
chmod +x MEOW.py

# Run
./MEOW.py
```

Or:
```bash
python3 MEOW.py
```

## Quick Start

1. **Launch MEOW**:
   ```bash
   ./MEOW.py
   ```

2. **Choose a mode** (1-4) from the main menu

3. **Use mode-specific commands** - type `help` in any mode for details

4. **Return to main menu** with `back` command

5. **Exit** with `exit` or Ctrl+D

## The Tools

### MIAU - Module Assembler

**Purpose**: Assembles separate module files back into a single monolithic .ino file

**Commands**:
```
load <config.json>              Load assembly configuration
assemble <dir> <output.ino>     Assemble modules
compare <assembled> <original>  Verify assembly matches original
```

**Use case**: After splitting your kernel with MRRP and editing individual modules, use MIAU to reassemble them into a working .ino file for Arduino IDE compilation.

### MRRP - Module Splitter

**Purpose**: Splits a monolithic .ino file into manageable module files

**Commands**:
```
load <config.json>           Load split configuration
split <source.ino> <outdir>  Split file into modules
verify <outdir>              Verify all modules extracted
```

**Configuration**: MRRP uses a JSON config file that defines:
- Module names and output filenames
- Start/end markers for extraction
- Optional line ranges

**Use case**: Take your 7000+ line kernel and split it into focused, manageable modules like "Memory Management", "Scheduler", "PMFS", etc. This makes development FAR easier.

### NYAA - Surgical Editor

**Purpose**: Applies precise, documented code edits via JSON manifests

**Commands**:
```
apply <source> <manifest> <output>  Apply edits from manifest
```

**Manifest capabilities**:
- Remove specific line ranges
- Insert code at precise locations
- Replace text throughout file
- Each edit includes a reason/description

**Use case**: When you need to make systematic changes (remove ACE system, add new functions, fix duplicates) - create a manifest and let NYAA handle it surgically.

### MROW - Code Verifier

**Purpose**: Verifies structural integrity of your code

**Commands**:
```
verify <source.ino>  Check code structure
```

**Checks**:
- ✓ Brace balance
- ✓ Duplicate function definitions
- ✓ Smart quotes (breaks compilation)
- ✓ Orphaned code outside functions

**Use case**: Run BEFORE split/assembly and AFTER to ensure nothing broke. Essential for catching issues before compilation.

## Typical Workflow

### Splitting a Monolithic Kernel

```
MEOW~> 2                                    # Enter MRRP mode
MRRP~> load mrrp_config_picomimi.json      # Load config
MRRP~> split Picomimi_v13.ino modules/     # Split into modules
MRRP~> verify modules/                      # Verify extraction
MRRP~> back                                 # Return to main menu
```

### Editing and Reassembling

```
# Edit individual module files in modules/ directory
# Then reassemble:

MEOW~> 1                                    # Enter MIAU mode
MIAU~> load mrrp_config_picomimi.json      # Same config!
MIAU~> assemble modules/ Picomimi_v13_New.ino
MIAU~> compare Picomimi_v13_New.ino Picomimi_v13.ino
```

### Applying Surgical Fixes

```
MEOW~> 3                                    # Enter NYAA mode
NYAA~> apply Picomimi_v13.ino fix_duplicates.json Picomimi_v13_Fixed.ino
```

### Verifying Code

```
MEOW~> 4                                    # Enter MROW mode
MROW~> verify Picomimi_v13.ino
```

## Configuration Files

### MRRP/MIAU Config Format

Both MRRP and MIAU use the **same config file** for consistency:

```json
{
  "version": "1.0",
  "description": "Picomimi v13 module configuration",
  "header": [
    "// File header comments\n",
    "// Toolchain info\n"
  ],
  "modules": [
    {
      "name": "Module Name",
      "filename": "01_ModuleName.txt",
      "start_marker": "// START MARKER",
      "end_marker": "// END MARKER"
    }
  ]
}
```

**Alternative**: Use line numbers instead of markers:
```json
{
  "name": "Memory Management",
  "filename": "06_Memory.txt",
  "start_line": 500,
  "end_line": 1200
}
```

### NYAA Manifest Format

```json
{
  "version": "1.0",
  "description": "What this manifest does",
  "remove_lines": [
    {
      "start": 100,
      "end": 150,
      "reason": "Removing ACE system"
    }
  ],
  "insert_code": [
    {
      "after_line": 200,
      "code": ["void new_function() {\n", "  // code\n", "}\n"],
      "reason": "Adding missing function"
    }
  ],
  "replace_text": [
    {
      "old": "old_text",
      "new": "new_text",
      "reason": "Updating API call"
    }
  ]
}
```

## Color Coding

MEOW uses a consistent color scheme:

- **🟢 Green**: Success messages
- **🔴 Red**: Errors
- **🟠 Orange**: Warnings  
- **🟣 Purple**: Uncertain/ambiguous states
- **⚪ White**: General information

## Why MEOW?

### The Problem

Working with a 7000+ line Arduino sketch is:
- Hard to navigate
- Difficult to debug
- Risky to modify
- Painful to collaborate on

### The Solution

MEOW lets you:
- **Split** your monolith into focused modules
- **Edit** individual modules safely
- **Assemble** back into working code
- **Verify** integrity at every step
- **Apply** systematic fixes with precision

### The Philosophy

- **Config-driven**: MRRP uses external configs, making it future-proof
- **Reversible**: Split → Edit → Reassemble → Compare with original
- **Safe**: MROW catches issues before they become compilation errors
- **Documented**: Every NYAA edit includes a reason
- **Clean**: Terminal output is clear, consistent, professional

## Advanced Usage

### Custom Module Layouts

Create your own config for different module structures:

```bash
# Split by feature
MRRP~> load configs/feature_split.json

# Split by subsystem  
MRRP~> load configs/subsystem_split.json

# Split for testing
MRRP~> load configs/test_modules.json
```

### Chaining Operations

```bash
# Split → Verify → Edit → Assemble → Verify → Compare
MRRP~> split kernel.ino modules/
MROW~> verify kernel.ino

# ... edit modules ...

MIAU~> assemble modules/ kernel_new.ino
MROW~> verify kernel_new.ino
MIAU~> compare kernel_new.ino kernel.ino
```

### Batch Processing

NYAA manifests can be stacked:

```bash
NYAA~> apply source.ino fix1.json temp1.ino
NYAA~> apply temp1.ino fix2.json temp2.ino  
NYAA~> apply temp2.ino fix3.json final.ino
```

## Troubleshooting

### MRRP Can't Find Markers

- Check your markers match exactly (including whitespace)
- Use line numbers instead: `"start_line"` and `"end_line"`
- Verify source file encoding is UTF-8

### MIAU Assembly Doesn't Match Original

- Run `MROW verify` on both files
- Check for missing modules in output directory
- Ensure config lists modules in correct order

### NYAA Edits Not Applying

- Manifest applies edits in order: text replacement → removals → insertions
- Line numbers shift after removals!
- Use `MROW verify` after applying

## Tips

1. **Always verify before and after** - MROW is your friend
2. **Keep configs in version control** - they document your structure
3. **Test on a copy first** - MEOW doesn't modify originals unless you tell it to
4. **Use descriptive filenames** - `01_Memory.txt` not `module1.txt`
5. **Document your manifests** - future you will thank current you

## Examples Included

- `mrrp_config_picomimi.json` - Config for Picomimi v13 kernel
- Example NYAA manifests coming soon!

## Requirements

- Python 3.6+
- No external dependencies (uses only stdlib)
- Works on Linux, macOS, Windows

## License

Part of the Picomimi MicroOS project

## Contributing

Found a bug? Have an idea? Let me know!

---

**Made with determination ฅ(•ㅅ•❀)ฅ**

*Remember: A well-organized codebase is a happy codebase!*
