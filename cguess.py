#!/usr/bin/env python3
# -*- coding: utf-8 -*-

"""
Identifies required libraries, include paths, local source files, and C++
standard for a project and generates a full compiler command line or a
Makefile/CMakeLists.txt file. It supports g++ and clang++, Debian, Red Hat,
and Arch-based distributions, recursively resolves local #includes, and
caches system lookups for performance.
"""

import argparse
import os
import re
import subprocess
import sys
import sqlite3
import json
from typing import Set, List, Dict, Optional, Tuple

# Regex to find #include directives. Captures the header file name and the quote/bracket type.
INCLUDE_REGEX = re.compile(r'^\s*#\s*include\s*([<"])([^>"]+)[>"]')

# Regex to find shared library files (.so) and extract the library name.
LIB_REGEX = re.compile(r'/lib([^/]+)\.so')

# A set of common development packages that are part of the standard toolchain
IGNORE_PACKAGES = {
    'libc6-dev', 'glibc-devel', 'gcc-libs', 'libgcc',
    'libstdc++-11-dev', 'libstdc++-devel',
    'linux-libc-dev'
}

# A comprehensive set of C++ standard library header names.
CPP_STANDARD_HEADERS = {
    'algorithm', 'any', 'array', 'atomic', 'bit', 'bitset', 'charconv',
    'chrono', 'codecvt', 'compare', 'complex', 'concepts', 'condition_variable',
    'coroutine', 'deque', 'exception', 'execution', 'filesystem', 'format',
    'forward_list', 'fstream', 'functional', 'future', 'initializer_list',
    'iomanip', 'ios', 'iosfwd', 'iostream', 'istream', 'iterator', 'limits',
    'list', 'locale', 'map', 'memory', 'memory_resource', 'mutex', 'new',
    'numbers', 'numeric', 'optional', 'ostream', 'queue', 'random', 'ranges',
    'ratio', 'regex', 'scoped_allocator', 'set', 'shared_mutex',
    'source_location', 'span', 'sstream', 'stack', 'stdexcept', 'streambuf',
    'string', 'string_view', 'syncstream', 'system_error', 'thread', 'tuple',
    'type_traits', 'typeindex', 'typeinfo', 'unordered_map', 'unordered_set',
    'utility', 'valarray', 'variant', 'vector', 'version',
    'cassert', 'cctype', 'cerrno', 'cfenv', 'cfloat', 'cinttypes', 'ciso646',
    'climits', 'clocale', 'cmath', 'csetjmp', 'csignal', 'cstdalign',
    'cstdarg', 'cstdbool', 'cstddef', 'cstdint', 'cstdio', 'cstdlib',
    'cstring', 'ctgmath', 'ctime', 'cuchar', 'cwchar', 'cwctype',
    'assert.h', 'ctype.h', 'errno.h', 'fenv.h', 'float.h', 'inttypes.h',
    'iso646.h', 'limits.h', 'locale.h', 'math.h', 'setjmp.h', 'signal.h',
    'stdalign.h', 'stdarg.h', 'stdbool.h', 'stddef.h', 'stdint.h',
    'stdio.h', 'stdlib.h', 'string.h', 'tgmath.h', 'time.h', 'uchar.h',
    'wchar.h', 'wctype.h'
}

