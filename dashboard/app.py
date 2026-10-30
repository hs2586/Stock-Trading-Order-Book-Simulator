"""app.py - Streamlit dashboard for the Order Book Lab project.

Run it from the project folder with:   streamlit run dashboard/app.py
All the real work is done in backend.py and by your C++ programs.
"""
import pandas as pd
import streamlit as st

import backend as bk

st.set_page_config(page_title="Order Book Lab", page_icon="📈", layout="wide")

EXAMPLE_CODE = r'''#include <cstdio>
#include <cstdlib>
#include <cstring>

// A deliberately buggy order-entry handler for testing the analyzer
void handle(char* input) {
    char name[16];
    char buf[64];

    strcpy(name, input);
    sprintf(buf, "ORDER %s", name);
    printf(buf);
    gets(name);
    system("echo order received");
    int r = rand();

    printf("done %d\n", r);
}
'''

SAMPLE_ORDERS = ["B 99.00 10", "B 100.00 15", "B 100.00 5", "S 101.00 8", "S 102.00 12", "S 101.00 6"]


# =============================================================== small helpers
def levels_df(levels):
    return pd.DataFrame({"Price ($)": [p / 100 for p, _ in levels],
                         "Quantity": [q for _, q in levels]})


def trades_df(trades):
    rows = [{"Buy order": t["buy"], "Sell order": t["sell"],
             "Price ($)": t["price"] / 100, "Quantity": t["qty"]} for t in reversed(trades)]
    return pd.DataFrame(rows, columns=["Buy order", "Sell order", "Price ($)", "Quantity"])


def render_book(book):
    """Shows the order book: best prices, both sides as tables, and a depth chart."""
    bids, asks = book["bids"], book["asks"]
    c1, c2, c3 = st.columns(3)
    c1.metric("Best bid", f"${bids[0][0] / 100:,.2f}" if bids else "-")
    c2.metric("Best ask", f"${asks[0][0] / 100:,.2f}" if asks else "-")
    c3.metric("Spread", f"${(asks[0][0] - bids[0][0]) / 100:,.2f}" if bids and asks else "-")

    left, right = st.columns(2)
    with left:
        st.markdown("**Asks (sellers)**, best price at the bottom")
        st.dataframe(levels_df(list(reversed(asks))), hide_index=True)
    with right:
        st.markdown("**Bids (buyers)**, best price at the top")
        st.dataframe(levels_df(bids), hide_index=True)

    if bids or asks:                                   # cumulative depth chart
        rows, total = {}, 0
        for price, qty in bids:
            total += qty
            rows[price / 100] = {"Bids": total, "Asks": 0}
        total = 0
        for price, qty in asks:
            total += qty
            rows[price / 100] = {"Bids": 0, "Asks": total}
        st.caption("Market depth: total quantity available up to each price")
        st.bar_chart(pd.DataFrame.from_dict(rows, orient="index").sort_index())


def load_example():
    st.session_state["code_input"] = EXAMPLE_CODE


# ================================================================== sidebar
with st.sidebar:
    st.header("📈 Order Book Lab")
    st.caption("A C++ matching engine and a security code analyzer.")

    missing = bk.missing_programs()
    if missing:
        st.error("Not built yet: " + ", ".join(missing))
    else:
        st.success("C++ programs found")

    st.subheader("AI assistant")
    models = bk.ollama_models()
    model = None
    if models is None:
        st.warning("Ollama is not running. Open the Ollama app to enable the AI features.")
    elif not models:
        st.warning("No AI model installed. In a terminal run: ollama pull qwen2.5-coder:3b")
    else:
        default = models.index("qwen2.5-coder:3b") if "qwen2.5-coder:3b" in models else 0
        model = st.selectbox("Model (runs on this computer)", models, index=default)
        st.caption("Nothing is uploaded. Your code stays on this computer.")

st.title("Order Book Lab")

if missing:
    st.error("Some C++ programs are missing, so the dashboard cannot work yet.")
    st.markdown("Run these in a terminal, from the project folder, then refresh this page:")
    st.code("g++ -std=c++17 -O2 tools/analyzer.cpp -o analyzer -lcurl\n"
            "g++ -std=c++17 -O2 src/dash_cli.cpp -o dash_cli", language="bash")
    st.stop()


