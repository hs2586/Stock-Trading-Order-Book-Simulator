"""backend.py - all the logic behind the dashboard (no Streamlit code in here).

It talks to the two C++ programs you already built:
  ./analyzer  - finds risky patterns and writes an auto-fixed copy
  ./dash_cli  - the order book engine (reads commands, prints JSON)
and to Ollama (a free local AI) over HTTP.
"""
import difflib
import json
import re
import subprocess
import tempfile
from pathlib import Path

import requests

ROOT = Path(__file__).resolve().parent.parent
ANALYZER = ROOT / "analyzer"
DASH_CLI = ROOT / "dash_cli"
OLLAMA_URL = "http://localhost:11434"
MAX_CODE_CHARS = 4000          # a small local model has a small memory window

SEVERITY_ICON = {"CRITICAL": "🔴", "HIGH": "🟠", "MEDIUM": "🟡"}


# ----------------------------------------------------------------- setup checks
def missing_programs():
    """Names of the C++ programs that have not been built yet."""
    return [p.name for p in (ANALYZER, DASH_CLI) if not p.exists()]


# ---------------------------------------------------------------- code analyzer
_HEADER = re.compile(r"^\[(CRITICAL|HIGH|MEDIUM)\] (CPP-\d+) \((CWE-\d+)\) line (\d+): (.*)$")


def parse_report(text):
    """Turns the analyzer's text report into a list of dictionaries."""
    findings, current = [], None
    for raw in text.splitlines():
        m = _HEADER.match(raw)
        if m:
            current = {"severity": m.group(1), "rule": m.group(2), "cwe": m.group(3),
                       "line": int(m.group(4)), "title": m.group(5),
                       "code": "", "advice": "", "fixed": False}
            findings.append(current)
            continue
        if current is None:
            continue
        s = raw.strip()
        if s.startswith("code:"):
            current["code"] = s[len("code:"):].strip()
        elif s.startswith("advice:"):
            current["advice"] = s[len("advice:"):].strip()
        elif s.startswith("status:"):
            current["fixed"] = s[len("status:"):].strip().startswith("auto-fixed")
    return findings


def run_analyzer(code):
    """Runs ./analyzer on `code`. Returns findings, the auto-fixed code and the raw output."""
    if not code.strip():
        raise ValueError("There is no code to analyze.")
    if not ANALYZER.exists():
        raise FileNotFoundError("The analyzer program has not been built yet.")
    with tempfile.TemporaryDirectory() as tmp:                 # keeps generated files out of your project
        (Path(tmp) / "input.cpp").write_text(code)
        proc = subprocess.run([str(ANALYZER), "input.cpp"], cwd=tmp, input="",   # empty input = answer "no" to the AI questions
                              capture_output=True, text=True, timeout=60)
        fixed_path = Path(tmp) / "autofixed.cpp"
        fixed_code = fixed_path.read_text() if fixed_path.exists() else code
    if proc.returncode != 0:
        raise RuntimeError(proc.stderr.strip() or "The analyzer failed.")
    return {"findings": parse_report(proc.stdout), "fixed_code": fixed_code, "raw": proc.stdout}


def make_diff(original, fixed):
    lines = difflib.unified_diff(original.splitlines(), fixed.splitlines(),
                                 "original", "auto-fixed", lineterm="")
    return "\n".join(lines)


def findings_to_markdown(name, findings):
    out = [f"# Vulnerability report: {name}", "", f"Findings: {len(findings)}", ""]
    for f in findings:
        out += [f"## [{f['severity']}] {f['rule']} ({f['cwe']}) line {f['line']}: {f['title']}",
                f"- code: `{f['code']}`", f"- advice: {f['advice']}",
                f"- status: {'auto-fixed' if f['fixed'] else 'needs manual or AI fix'}", ""]
    return "\n".join(out)


# --------------------------------------------------------------------- local AI
def summarize(findings):
    return "".join(f"- line {f['line']} [{f['severity']}] {f['cwe']} {f['title']}: {f['code']}\n"
                   for f in findings)


def explain_prompt(findings):
    return ("You are a secure C++ code reviewer. For each finding below, explain in two short "
            "sentences why it is dangerous, then show one corrected line of code. Be concise.\n\n"
            "Findings:\n" + summarize(findings))


def fix_prompt(fixed_code, remaining):
    return ("The following C++ code was already partly fixed by an automatic tool. Fix ONLY the "
            "remaining problems listed below. Do not change anything else: keep every other line, "
            "the function names and the behaviour exactly as they are. Output ONLY the complete "
            "code inside one ```cpp code block, with no explanation.\n\nRemaining problems:\n" +
            summarize(remaining) + "\nCode:\n```cpp\n" + fixed_code + "\n```\n")


def extract_code_block(text):
    """Returns (code, complete). complete is False if the answer had no closing fence."""
    start = text.find("```")
    if start == -1:
        return text, False
    nl = text.find("\n", start)
    if nl == -1:
        return text, False
    end = text.find("```", nl)
    if end == -1:
        return text[nl + 1:], False
    return text[nl + 1:end], True


def ollama_models():
    """Names of the models installed in Ollama, or None if Ollama is not running."""
    try:
        r = requests.get(f"{OLLAMA_URL}/api/tags", timeout=3)
        r.raise_for_status()
        return [m["name"] for m in r.json().get("models", [])]
    except (requests.RequestException, ValueError, KeyError):
        return None


def ask_ollama(prompt, model, max_tokens=1500, timeout=600):
    try:
        r = requests.post(f"{OLLAMA_URL}/api/generate", timeout=timeout,
                          json={"model": model, "prompt": prompt, "stream": False,
                                "options": {"num_ctx": 4096, "num_predict": max_tokens}})
    except requests.ConnectionError:
        raise RuntimeError("Could not reach Ollama. Open the Ollama app and try again.")
    except requests.Timeout:
        raise RuntimeError("The AI took too long to answer. Try a smaller model.")
    if r.status_code != 200:
        try:
            detail = r.json().get("error", r.text[:200])
        except ValueError:
            detail = r.text[:200]
        raise RuntimeError(f"Ollama error: {detail}")
    return r.json().get("response", "")


# ------------------------------------------------------------ order book engine
def run_engine(commands, timeout=120):
    """Sends commands to ./dash_cli and returns its JSON answer as a dictionary."""
    if not DASH_CLI.exists():
        raise FileNotFoundError("The dash_cli program has not been built yet.")
    proc = subprocess.run([str(DASH_CLI)], input="\n".join(commands) + "\n",
                          capture_output=True, text=True, timeout=timeout)
    if proc.returncode != 0:
        raise RuntimeError(proc.stderr.strip() or "The order book engine failed.")
    return json.loads(proc.stdout)


def sim_command(orders, seed, mid_price, spread_ticks, market_pct, cancel_pct):
    return f"SIM {int(orders)} {int(seed)} {mid_price:.2f} {int(spread_ticks)} {int(market_pct)} {int(cancel_pct)}"


def order_command(action, price, qty, order_id):
    if action == "Limit buy":
        return f"B {price:.2f} {int(qty)}"
    if action == "Limit sell":
        return f"S {price:.2f} {int(qty)}"
    if action == "Market buy":
        return f"MB {int(qty)}"
    if action == "Market sell":
        return f"MS {int(qty)}"
    return f"C {int(order_id)}"