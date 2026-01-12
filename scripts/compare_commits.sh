#!/bin/bash
# compare_commits.sh - Compare benchmark performance between two commits
# Uses git worktrees to avoid disrupting your working directory
#
# Usage: ./scripts/compare_commits.sh [baseline_ref] [compare_ref]
#   baseline_ref: Git ref for baseline (default: HEAD~1)
#   compare_ref:  Git ref to compare (default: HEAD)
#
# Example:
#   ./scripts/compare_commits.sh HEAD~1 HEAD
#   ./scripts/compare_commits.sh abc123 def456

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
NC='\033[0m' # No Color
BOLD='\033[1m'

# Configuration
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
WORKTREE_DIR="$PROJECT_ROOT/.bench-worktrees"
RESULTS_DIR="$PROJECT_ROOT/.bench-results"
BENCHMARK_TARGET="plexusBenchmarks"

# Arguments with defaults
BASELINE_REF="${1:-HEAD~1}"
COMPARE_REF="${2:-HEAD}"

# Resolve refs to commit hashes for consistency
resolve_ref() {
    git -C "$PROJECT_ROOT" rev-parse --short "$1" 2>/dev/null || {
        echo -e "${RED}Error: Cannot resolve ref '$1'${NC}" >&2
        exit 1
    }
}

BASELINE_HASH=$(resolve_ref "$BASELINE_REF")
COMPARE_HASH=$(resolve_ref "$COMPARE_REF")

echo -e "${BOLD}${CYAN}╔══════════════════════════════════════════════════════════════╗${NC}"
echo -e "${BOLD}${CYAN}║          Plexus Benchmark Comparison Tool                    ║${NC}"
echo -e "${BOLD}${CYAN}╚══════════════════════════════════════════════════════════════╝${NC}"
echo ""
echo -e "${BLUE}Baseline:${NC} $BASELINE_REF ${YELLOW}($BASELINE_HASH)${NC}"
echo -e "${BLUE}Compare:${NC}  $COMPARE_REF ${YELLOW}($COMPARE_HASH)${NC}"
echo ""

# Create directories
mkdir -p "$WORKTREE_DIR" "$RESULTS_DIR"

# Cleanup function
cleanup() {
    echo -e "\n${YELLOW}Cleaning up worktrees...${NC}"
    git -C "$PROJECT_ROOT" worktree remove --force "$WORKTREE_DIR/baseline" 2>/dev/null || true
    git -C "$PROJECT_ROOT" worktree remove --force "$WORKTREE_DIR/compare" 2>/dev/null || true
    rmdir "$WORKTREE_DIR" 2>/dev/null || true
}

# Set trap for cleanup on exit
trap cleanup EXIT

# Build and run benchmarks for a given ref
# Returns the result file path via the RESULT_FILE global variable
run_benchmarks() {
    local ref="$1"
    local hash="$2"
    local name="$3"
    local worktree_path="$WORKTREE_DIR/$name"
    RESULT_FILE="$RESULTS_DIR/${name}_${hash}.json"
    
    echo -e "${BOLD}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
    echo -e "${BLUE}[$name]${NC} Setting up worktree for ${YELLOW}$ref ($hash)${NC}"
    
    # Remove existing worktree if present
    git -C "$PROJECT_ROOT" worktree remove --force "$worktree_path" 2>/dev/null || true
    
    # Create worktree
    git -C "$PROJECT_ROOT" worktree add "$worktree_path" "$hash" --quiet
    
    echo -e "${BLUE}[$name]${NC} Building..."
    
    # Build in the worktree (use Ninja for faster builds)
    cmake -S "$worktree_path" -B "$worktree_path/build" \
        -G Ninja \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_CXX_FLAGS="-O3 -DNDEBUG" \
        > /dev/null 2>&1
    
    cmake --build "$worktree_path/build" --target "$BENCHMARK_TARGET" > /dev/null 2>&1
    
    echo -e "${BLUE}[$name]${NC} Running benchmarks..."
    
    # Run benchmarks and save JSON output
    "$worktree_path/build/$BENCHMARK_TARGET" \
        --benchmark_format=json \
        --benchmark_out="$RESULT_FILE" \
        --benchmark_repetitions=3 \
        --benchmark_report_aggregates_only=true \
        > /dev/null 2>&1
    
    echo -e "${GREEN}[$name]${NC} Results saved to ${CYAN}$RESULT_FILE${NC}"
}

