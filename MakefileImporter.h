#pragma once

#include "GediProject.h"

#include <string>
#include <vector>

// ═══════════════════════════════════════════════════════════════════════════════
// MakefileImporter
//
// Reads an existing (GNU) Makefile and turns it into a GediProject whose build
// file is that Makefile (GediProject::build_file) - gedi will build it with
// `make` and never regenerate it.
//
// The importer evaluates the makefile much like GNU make's read phase does:
//   * variables of every flavour (=  :=  ::=  ?=  +=  !=), define/endef,
//     override/export, target-specific variables (ignored)
//   * conditionals (ifeq/ifneq/ifdef/ifndef, else-chains)
//   * include / -include / sinclude, relative to make's working directory
//   * the text functions (subst, patsubst, filter, wildcard, foreach, call,
//     eval, if/or/and, shell, ...) and substitution references
//   * explicit, pattern, static-pattern and suffix rules, VPATH / vpath
//   * recursive make: `$(MAKE) -C dir`, `cd dir && $(MAKE)`, `-f file`,
//     `for d in $(SUBDIRS); do $(MAKE) -C $$d; done`, `$(SUBDIRS): ; $(MAKE) -C $@`
//
// Link targets (executables, static and shared libraries) become project
// targets; their sources are found by following object prerequisites back
// through the rules to the files on disk.  Header dependencies come from the
// rules (e.g. generated .d files) and from scanning #include lines against
// the -I directories the makefile uses.
//
// Sources may live anywhere - sub-directories or parent directories of the
// makefile; paths are stored relative to the makefile's directory (which
// becomes the project root) and may therefore start with "../".
// ═══════════════════════════════════════════════════════════════════════════════

struct MakefileImportResult {
    bool        ok = false;
    std::string error;

    GediProject project;                 // root = directory of the makefile

    std::vector<std::string> makefiles;  // every makefile that was read (absolute)
    std::vector<std::string> warnings;
    size_t source_count = 0;             // distinct files across all targets
};

class MakefileImporter {
public:
    struct Options {
        bool allow_shell = true;         // evaluate $(shell ...) / != while reading
        int  max_makefiles = 256;        // guard against runaway recursion
    };

    static MakefileImportResult import(const std::string& makefile_path);
    static MakefileImportResult import(const std::string& makefile_path, const Options& opts);

    // True for Makefile / makefile / GNUmakefile / *.mk / *.mak / *.make
    static bool looksLikeMakefile(const std::string& path);
};
