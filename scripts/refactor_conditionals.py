#!/usr/bin/env python3
"""
Complete refactoring script to remove ALL ENABLE_* conditional compilation.
Handles: #ifdef, #ifndef, #if defined, nested conditions, multiline statements.
"""

import re
import sys
from pathlib import Path
from typing import Tuple

# Fusion enabled features
ENABLED = {
    'ENABLE_UART', 'ENABLE_USB', 'ENABLE_VOX', 'ENABLE_TX1750', 'ENABLE_FLASHLIGHT',
    'ENABLE_SPECTRUM', 'ENABLE_BIG_FREQ', 'ENABLE_SMALL_BOLD', 'ENABLE_CUSTOM_MENU_LAYOUT',
    'ENABLE_KEEP_MEM_NAME', 'ENABLE_WIDE_RX', 'ENABLE_NO_CODE_SCAN_TIMEOUT',
    'ENABLE_SQUELCH_MORE_SENSITIVE', 'ENABLE_FASTER_CHANNEL_SCAN', 'ENABLE_RSSI_BAR',
    'ENABLE_AUDIO_BAR', 'ENABLE_COPY_CHAN_TO_VFO', 'ENABLE_SCAN_RANGES',
    'ENABLE_FEAT_F4HWN', 'ENABLE_FEAT_F4HWN_SPECTRUM', 'ENABLE_FEAT_F4HWN_RX_TX_TIMER',
    'ENABLE_FEAT_F4HWN_SLEEP', 'ENABLE_FEAT_F4HWN_RESUME_STATE', 'ENABLE_FEAT_F4HWN_NARROWER',
    'ENABLE_FEAT_F4HWN_INV', 'ENABLE_FEAT_F4HWN_CTR', 'ENABLE_FEAT_F4HWN_CA',
    'ENABLE_NAVIG_LEFT_RIGHT', 'ENABLE_FMRADIO', 'ENABLE_AIRCOPY',
    'ENABLE_FEAT_F4HWN_SCREENSHOT', 'ENABLE_FEAT_F4HWN_GAME', 'ENABLE_FEAT_F4HWN_PMR',
    'ENABLE_FEAT_F4HWN_GMRS_FRS_MURS', 'ENABLE_FEAT_F4HWN_RESCUE_OPS',
    'ENABLE_SWD', 'ENABLE_ALARM', 'ENABLED_AIRCOPY',
}

# Disabled features
DISABLED = {
    'ENABLE_NOAA', 'ENABLE_VOICE', 'ENABLE_PWRON_PASSWORD', 'ENABLE_DTMF_CALLING',
    'ENABLE_TX_WHEN_AM', 'ENABLE_F_CAL_MENU', 'ENABLE_CTCSS_TAIL_PHASE_SHIFT',
    'ENABLE_BOOT_BEEPS', 'ENABLE_SHOW_CHARGE_LEVEL', 'ENABLE_REVERSE_BAT_SYMBOL',
    'ENABLE_AM_FIX', 'ENABLE_REDUCE_LOW_MID_TX_POWER', 'ENABLE_BYP_RAW_DEMODULATORS',
    'ENABLE_BLMIN_TMP_OFF', 'ENABLE_REGA', 'ENABLE_EXTRA_UART_CMD',
    'ENABLE_FEAT_F4HWN_CHARGING_C', 'ENABLE_FEAT_F4HWN_VOL', 'ENABLE_FEAT_F4HWN_RESET_CHANNEL',
    'ENABLE_FEAT_F4HWN_DEBUG', 'ENABLE_AM_FIX_SHOW_DATA', 'ENABLE_AGC_SHOW_DATA',
    'ENABLE_UART_RW_BK_REGS', 'ENABLE_OVERLAY',
}


def is_header_guard(line: str) -> bool:
    """Check if this is a header guard (NOT an ENABLE_ directive)."""
    return bool(re.match(r'^\s*#\s*ifndef\s+[A-Z_]+_H\s*$', line))