class Cache:
    """A simple SQLite cache for system lookup results."""
    def __init__(self, distro_id, enabled=True):
        self.enabled = enabled
        self.distro_id = distro_id
        if not self.enabled:
            return
            
        cache_dir = os.path.join(os.path.expanduser("~"), ".cache", "cguess")
        os.makedirs(cache_dir, exist_ok=True)
        self.db_path = os.path.join(cache_dir, "cache.db")
        
        # Check for outdated schema and automatically recreate the cache.
        if os.path.exists(self.db_path):
            try:
                conn_check = sqlite3.connect(self.db_path)
                cursor_check = conn_check.cursor()
                # This query will fail if the distro_id column doesn't exist in the packages table,
                # indicating an old schema that needs to be deleted.
                cursor_check.execute("SELECT distro_id FROM packages LIMIT 1")
                conn_check.close()
            except sqlite3.OperationalError:
                print("Cache schema is outdated. Clearing and recreating.", file=sys.stderr)
                conn_check.close()
                os.remove(self.db_path)

        self.conn = sqlite3.connect(self.db_path)
        self._create_tables()

    def _create_tables(self):
        cursor = self.conn.cursor()
        cursor.execute('''
            CREATE TABLE IF NOT EXISTS system_headers (
                distro_id TEXT,
                header_name TEXT,
                candidates_json TEXT,
                PRIMARY KEY (distro_id, header_name)
            )
        ''')
        cursor.execute('''
            CREATE TABLE IF NOT EXISTS packages (
                distro_id TEXT,
                package_name TEXT,
                include_paths_json TEXT,
                libs_json TEXT,
                PRIMARY KEY (distro_id, package_name)
            )
        ''')
        self.conn.commit()

    def get(self, table, key):
        if not self.enabled: return None
        cursor = self.conn.cursor()
        if table == 'system_headers':
             cursor.execute(f"SELECT candidates_json FROM system_headers WHERE distro_id=? AND header_name=?", (self.distro_id, key))
             row = cursor.fetchone()
             return json.loads(row[0]) if row else None
        elif table == 'packages':
            cursor.execute(f"SELECT include_paths_json, libs_json FROM packages WHERE distro_id=? AND package_name=?", (self.distro_id, key))
            row = cursor.fetchone()
            if row:
                return (json.loads(row[0]), json.loads(row[1]))
            return None

    def set(self, table, key, value):
        if not self.enabled: return
        cursor = self.conn.cursor()
        if table == 'system_headers':
            cursor.execute("INSERT OR REPLACE INTO system_headers (distro_id, header_name, candidates_json) VALUES (?, ?, ?)", (self.distro_id, key, json.dumps(value)))
        elif table == 'packages':
            include_paths, libs = value
            cursor.execute("INSERT OR REPLACE INTO packages (distro_id, package_name, include_paths_json, libs_json) VALUES (?, ?, ?, ?)", 
                           (self.distro_id, key, json.dumps(list(include_paths)), json.dumps(list(libs))))
        self.conn.commit()

    def close(self):
        if self.enabled:
            self.conn.close()

    @staticmethod
    def clear():
        cache_file = os.path.join(os.path.expanduser("~"), ".cache", "cguess", "cache.db")
        if os.path.exists(cache_file):
            os.remove(cache_file)
            print("Cache cleared.", file=sys.stderr)
        else:
            print("No cache file to clear.", file=sys.stderr)

def run_command(command: List[str]) -> str:
    """Executes a shell command and returns its stdout."""
    try:
        result = subprocess.run(command, capture_output=True, text=True, check=True, encoding='utf-8')
        return result.stdout.strip()
    except (subprocess.CalledProcessError, FileNotFoundError):
        return ""

class PackageManager:
    """Abstract base class for distribution-specific package managers."""
    def find_header_candidates(self, header: str) -> List[Dict[str, str]]:
        raise NotImplementedError
    
    def find_package_info(self, package_name: str) -> Tuple[Set[str], Set[str]]:
        raise NotImplementedError

class DebianManager(PackageManager):
    """Package manager for Debian-based systems (dpkg)."""
    def find_header_candidates(self, header: str) -> List[Dict[str, str]]:
        candidates = []
        search_pattern = f'*/{header}'
        output = run_command(['dpkg', '-S', search_pattern])
        if not output: return candidates
        for match_line in output.splitlines():
            try:
                last_colon_index = match_line.rfind(':')
                if last_colon_index == -1: continue
                full_path = match_line[last_colon_index+1:].strip()
                package_name = match_line[:last_colon_index].split(':')[0]
                if not any(c['full_path'] == full_path for c in candidates):
                    candidates.append({'package': package_name, 'full_path': full_path})
            except Exception: continue
        return candidates

    def find_package_info(self, package_name: str) -> Tuple[Set[str], Set[str]]:
        include_paths, libs = set(), set()
        file_list = run_command(['dpkg', '-L', package_name])
        for file_path in file_list.splitlines():
            if '/usr/include/' in file_path and file_path.endswith(('.h', '.hpp', '.hh')):
                include_paths.add(os.path.dirname(file_path))
            match = LIB_REGEX.search(file_path)
            if match: libs.add(match.group(1))
        return include_paths, libs

