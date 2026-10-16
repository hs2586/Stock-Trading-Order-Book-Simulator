// analyzer.cpp - Day 6 (part A): scan C++ code, print a report, write an auto-fixed copy
//
// Usage:
//   ./analyzer path/to/file.cpp     analyze a file
//   ./analyzer                      paste code, finish with a line containing only END
#include <algorithm>
#include <fstream>
#include <iostream>
#include <regex>
#include <sstream>
#include <string>
#include <vector>

// One detection rule: "if a line matches this pattern, report this problem"
struct Rule {
    std::string id;
    std::string severity;   // CRITICAL, HIGH or MEDIUM
    std::string cwe;        // official weakness number, see cwe.mitre.org
    std::string title;
    std::string advice;
    std::regex  pattern;    // detects the problem
    std::regex  fixPattern; // used to rewrite the line (only if fixTo is not empty)
    std::string fixTo;      // replacement text; empty = no safe automatic fix
};

// One problem found in the user's code
struct Finding {
    size_t      line;       // line number, starting at 1
    const Rule* rule;
    std::string code;       // the offending line, trimmed
    bool        fixed;      // true if the automatic fixer rewrote this line
};

// Small helper so rules without an auto-fix stay short
static Rule makeRule(const std::string& id, const std::string& severity, const std::string& cwe,
                     const std::string& title, const std::string& advice, const char* detect,
                     const char* fixFrom = "x^", const std::string& fixTo = "") {
    return Rule{id, severity, cwe, title, advice, std::regex(detect), std::regex(fixFrom), fixTo};
}

static std::vector<Rule> buildRules() {
    return {
        makeRule("CPP-001", "CRITICAL", "CWE-242", "gets() cannot limit input size",
                 "gets() was removed from C++14. Use std::getline or fgets with a size.",
                 R"(\bgets\s*\()",
                 R"(\bgets\s*\(\s*(\w+)\s*\))",
                 "fgets($1, sizeof($1), stdin) /* AUTOFIX: dest must be an array */"),
        makeRule("CPP-002", "HIGH", "CWE-120", "strcpy() has no bounds checking",
                 "Use std::string, or snprintf with the destination size.",
                 R"(\bstrcpy\s*\()",
                 R"(\bstrcpy\s*\(\s*(\w+)\s*,\s*([^;]+?)\s*\)\s*;)",
                 "snprintf($1, sizeof($1), \"%s\", $2); /* AUTOFIX: dest must be an array */"),
        makeRule("CPP-003", "HIGH", "CWE-120", "sprintf() can overflow the buffer",
                 "Use snprintf with the buffer size, or std::ostringstream.",
                 R"(\bsprintf\s*\()",
                 R"(\bsprintf\s*\(\s*(\w+)\s*,)",
                 "snprintf($1, sizeof($1),"),
        makeRule("CPP-004", "HIGH", "CWE-134", "printf with a non-literal format string",
                 "If the text is user-controlled, attackers can read or write memory. Use printf(\"%s\", text).",
                 R"(\bprintf\s*\(\s*[A-Za-z_]\w*\s*\))",
                 R"(\bprintf\s*\(\s*([A-Za-z_]\w*)\s*\))",
                 "printf(\"%s\", $1)"),
        makeRule("CPP-005", "HIGH", "CWE-78", "OS command execution",
                 "system() and popen() allow command injection. Avoid them or validate input strictly.",
                 R"(\b(system|popen)\s*\()"),
        makeRule("CPP-006", "MEDIUM", "CWE-338", "rand() is predictable and low quality",
                 "Use <random> (std::mt19937). For security-sensitive values use std::random_device.",
                 R"(\brand\s*\(\s*\))"),
        makeRule("CPP-007", "HIGH", "CWE-120", "strcat() has no bounds checking",
                 "Use std::string::append, or strncat with the remaining space.",
                 R"(\bstrcat\s*\()",
                 R"(\bstrcat\s*\(\s*(\w+)\s*,\s*([^;]+?)\s*\)\s*;)",
                 "strncat($1, $2, sizeof($1) - strlen($1) - 1); /* AUTOFIX: dest must be an array */"),
    };
}

static int severityRank(const std::string& s) {
    if (s == "CRITICAL") return 0;
    if (s == "HIGH") return 1;
    return 2;
}

static std::string trim(const std::string& s) {
    const auto first = s.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return "";
    const auto last = s.find_last_not_of(" \t\r\n");
    return s.substr(first, last - first + 1);
}