# ============================================================ tab 1: analyzer
def render_analyzer():
    st.subheader("Find security problems in C++ code")
    source = st.radio("Input method", ["Paste code", "Upload a file"], horizontal=True, key="source")

    code, name = "", "pasted_code.cpp"
    if source == "Paste code":
        st.text_area("C++ code", height=280, key="code_input", placeholder="Paste your C++ code here...")
        st.button("Load an example with bugs", on_click=load_example, key="load_example")
        code = st.session_state.get("code_input", "")
    else:
        uploaded = st.file_uploader("Choose a source file", type=["cpp", "cc", "cxx", "c", "hpp", "h"],
                                    key="uploaded_file")
        if uploaded is not None:
            code = uploaded.getvalue().decode("utf-8", errors="replace")
            name = uploaded.name

    if st.button("🔍 Analyze", type="primary", key="analyze"):
        try:
            with st.spinner("Scanning..."):
                result = bk.run_analyzer(code)
            st.session_state["analysis"] = {"name": name, "code": code, **result}
            st.session_state.pop("ai_explain", None)       # old AI answers belong to old code
            st.session_state.pop("ai_fix", None)
        except Exception as error:
            st.error(str(error))

    analysis = st.session_state.get("analysis")
    if not analysis:
        st.info("Paste or upload code, then click Analyze.")
        return

    findings = analysis["findings"]
    st.divider()
    st.subheader(f"Results for {analysis['name']}")
    if not findings:
        st.success("No known risky patterns found. (This is a pattern scan, so it does not prove the code is safe.)")
        return

    count = lambda sev: sum(1 for f in findings if f["severity"] == sev)
    auto_fixed = sum(1 for f in findings if f["fixed"])
    cols = st.columns(5)
    cols[0].metric("Findings", len(findings))
    cols[1].metric("Critical", count("CRITICAL"))
    cols[2].metric("High", count("HIGH"))
    cols[3].metric("Medium", count("MEDIUM"))
    cols[4].metric("Auto-fixed", auto_fixed)

    table = pd.DataFrame([{
        "Severity": f"{bk.SEVERITY_ICON[f['severity']]} {f['severity']}",
        "Rule": f["rule"], "CWE": f["cwe"], "Line": f["line"], "Problem": f["title"],
        "Code": f["code"], "Status": "✅ auto-fixed" if f["fixed"] else "⚠️ needs a fix"} for f in findings])
    st.dataframe(table, hide_index=True)

    with st.expander("What should I do about each problem?"):
        for f in findings:
            st.markdown(f"**Line {f['line']}: {f['title']}** ({f['cwe']})")
            st.write(f["advice"])

    # ---- automatic fixes
    st.subheader("Automatic fixes")
    st.caption("Safe, mechanical rewrites only. Your original code is never changed.")
    if auto_fixed:
        st.code(bk.make_diff(analysis["code"], analysis["fixed_code"]), language="diff")
        st.warning("Review before using: the fixer assumes the destination is an array (sizeof). "
                   "If it is a pointer, the copy would be cut short.")
        d1, d2 = st.columns(2)
        d1.download_button("⬇ Download auto-fixed code", analysis["fixed_code"],
                           file_name="autofixed.cpp", key="dl_fixed")
        d2.download_button("⬇ Download report (Markdown)",
                           bk.findings_to_markdown(analysis["name"], findings),
                           file_name="analysis_report.md", key="dl_report")
    else:
        st.info("None of these problems has a safe automatic fix.")

    # ---- AI assistant
    st.subheader("🤖 AI assistant")
    st.caption("Nothing is sent to the AI until you click a button. The AI can be wrong, so every answer is checked.")
    if model is None:
        st.info("Open the Ollama app (see the sidebar) to use the AI features.")
        return

    if st.button("Explain the risks", key="explain"):
        try:
            with st.spinner("Asking the AI... this can take a minute"):
                st.session_state["ai_explain"] = bk.ask_ollama(bk.explain_prompt(findings), model, 1500)
        except Exception as error:
            st.error(str(error))
    if st.session_state.get("ai_explain"):
        st.markdown(st.session_state["ai_explain"])

    remaining = [f for f in findings if not f["fixed"]]
    st.markdown("**Corrected code**")
    if not remaining:
        st.success("The automatic fixes handled every problem, so the AI has nothing left to rewrite.")
        return
    st.info(f"{len(remaining)} problem(s) remain after the automatic fixes. "
            f"The AI only gets the code with the automatic fixes applied and these {len(remaining)} problems.")
    if len(analysis["fixed_code"]) > bk.MAX_CODE_CHARS:
        st.warning(f"The code is too long for the small local model (limit {bk.MAX_CODE_CHARS} characters).")
        return

    if st.button("Generate corrected code with the AI?", key="generate_fix"):
        try:
            with st.spinner("Generating... this can take a minute or two"):
                answer = bk.ask_ollama(bk.fix_prompt(analysis["fixed_code"], remaining), model, 2048)
                ai_code, complete = bk.extract_code_block(answer)
                try:                                                     # trust but verify
                    after = bk.run_analyzer(ai_code)["findings"]
                except Exception:
                    after = None
            st.session_state["ai_fix"] = {"code": ai_code, "complete": complete,
                                          "before": len(remaining), "after": after}
        except Exception as error:
            st.error(str(error))

    ai_fix = st.session_state.get("ai_fix")
    if ai_fix:
        if not ai_fix["complete"]:
            st.warning("The AI answer had no complete code block. It may be cut off or contain extra text.")
        after = ai_fix["after"]
        if after is None:
            st.warning("Could not re-scan the AI code.")
        else:
            m1, m2 = st.columns(2)
            m1.metric("Problems before the AI step", ai_fix["before"])
            m2.metric("Problems after the AI step (our re-scan)", len(after))
            if after:
                st.error("The AI did not fix everything: " + ", ".join(
                    f"line {f['line']} ({f['rule']})" for f in after))
            else:
                st.success("Our scanner found no known risky patterns in the AI's code. Still compile and test it!")
        st.markdown("What the AI changed compared with the automatic fixes:")
        st.code(bk.make_diff(analysis["fixed_code"], ai_fix["code"]) or "(no changes)", language="diff")
        st.download_button("⬇ Download AI code", ai_fix["code"], file_name="ai_fixed.cpp", key="dl_ai")