class RedHatManager(PackageManager):
    """Package manager for Red Hat-based systems (rpm)."""
    def find_header_candidates(self, header: str) -> List[Dict[str, str]]:
        candidates = []
        # rpm doesn't support globbing in the same way, so we find the package first
        # then list its files. This is less direct but works.
        output = run_command(['rpm', '-qf', f'/usr/include/{header}'])
        if not output or 'not owned' in output:
             # Fallback for headers in subdirectories
             output = run_command(['repoquery', '--whatprovides', f'*/{header}'])
             if not output: return []
             # This is a bit of a guess, we take the first one
             package_name = output.splitlines()[0].rsplit('-', 2)[0]
        else:
            package_name = output.strip()

        # Now find the exact path
        file_list = run_command(['rpm', '-ql', package_name])
        for file_path in file_list.splitlines():
            if file_path.endswith(f'/{header}'):
                 candidates.append({'package': package_name, 'full_path': file_path})
        return candidates

    def find_package_info(self, package_name: str) -> Tuple[Set[str], Set[str]]:
        include_paths, libs = set(), set()
        file_list = run_command(['rpm', '-ql', package_name])
        for file_path in file_list.splitlines():
            if '/usr/include/' in file_path and file_path.endswith(('.h', '.hpp', '.hh')):
                include_paths.add(os.path.dirname(file_path))
            match = LIB_REGEX.search(file_path)
            if match: libs.add(match.group(1))
        return include_paths, libs

class ArchManager(PackageManager):
    """Package manager for Arch-based systems (pacman)."""
    def find_header_candidates(self, header: str) -> List[Dict[str, str]]:
        candidates = []
        # Use pkgfile to find which package owns a file not currently on the filesystem
        output = run_command(['pkgfile', '-s', header])
        if not output: return candidates
        for line in output.splitlines():
            try:
                package_name, file_path = line.split('\t', 1)
                if file_path.endswith(f'/{header}'):
                    if not any(c['full_path'] == file_path for c in candidates):
                        candidates.append({'package': package_name, 'full_path': file_path})
            except Exception: continue
        return candidates

    def find_package_info(self, package_name: str) -> Tuple[Set[str], Set[str]]:
        include_paths, libs = set(), set()
        file_list = run_command(['pacman', '-Ql', package_name])
        for line in file_list.splitlines():
            try:
                _, file_path = line.split(' ', 1)
                if '/usr/include/' in file_path and file_path.endswith(('.h', '.hpp', '.hh')):
                    include_paths.add(os.path.dirname(file_path))
                match = LIB_REGEX.search(file_path)
                if match: libs.add(match.group(1))
            except Exception: continue
        return include_paths, libs

def get_distro() -> Tuple[Optional[str], Optional[PackageManager]]:
    """Detects the Linux distribution and returns its ID and a PackageManager instance."""
    if not os.path.exists('/etc/os-release'):
        return None, None
    
    with open('/etc/os-release') as f:
        lines = f.readlines()
    
    info = {k.strip(): v.strip().strip('"') for k, v in (line.split('=', 1) for line in lines if '=' in line)}
    distro_id = info.get('ID')
    id_like = info.get('ID_LIKE', '').split()

    if distro_id in ['debian', 'ubuntu', 'linuxmint'] or 'debian' in id_like:
        return 'debian', DebianManager()
    if distro_id in ['fedora', 'centos', 'rhel'] or 'fedora' in id_like:
        return 'redhat', RedHatManager()
    if distro_id == 'arch' or 'arch' in id_like:
        return 'arch', ArchManager()
        
    return None, None

def get_compiler_version(compiler: str) -> Optional[int]:
    """Gets the major version number of the system's compiler."""
    print(f"   -> Checking {compiler} version...", file=sys.stderr)
    output = run_command([compiler, '--version'])
    if not output:
        print(f"Warning: Could not find {compiler}. Is it installed and in your PATH?", file=sys.stderr)
        return None
        
    # Regex for GCC-like and Clang-like version strings
    match = re.search(r'(?:version|\))\s*(\d+)\.', output)
    if match:
        version = int(match.group(1))
        return version
    print(f"Warning: Could not parse {compiler} version string.", file=sys.stderr)
    return None