// Removes // and /* */ comments so that commented-out code is not reported.
// inBlock remembers whether we are inside a /* ... */ that started on an earlier line.
static std::string stripComments(const std::string& line, bool& inBlock) {
    std::string out;
    bool inString = false;
    for (size_t i = 0; i < line.size(); ++i) {
        const char c = line[i];
        const char next = (i + 1 < line.size()) ? line[i + 1] : '\0';
        if (inBlock) {
            if (c == '*' && next == '/') { inBlock = false; ++i; }
            continue;
        }
        if (!inString && c == '/' && next == '*') { inBlock = true; ++i; continue; }
        if (!inString && c == '/' && next == '/') break;       // rest of the line is a comment
        if (c == '"' && !(inString && i > 0 && line[i - 1] == '\\')) inString = !inString;
        out += c;
    }
    return out;
}

// Finds problems AND builds fixedLines, a copy of the code with safe fixes applied.
// The original lines are never changed.
static std::vector<Finding> scan(const std::vector<std::string>& lines,
                                 const std::vector<Rule>& rules,
                                 std::vector<std::string>& fixedLines) {
    std::vector<Finding> findings;
    fixedLines = lines;
    bool inBlock = false;
    for (size_t i = 0; i < lines.size(); ++i) {
        const std::string code = stripComments(lines[i], inBlock);
        for (const Rule& rule : rules) {
            if (!std::regex_search(code, rule.pattern)) continue;

            bool fixed = false;
            if (!rule.fixTo.empty()) {
                const std::string rewritten =
                    std::regex_replace(fixedLines[i], rule.fixPattern, rule.fixTo);
                if (rewritten != fixedLines[i]) {
                    fixedLines[i] = rewritten;
                    fixed = true;
                }
            }
            findings.push_back({i + 1, &rule, trim(lines[i]), fixed});
        }
    }
    return findings;
}

static std::string buildReport(const std::string& name, const std::vector<Finding>& findings) {
    int critical = 0, high = 0, medium = 0, autoFixed = 0;
    for (const Finding& f : findings) {
        if (f.rule->severity == "CRITICAL") ++critical;
        else if (f.rule->severity == "HIGH") ++high;
        else ++medium;
        if (f.fixed) ++autoFixed;
    }

    std::ostringstream out;
    out << "=== Vulnerability Report: " << name << " ===\n"
        << "Findings: " << findings.size() << "  (critical " << critical
        << ", high " << high << ", medium " << medium << ")\n"
        << "Auto-fixed: " << autoFixed << "   Need manual/AI fix: "
        << (findings.size() - autoFixed) << "\n\n";

    for (const Finding& f : findings) {
        out << "[" << f.rule->severity << "] " << f.rule->id << " (" << f.rule->cwe << ")"
            << " line " << f.line << ": " << f.rule->title << "\n"
            << "    code:   " << f.code << "\n"
            << "    advice: " << f.rule->advice << "\n"
            << "    status: " << (f.fixed ? "auto-fixed in autofixed.cpp" : "needs manual or AI fix")
            << "\n\n";
    }
    if (findings.empty()) {
        out << "No known risky patterns found.\n"
            << "(This is a pattern scan. It does not prove the code is safe.)\n";
    }
    return out.str();
}

int main(int argc, char** argv) {
    std::vector<std::string> lines;
    std::string name = "pasted_code";

    if (argc > 1) {
        std::ifstream in(argv[1]);
        if (!in) {
            std::cerr << "cannot open file: " << argv[1] << "\n";
            return 1;
        }
        std::string line;
        while (std::getline(in, line)) lines.push_back(line);
        name = argv[1];
    } else {
        std::cout << "Paste your C++ code. Finish with a line containing only END\n";
        std::string line;
        while (std::getline(std::cin, line) && trim(line) != "END") lines.push_back(line);
    }

    if (lines.empty()) {
        std::cerr << "no code received\n";
        return 1;
    }

    const std::vector<Rule> rules = buildRules();
    std::vector<std::string> fixedLines;
    std::vector<Finding> findings = scan(lines, rules, fixedLines);

    // Most severe first; same severity -> by line number
    std::sort(findings.begin(), findings.end(), [](const Finding& a, const Finding& b) {
        const int ra = severityRank(a.rule->severity);
        const int rb = severityRank(b.rule->severity);
        if (ra != rb) return ra < rb;
        return a.line < b.line;
    });

    const std::string report = buildReport(name, findings);
    std::cout << "\n" << report;
    { std::ofstream file("analysis_report.txt"); file << report; }
    std::cout << "Report saved to analysis_report.txt\n";

    int fixCount = 0;
    for (const Finding& f : findings) if (f.fixed) ++fixCount;
    if (fixCount > 0) {
        std::ofstream fixedFile("autofixed.cpp");
        for (const std::string& l : fixedLines) fixedFile << l << "\n";
        std::cout << fixCount << " automatic fix(es) written to autofixed.cpp\n"
                  << "Your original file was NOT changed. Review autofixed.cpp before using it.\n";
    }
    return 0;
}