# ========================================================= tab 2: order book
def render_random_simulation():
    st.markdown("Fire thousands of random orders at the C++ matching engine and watch what happens.")
    c1, c2, c3 = st.columns(3)
    orders = c1.number_input("Number of orders", min_value=100, max_value=500000, value=20000, step=1000, key="sim_orders")
    seed = c1.number_input("Random seed (same seed = same run)", min_value=0, max_value=999999, value=42, step=1, key="sim_seed")
    mid = c2.number_input("Starting price ($)", min_value=5.0, max_value=10000.0, value=100.0, step=1.0, key="sim_mid")
    spread = c2.slider("Price spread (± ticks, 1 tick = $0.01)", 5, 400, 50, key="sim_spread")
    market_pct = c3.slider("Market orders (%)", 0, 60, 20, key="sim_market")
    cancel_pct = c3.slider("Cancels (%)", 0, 40, 10, key="sim_cancel")

    if st.button("▶ Run simulation", type="primary", key="run_sim"):
        try:
            with st.spinner("Running..."):
                st.session_state["sim_result"] = bk.run_engine(
                    [bk.sim_command(orders, seed, mid, spread, market_pct, cancel_pct)])
        except Exception as error:
            st.error(str(error))

    data = st.session_state.get("sim_result")
    if not data:
        st.info("Choose the settings and click Run simulation.")
        return

    stats = data["stats"]
    st.divider()
    if stats["sim_orders"] == 0:                       # the engine refused the settings
        st.error(data["log"][0]["msg"] if data["log"] else "The simulation did not run.")
        return
    if stats["invariants_ok"]:
        st.success("All safety checks passed: the book was never crossed, and every trade was valid.")
    else:
        st.error("Safety check FAILED: " + stats["invariant_message"])
    m = st.columns(5)
    m[0].metric("Orders processed", f"{stats['sim_orders']:,}")
    m[1].metric("Trades", f"{stats['trades']:,}")
    m[2].metric("Traded volume", f"{stats['volume']:,}")
    m[3].metric("Time", f"{stats['sim_seconds'] * 1000:,.1f} ms")
    m[4].metric("Throughput", f"{stats['orders_per_sec']:,} orders/s")
    st.caption(data["log"][0]["msg"] if data["log"] else "")

    if data["price_series"]:
        st.markdown("**Trade price over time**")
        st.line_chart(pd.DataFrame({"Trade price ($)": [p / 100 for p in data["price_series"]]}))
    st.markdown("**Order book at the end of the simulation**")
    render_book(data["book"])
    st.markdown("**Most recent trades**")
    st.dataframe(trades_df(data["trades"][-20:]), hide_index=True)


