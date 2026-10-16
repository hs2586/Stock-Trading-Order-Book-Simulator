// analyzer.cpp - Day 5: scan C++ code for risky patterns and print a report
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
    std::regex  pattern;
};

// One problem found in the user's code
struct Finding {
    size_t      line;       // line number, starting at 1
    const Rule* rule;
    std::string code;       // the offending line, trimmed
};

static std::vector<Rule> buildRules() {
    return {
        {"CPP-001", "CRITICAL", "CWE-242", "gets() cannot limit input size",
         "gets() was removed from C++14. Use std::getline or fgets with a size.",
         std::regex(R"(\bgets\s*\()")},
        {"CPP-002", "HIGH", "CWE-120", "strcpy() has no bounds checking",
         "Use std::string, or snprintf with the destination size.",
         std::regex(R"(\bstrcpy\s*\()")},
        {"CPP-003", "HIGH", "CWE-120", "sprintf() can overflow the buffer",
         "Use snprintf with the buffer size, or std::ostringstream.",
         std::regex(R"(\bsprintf\s*\()")},
        {"CPP-004", "HIGH", "CWE-134", "printf with a non-literal format string",
         "If the text is user-controlled, attackers can read or write memory. Use printf(\"%s\", text).",
         std::regex(R"(\bprintf\s*\(\s*[A-Za-z_]\w*\s*\))")},
        {"CPP-005", "HIGH", "CWE-78", "OS command execution",
         "system() and popen() allow command injection. Avoid them or validate input strictly.",
         std::regex(R"(\b(system|popen)\s*\()")},
        {"CPP-006", "MEDIUM", "CWE-338", "rand() is predictable and low quality",
         "Use <random> (std::mt19937). For security-sensitive values use std::random_device.",
         std::regex(R"(\brand\s*\(\s*\))")},
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

static std::vector<Finding> scan(const std::vector<std::string>& lines,
                                 const std::vector<Rule>& rules) {
    std::vector<Finding> findings;
    bool inBlock = false;
    for (size_t i = 0; i < lines.size(); ++i) {
        const std::string code = stripComments(lines[i], inBlock);
        for (const Rule& rule : rules) {
            if (std::regex_search(code, rule.pattern)) {
                findings.push_back({i + 1, &rule, trim(lines[i])});
            }
        }
    }
    return findings;
}

static std::string buildReport(const std::string& name, const std::vector<Finding>& findings) {
    int critical = 0, high = 0, medium = 0;
    for (const Finding& f : findings) {
        if (f.rule->severity == "CRITICAL") ++critical;
        else if (f.rule->severity == "HIGH") ++high;
        else ++medium;
    }

    std::ostringstream out;
    out << "=== Vulnerability Report: " << name << " ===\n"
        << "Findings: " << findings.size() << "  (critical " << critical
        << ", high " << high << ", medium " << medium << ")\n\n";

    for (const Finding& f : findings) {
        out << "[" << f.rule->severity << "] " << f.rule->id << " (" << f.rule->cwe << ")"
            << " line " << f.line << ": " << f.rule->title << "\n"
            << "    code:   " << f.code << "\n"
            << "    advice: " << f.rule->advice << "\n\n";
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
    std::vector<Finding> findings = scan(lines, rules);

    // Most severe first; same severity -> by line number
    std::sort(findings.begin(), findings.end(), [](const Finding& a, const Finding& b) {
        const int ra = severityRank(a.rule->severity);
        const int rb = severityRank(b.rule->severity);
        if (ra != rb) return ra < rb;
        return a.line < b.line;
    });

    const std::string report = buildReport(name, findings);
    std::cout << "\n" << report;

    std::ofstream file("analysis_report.txt");
    file << report;
    std::cout << "Report saved to analysis_report.txt\n";
    return 0;
}