def get_max_supported_standard(compiler_version: Optional[int]) -> Optional[str]:
    """Determines the highest C++ standard flag supported by the given compiler version."""
    if not compiler_version: return None
    print(f"   -> Detected compiler version {compiler_version}.", file=sys.stderr)
    # This mapping is a good approximation for both modern GCC and Clang.
    if compiler_version >= 11: standard = 'c++20'
    elif compiler_version >= 7: standard = 'c++17'
    elif compiler_version >= 5: standard = 'c++14'
    elif compiler_version >= 4: standard = 'c++11'
    else:
        print("   -> Compiler version is too old to reliably set a modern C++ standard.", file=sys.stderr)
        return None
    print(f"   -> Setting highest supported standard to C++{standard[3:]}.", file=sys.stderr)
    return f'-std={standard}'

def extract_headers_from_file(file_path: str) -> Set[Tuple[str, str]]:
    """Parses a file and extracts all unique headers and their type ('"' or '<')."""
    headers = set()
    try:
        with open(file_path, 'r', encoding='utf-8', errors='ignore') as f:
            for line in f:
                match = INCLUDE_REGEX.match(line)
                if match:
                    quote_type, header_name = match.groups()
                    headers.add((header_name, quote_type))
    except FileNotFoundError:
        pass
    except Exception as e:
        print(f"Warning: Could not read file '{file_path}': {e}", file=sys.stderr)
    return headers

def analyze_source_for_symbols(file_path: str) -> Set[str]:
    """Extracts potential function names from a C++ source file."""
    symbols = set()
    symbol_regex = re.compile(r'\b([a-zA-Z_][a-zA-Z0-9_]*)\s*\(')
    try:
        with open(file_path, 'r', encoding='utf-8') as f:
            content = f.read()
            for match in symbol_regex.finditer(content):
                symbols.add(match.group(1))
    except Exception: pass
    return symbols

def score_header_match(source_symbols: Set[str], header_path: str) -> int:
    """Scores a header file based on how many source symbols it contains."""
    score = 0
    try:
        with open(header_path, 'r', encoding='utf-8', errors='ignore') as f:
            header_content = f.read()
            for symbol in source_symbols:
                if re.search(r'\b' + re.escape(symbol) + r'\b', header_content):
                    score += 1
    except Exception: pass
    return score

def find_local_file(filename: str, start_dir: str) -> Optional[str]:
    """Recursively searches for a file in a directory and its subdirectories."""
    for root, _, files in os.walk(start_dir):
        if filename in files:
            return os.path.join(root, filename)
    return None

def find_header_candidates(header: str, pm: PackageManager, cache: Cache) -> List[Dict[str, str]]:
    """Finds all possible Debian packages and paths for a system header, using a cache."""
    cached_candidates = cache.get('system_headers', header)
    if cached_candidates:
        print(f"   -> Found candidates for '{header}' in cache.", file=sys.stderr)
        return cached_candidates

    candidates = pm.find_header_candidates(header)
    cache.set('system_headers', header, candidates)
    return candidates

def find_package_info(package_name: str, pm: PackageManager, cache: Cache) -> Tuple[Set[str], Set[str]]:
    """Finds all include paths and libraries within a given Debian package, using a cache."""
    cached_info = cache.get('packages', package_name)
    if cached_info:
        print(f"      -> Found info for package '{package_name}' in cache.", file=sys.stderr)
        return (set(cached_info[0]), set(cached_info[1]))

    include_paths, libs = pm.find_package_info(package_name)
    cache.set('packages', package_name, (include_paths, libs))
    return include_paths, libs