def render_manual_trading():
    st.markdown("Place orders yourself and see the matching engine react. Orders are numbered 1, 2, 3... "
                "so you can cancel one by its id.")
    if "manual_cmds" not in st.session_state:
        st.session_state["manual_cmds"] = []

    left, right = st.columns([1, 2])
    with left:
        with st.form("order_form"):
            action = st.selectbox("Action", ["Limit buy", "Limit sell", "Market buy", "Market sell", "Cancel order"])
            price = st.number_input("Price ($), for limit orders", min_value=0.01, max_value=100000.0, value=100.0, step=0.5)
            qty = st.number_input("Quantity", min_value=1, max_value=1000000, value=10, step=1)
            order_id = st.number_input("Order id (for cancel)", min_value=1, max_value=10000000, value=1, step=1)
            submitted = st.form_submit_button("Send order")
        if submitted:
            st.session_state["manual_cmds"].append(bk.order_command(action, price, qty, order_id))
        b1, b2 = st.columns(2)
        if b1.button("Add sample orders", key="add_samples"):
            st.session_state["manual_cmds"].extend(SAMPLE_ORDERS)
        if b2.button("Reset book", key="reset_book"):
            st.session_state["manual_cmds"] = []

    try:
        data = bk.run_engine(st.session_state["manual_cmds"])
    except Exception as error:
        st.error(str(error))
        return

    with right:
        st.markdown("**What happened** (newest first)")
        if data["log"]:
            log = pd.DataFrame([{"Command": e["cmd"], "Result": e["msg"]} for e in reversed(data["log"])])
            st.dataframe(log.head(12), hide_index=True)
        else:
            st.info("Send an order, or click 'Add sample orders'.")
    st.divider()
    render_book(data["book"])
    if data["trades"]:
        st.markdown("**Trades**")
        st.dataframe(trades_df(data["trades"]), hide_index=True)


def render_simulator():
    random_tab, manual_tab = st.tabs(["🎲 Random market simulation", "🕹️ Manual trading"])
    with random_tab:
        render_random_simulation()
    with manual_tab:
        render_manual_trading()


# ================================================================ tab 3: about
def render_about():
    st.markdown("""
### How this project works
- **C++ matching engine** (`src/orderbook.hpp`): price-time priority, limit and market orders, cancel.
- **C++ analyzer** (`tools/analyzer.cpp`): finds risky patterns, writes safe automatic fixes.
- **Local AI** (Ollama): explains the risks and tries to fix what the rules cannot. Its answers are re-checked by the analyzer.
- **This dashboard** only shows the results. All the real work is done by the C++ programs.

### Known limitations
- The analyzer matches text patterns. It does not parse C++, so it can flag words inside strings (false positives).
- The auto-fixer assumes destinations are arrays. A pointer destination would be truncated.
- A fix can use a function whose header the file does not include (for example `snprintf` without `<cstdio>`).
- `system()` is flagged the same way whether its argument is constant or comes from a user.
- A small local AI model is not reliable for security fixes, so always review.
""")


tab_analyzer, tab_simulator, tab_about = st.tabs(["🔍 Code security analyzer", "📈 Order book simulator", "ℹ️ About"])
with tab_analyzer:
    render_analyzer()
with tab_simulator:
    render_simulator()
with tab_about:
    render_about()