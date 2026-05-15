import subprocess
import json
import shutil
import re

def parse_flags(flag_string):
    """
    Separates flags into includes, library names, library paths, and general flags.
    """
    parsed = {
        "include_dirs": [],
        "link_libraries": [],
        "link_paths": [],
        "other_compiler_flags": []
    }
    
    parts = flag_string.split()
    for part in parts:
        if part.startswith('-I'):
            parsed["include_dirs"].append(part[2:])
        elif part.startswith('-l'):
            parsed["link_libraries"].append(part[2:])
        elif part.startswith('-L'):
            parsed["link_paths"].append(part[2:])
        else:
            # Catch-all for things like -pthread, -m64, -fstack-protector
            parsed["other_compiler_flags"].append(part)
            
    return parsed

def get_library_data():
    if not shutil.which("pkg-config"):
        return {"error": "pkg-config is not installed."}

    try:
        result = subprocess.run(['pkg-config', '--list-all'], capture_output=True, text=True, check=True)
        lines = result.stdout.strip().split('\n')
    except subprocess.CalledProcessError:
        return {"error": "Failed to list packages."}

    libraries = []

    for line in lines:
        if not line: continue
        parts = line.split(maxsplit=1)
        short_name = parts[0]
        description = parts[1] if len(parts) > 1 else ""

        try:
            version = subprocess.check_output(['pkg-config', '--modversion', short_name], text=True, stderr=subprocess.DEVNULL).strip()
            
            # Fetch raw flags
            raw_cflags = subprocess.check_output(['pkg-config', '--cflags', short_name], text=True, stderr=subprocess.DEVNULL).strip()
            raw_libs = subprocess.check_output(['pkg-config', '--libs', short_name], text=True, stderr=subprocess.DEVNULL).strip()

            # Parse flags into specific CMake-friendly categories
            cflags_parsed = parse_flags(raw_cflags)
            libs_parsed = parse_flags(raw_libs)

            cmake_var = re.sub(r'[^A-Z0-9]', '_', short_name.upper())
            libraries.append({
                "short_name": short_name,
                "version": version,
                "description": description,
                "cmake_find_package_hint": f"pkg_check_modules({cmake_var} REQUIRED {short_name})",
                "include_directories": list(set(cflags_parsed["include_dirs"])),
                "link_libraries": list(set(libs_parsed["link_libraries"])),
                "link_directories": list(set(libs_parsed["link_paths"])),
                "compiler_flags": list(set(cflags_parsed["other_compiler_flags"] + libs_parsed["other_compiler_flags"]))
            })
        except subprocess.CalledProcessError:
            continue

    return libraries

if __name__ == "__main__":
    print(json.dumps(get_library_data(), indent=4))