def generate_makefile(compiler, output_name, source_files, mode_flags, std_flag, include_paths, libs):
    """Generates a Makefile."""
    cxxflags = []
    if std_flag: cxxflags.append(std_flag)
    if mode_flags: cxxflags.extend(mode_flags)
    for path in sorted(list(include_paths)):
        cxxflags.append(f"-I{path}")

    ldflags = [f"-l{lib}" for lib in sorted(list(libs))]
    
    # Use relative paths for source files to make the Makefile more portable
    relative_sources = [os.path.relpath(p) for p in sorted(list(source_files))]
    sources_str = " ".join(relative_sources)
    
    # Note the explicit \t for tab characters required by make
    makefile_content = f"""
# Generated by cguess.py

CXX = {compiler}
CXXFLAGS = {" ".join(cxxflags)}
LDFLAGS = {" ".join(ldflags)}

SOURCES = {sources_str}
OBJECTS = $(SOURCES:.cpp=.o)
EXECUTABLE = {output_name}

all: $(EXECUTABLE)

$(EXECUTABLE): $(OBJECTS)
\t$(CXX) $(OBJECTS) -o $@ $(LDFLAGS)

%.o: %.cpp
\t$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
\trm -f $(OBJECTS) $(EXECUTABLE)

.PHONY: all clean
"""
    with open("Makefile", "w") as f:
        f.write(makefile_content.strip())
    print("✅ Generated Makefile.", file=sys.stderr)

def generate_cmakefile(output_name, source_files, mode, std_flag, include_paths, libs):
    """Generates a CMakeLists.txt file."""
    project_name = "".join(c for c in output_name if c.isalnum()) or "MyProject"
    
    std_version = std_flag.split('=')[-1].replace('c++', '') if std_flag else "11"

    cmake_content = f"""# Generated by cguess.py
cmake_minimum_required(VERSION 3.10)
project({project_name} CXX)

set(CMAKE_CXX_STANDARD {std_version})
set(CMAKE_CXX_STANDARD_REQUIRED ON)
"""
    if mode == 'release':
        cmake_content += '\nset(CMAKE_BUILD_TYPE Release)\n'
    elif mode == 'debug':
        cmake_content += '\nset(CMAKE_BUILD_TYPE Debug)\n'

    relative_sources = [os.path.relpath(p) for p in sorted(list(source_files))]
    cmake_content += f'\nadd_executable({output_name} {" ".join(relative_sources)})\n'

    if include_paths:
        # Create the indented string of include paths separately to avoid f-string backslash error
        include_dirs_str = "\n    ".join(f'"{path}"' for path in sorted(list(include_paths)))
        cmake_content += f"""
target_include_directories({output_name} PRIVATE
    {include_dirs_str}
)
"""

    if libs:
        # Create the indented string of libraries separately
        libs_str = "\n    ".join(sorted(list(libs)))
        cmake_content += f"""
target_link_libraries({output_name} PRIVATE
    {libs_str}
)
"""
    with open("CMakeLists.txt", "w") as f:
        f.write(cmake_content.strip())
    print("✅ Generated CMakeLists.txt.", file=sys.stderr)