# Check if compare.py is available (Google Benchmark tool)
find_compare_tool() {
    # Try common locations
    local paths=(
        "$PROJECT_ROOT/build/_deps/googlebenchmark-src/tools/compare.py"
        "/usr/local/share/benchmark/tools/compare.py"
        "$HOME/.local/share/benchmark/tools/compare.py"
    )
    
    for p in "${paths[@]}"; do
        if [[ -f "$p" ]]; then
            echo "$p"
            return 0
        fi
    done
    return 1
}

# Simple comparison when compare.py is not available
simple_compare() {
    local baseline_file="$1"
    local compare_file="$2"
    
    echo -e "\n${BOLD}${CYAN}╔══════════════════════════════════════════════════════════════╗${NC}"
    echo -e "${BOLD}${CYAN}║                    Benchmark Comparison                      ║${NC}"
    echo -e "${BOLD}${CYAN}╚══════════════════════════════════════════════════════════════╝${NC}\n"
    
    # Use Python for JSON parsing - pass files as arguments to avoid heredoc issues
    python3 - "$baseline_file" "$compare_file" <<'PYTHON_SCRIPT'
import json
import sys

def load_benchmarks(path):
    with open(path) as f:
        data = json.load(f)
    return {b['name']: b for b in data.get('benchmarks', [])}

baseline_file = sys.argv[1]
compare_file = sys.argv[2]

baseline = load_benchmarks(baseline_file)
compare = load_benchmarks(compare_file)

# Header
print(f"{'Benchmark':<50} {'Baseline':>14} {'Compare':>14} {'Change':>10}")
print("─" * 90)

all_names = sorted(set(baseline.keys()) | set(compare.keys()))

for name in all_names:
    if '_mean' not in name:
        continue
    
    b = baseline.get(name, {})
    c = compare.get(name, {})
    
    b_time = b.get('real_time', 0)
    c_time = c.get('real_time', 0)
    
    if b_time > 0 and c_time > 0:
        change = ((c_time - b_time) / b_time) * 100
        
        # Color coding
        if change < -5:
            color = '\033[0;32m'  # Green (improvement)
            symbol = '▼'
        elif change > 5:
            color = '\033[0;31m'  # Red (regression)
            symbol = '▲'
        else:
            color = '\033[0;33m'  # Yellow (neutral)
            symbol = '●'
        
        reset = '\033[0m'
        
        # Format times - auto-scale based on magnitude for readability
        def fmt_time(t, unit):
            # Convert everything to nanoseconds first
            if unit == 'us':
                t = t * 1000
            elif unit == 'ms':
                t = t * 1e6
            
            # Now auto-scale for display
            if t >= 1e9:
                return f"{t/1e9:.2f} s"
            elif t >= 1e6:
                return f"{t/1e6:.2f} ms"
            elif t >= 1e3:
                return f"{t/1e3:.2f} µs"
            else:
                return f"{t:.1f} ns"
        
        unit = b.get('time_unit', 'ns')
        display_name = name.replace('_mean', '')
        
        print(f"{display_name:<50} {fmt_time(b_time, unit):>14} {fmt_time(c_time, unit):>14} {color}{symbol} {change:+.1f}%{reset}")

print("─" * 90)
print("\n▼ = improvement (faster), ▲ = regression (slower), ● = within noise margin (±5%)")
PYTHON_SCRIPT
}

# Main execution
echo -e "\n${BOLD}Phase 1: Running baseline benchmarks${NC}"
run_benchmarks "$BASELINE_REF" "$BASELINE_HASH" "baseline"
BASELINE_RESULT="$RESULT_FILE"

echo -e "\n${BOLD}Phase 2: Running comparison benchmarks${NC}"
run_benchmarks "$COMPARE_REF" "$COMPARE_HASH" "compare"
COMPARE_RESULT="$RESULT_FILE"

# Compare results
echo -e "\n${BOLD}Phase 3: Comparing results${NC}"

# Use our built-in comparison (Google's compare.py requires scipy which may not be installed)
simple_compare "$BASELINE_RESULT" "$COMPARE_RESULT"

echo -e "\n${GREEN}${BOLD}✓ Comparison complete!${NC}"
echo -e "Results saved in: ${CYAN}$RESULTS_DIR${NC}"