def process_file(filepath: Path, dry_run: bool = False) -> Tuple[int, int]:
    """Process file, return (lines_removed, passes_needed)."""
    try:
        with open(filepath, 'r', encoding='utf-8', errors='ignore') as f:
            content = f.read()
    except Exception as e:
        print(f"Error reading {filepath}: {e}")
        return 0, 0
    
    original_content = content
    passes = 0
    max_passes = 10  # Safety limit
    
    while passes < max_passes:
        passes += 1
        lines = content.split('\n')
        result = []
        i = 0
        changes_made = False
        
        while i < len(lines):
            line = lines[i]
            
            # Skip header guards
            if is_header_guard(line):
                result.append(line)
                i += 1
                continue
            
            # Check for ENABLE_ conditional directives
            ifdef_match = re.match(r'^\s*#\s*ifdef\s+(ENABLE[A-Z_0-9]*)', line)
            ifndef_match = re.match(r'^\s*#\s*ifndef\s+(ENABLE[A-Z_0-9]*)', line)
            if_defined_match = re.match(r'^\s*#\s*if\s+defined', line)
            
            feature = None
            is_enabled = None
            
            if ifdef_match:
                feature = ifdef_match.group(1)
                is_enabled = feature in ENABLED
            elif ifndef_match:
                feature = ifndef_match.group(1)
                is_enabled = feature in DISABLED  # ifndef inverts logic
            elif if_defined_match and 'ENABLE' in line:
                # Extract all ENABLE features
                features = re.findall(r'ENABLE[A-Z_0-9]*', line)
                if not features:
                    result.append(line)
                    i += 1
                    continue
                
                # Evaluate compound conditions
                if all(f in ENABLED or f in DISABLED for f in features):
                    if '&&' in line:
                        is_enabled = all(f in ENABLED for f in features)
                    elif '||' in line:
                        is_enabled = any(f in ENABLED for f in features)
                    elif len(features) == 1:
                        is_enabled = features[0] in ENABLED
                    else:
                        result.append(line)
                        i += 1
                        continue
                else:
                    result.append(line)
                    i += 1
                    continue
            else:
                result.append(line)
                i += 1
                continue
            
            # Found a conditional we can evaluate!
            changes_made = True
            i += 1  # Skip the directive line
            depth = 1
            block = []
            else_block = []
            in_else = False
            
            # Collect the block
            while i < len(lines) and depth > 0:
                curr = lines[i]
                
                if re.match(r'^\s*#\s*(ifdef|ifndef|if)\s', curr):
                    depth += 1
                    if not in_else:
                        block.append(curr)
                    else:
                        else_block.append(curr)
                elif re.match(r'^\s*#\s*endif', curr):
                    depth -= 1
                    if depth > 0:
                        if not in_else:
                            block.append(curr)
                        else:
                            else_block.append(curr)
                    # else: discard the matching #endif
                elif re.match(r'^\s*#\s*else\s*$', curr) and depth == 1:
                    in_else = True
                else:
                    if not in_else:
                        block.append(curr)
                    else:
                        else_block.append(curr)
                
                i += 1
            
            # Add appropriate block based on evaluation
            if is_enabled:
                result.extend(block)  # Unwrap: keep block content
            else:
                result.extend(else_block)  # Keep else block if present
        
        content = '\n'.join(result)
        
        if not changes_made:
            break  # No more changes, we're done
    
    # Write back if changed
    lines_removed = original_content.count('\n') - content.count('\n')
    
    if not dry_run and original_content != content:
        try:
            with open(filepath, 'w', encoding='utf-8') as f:
                f.write(content)
        except Exception as e:
            print(f"Error writing {filepath}: {e}")
    
    return lines_removed, passes


def main():
    import argparse
    
    parser = argparse.ArgumentParser(description='Remove ALL ENABLE_* conditionals from C/C++ files')
    parser.add_argument('--dry-run', action='store_true', help='Show changes without modifying files')
    parser.add_argument('--app-dir', default='App', help='Directory to process (default: App)')
    args = parser.parse_args()
    
    app_dir = Path(args.app_dir)
    if not app_dir.exists():
        print(f"Error: Directory {app_dir} does not exist")
        sys.exit(1)
    
    files = list(app_dir.rglob('*.c')) + list(app_dir.rglob('*.h'))
    
    print(f"Processing {len(files)} files...")
    if args.dry_run:
        print("DRY RUN MODE - no files will be modified\n")
    
    total_lines = 0
    total_passes = 0
    modified_files = 0
    
    for filepath in sorted(files):
        lines_removed, passes = process_file(filepath, args.dry_run)
        
        if lines_removed > 0:
            modified_files += 1
            total_lines += lines_removed
            total_passes += passes
            status = "[DRY RUN]" if args.dry_run else "[MODIFIED]"
            print(f"{status} {filepath}: {lines_removed} lines removed ({passes} passes)")
    
    print(f"\n{'='*60}")
    print(f"Files processed: {len(files)}")
    print(f"Files modified: {modified_files}")
    print(f"Total lines removed: {total_lines}")
    print(f"Average passes per file: {total_passes/max(modified_files, 1):.1f}")
    
    if not args.dry_run and modified_files > 0:
        print(f"\n✅ Refactoring complete!")
        print(f"\nTo verify, run:")
        print(f"  grep -rn '#ifdef ENABLE\\|#ifndef ENABLE\\|#if defined.*ENABLE' {app_dir} "
              f"--include='*.c' --include='*.h' | wc -l")


if __name__ == '__main__':
    main()