def main():
    parser = argparse.ArgumentParser(description="Generates a full compiler command or build file for a C++ project.")
    parser.add_argument("source_file", help="Path to the main C++ source file.")
    parser.add_argument("-o", "--output", help="Name for the output executable.")
    parser.add_argument("-c", "--compiler", choices=['g++', 'clang++'], default='g++', help="Specify the compiler to use (default: g++).")
    parser.add_argument("-m", "--mode", choices=['debug', 'release'], help="Specify the build mode (debug: -g, release: -O3 -DNDEBUG).")
    parser.add_argument("-g", "--generate", choices=['makefile', 'cmake'], help="Generate a build file instead of printing the command.")
    parser.add_argument("--clear-cache", action="store_true", help="Clear the cache and exit.")
    parser.add_argument("--no-cache", action="store_true", help="Disable cache for this run.")
    args = parser.parse_args()

    if args.clear_cache:
        Cache.clear()
        sys.exit(0)

    distro_id, pm = get_distro()
    if not distro_id or not pm:
        print("Error: Could not determine Linux distribution or distribution is not supported.", file=sys.stderr)
        sys.exit(1)
    
    print(f"🐧 Detected Distro: {distro_id.capitalize()}", file=sys.stderr)

    cache = Cache(distro_id, enabled=not args.no_cache)
    compiler = args.compiler

    main_source_file = os.path.abspath(args.source_file)
    project_root = os.path.dirname(main_source_file)
    output_name = args.output or os.path.splitext(os.path.basename(main_source_file))[0]

    print(f"🔍 Analyzing project starting from '{main_source_file}'...", file=sys.stderr)
    
    compiler_version = get_compiler_version(compiler)
    cpp_standard_flag = get_max_supported_standard(compiler_version)
    
    mode_flags = []
    if args.mode == 'debug':
        mode_flags.append('-g')
        print("🔧 Build mode: debug (-g)", file=sys.stderr)
    elif args.mode == 'release':
        mode_flags.extend(['-O3', '-DNDEBUG'])
        print("🚀 Build mode: release (-O3 -DNDEBUG)", file=sys.stderr)

    all_source_files = {main_source_file}
    all_include_paths = set()
    all_libs = set()
    processed_system_packages = set()
    processed_local_headers = set()
    
    headers_to_process = list(extract_headers_from_file(main_source_file))
    
    while headers_to_process:
        header_name, quote_type = headers_to_process.pop(0)

        if header_name in CPP_STANDARD_HEADERS or header_name in processed_local_headers:
            continue

        local_header_path = None
        if quote_type == '"':
            local_header_path = find_local_file(header_name, project_root)

        if local_header_path:
            abs_local_header_path = os.path.abspath(local_header_path)
            print(f"   -> Found local header: {local_header_path}", file=sys.stderr)
            processed_local_headers.add(abs_local_header_path)
            all_include_paths.add(os.path.dirname(abs_local_header_path))

            base_name = os.path.splitext(abs_local_header_path)[0]
            cpp_file = f"{base_name}.cpp"
            if os.path.exists(cpp_file):
                print(f"      - Found corresponding source file: {cpp_file}", file=sys.stderr)
                all_source_files.add(cpp_file)
            
            new_headers = extract_headers_from_file(abs_local_header_path)
            for h in new_headers:
                if h[0] not in processed_local_headers and h not in headers_to_process:
                    headers_to_process.append(h)
            continue

        print(f"\n--- Processing system header '{header_name}' ---", file=sys.stderr)
        source_symbols = analyze_source_for_symbols(main_source_file)
        candidates = find_header_candidates(header_name, pm, cache)
        if not candidates:
            print(f"   -> Could not find a package for header '{header_name}'.", file=sys.stderr)
            continue
        
        best_candidate = candidates[0]
        if len(candidates) > 1:
            print(f"   -> Found {len(candidates)} candidates. Disambiguating...", file=sys.stderr)
            best_candidate = max(candidates, key=lambda c: score_header_match(source_symbols, c['full_path']))

        package = best_candidate['package']
        if package in processed_system_packages or any(package.startswith(p) for p in IGNORE_PACKAGES):
            continue
        
        processed_system_packages.add(package)
        print(f"   -> Found in package '{package}'. Analyzing package...", file=sys.stderr)
        
        package_includes, package_libs = find_package_info(package, pm, cache)
        all_include_paths.update(package_includes)
        all_libs.update(package_libs)
        print(f"      - Found {len(package_includes)} include paths and {len(package_libs)} libraries.", file=sys.stderr)

    # --- Final Output ---
    if args.generate == 'makefile':
        generate_makefile(compiler, output_name, all_source_files, mode_flags, cpp_standard_flag, all_include_paths, all_libs)
    elif args.generate == 'cmake':
        generate_cmakefile(output_name, all_source_files, args.mode, cpp_standard_flag, all_include_paths, all_libs)
    else:
        command_parts = [compiler]
        command_parts.extend(sorted(list(all_source_files)))
        command_parts.extend(['-o', output_name])
        if mode_flags:
            command_parts.extend(mode_flags)
        if cpp_standard_flag:
            command_parts.append(cpp_standard_flag)
        if all_include_paths:
            command_parts.extend([f"-I{path}" for path in sorted(list(all_include_paths))])
        if all_libs:
            command_parts.extend([f"-l{lib}" for lib in sorted(list(all_libs))])

        full_command = " ".join(command_parts)

        print("\n----------------------------------------", file=sys.stderr)
        print("✅ Generated compiler command:", file=sys.stderr)
        print(full_command)
        print("----------------------------------------", file=sys.stderr)

    cache.close()

if __name__ == "__main__":
    main()
