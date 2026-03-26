const reloadButton = document.getElementById("reload-button");
const datasetSelect = document.getElementById("dataset-select");
const engineButtons = [...document.querySelectorAll("[data-engine]")];
const playButton = document.getElementById("play-button");
const pauseButton = document.getElementById("pause-button");
const stepButton = document.getElementById("step-button");
const resetButton = document.getElementById("reset-button");
const timeline = document.getElementById("timeline");
const speedRange = document.getElementById("speed-range");
const speedLabel = document.getElementById("speed-label");
const stepLabel = document.getElementById("step-label");
const statusBanner = document.getElementById("status-banner");
const tabButtons = [...document.querySelectorAll(".tab-button")];
const bestBid = document.getElementById("best-bid");
const bestBidQty = document.getElementById("best-bid-qty");
const bestAsk = document.getElementById("best-ask");
const bestAskQty = document.getElementById("best-ask-qty");
const eventBadge = document.getElementById("event-badge");
const eventGrid = document.getElementById("event-grid");
const resultPill = document.getElementById("result-pill");
const resultGrid = document.getElementById("result-grid");
const enginePill = document.getElementById("engine-pill");
const bookRows = document.getElementById("book-rows");
const spreadValue = document.getElementById("spread-value");
const midPrice = document.getElementById("mid-price");
const buyPressure = document.getElementById("buy-pressure");
const sellPressure = document.getElementById("sell-pressure");
const buyPressureBar = document.getElementById("buy-pressure-bar");
const sellPressureBar = document.getElementById("sell-pressure-bar");
const ingressFill = document.getElementById("ingress-fill");
const egressFill = document.getElementById("egress-fill");
const matcherPulse = document.getElementById("matcher-pulse");
const ingressQueue = document.getElementById("ingress-queue");
const egressQueue = document.getElementById("egress-queue");
const matcherActive = document.getElementById("matcher-active");
const orderTape = document.getElementById("order-tape");
const fillsList = document.getElementById("fills-list");
const overviewPanel = document.getElementById("overview-panel");
const analysisPanel = document.getElementById("analysis-panel");
const modelsPanel = document.getElementById("models-panel");
const reportPanel = document.getElementById("report-panel");

const datasetSources = {
  balanced: {
    label: "Balanced",
    csv: "balanced",
    replay: "balanced",
    analysis: "balanced",
    stageBench: null,
    evaluation: null,
  },
  cancel_heavy: {
    label: "Cancel Heavy",
    csv: "cancel_heavy",
    replay: "cancel_heavy",
    analysis: "cancel_heavy",
    stageBench: null,
    evaluation: null,
  },
  bursty: {
    label: "Bursty",
    csv: "bursty",
    replay: "bursty",
    analysis: "bursty",
    stageBench: null,
    evaluation: null,
  },
  aapl_lobster: {
    label: "AAPL",
    csv: "aapl_lobster",
    replay: "aapl_lobster_normalized",
    analysis: "aapl_lobster_normalized",
    stageBench: "aapl_stage_bench_binary",
    evaluation: "aapl_eval_binary",
  },
};

const state = {
  replay: null,
  metrics: [],
  analysis: {
    comparisonMarkdown: "",
    flamegraphPath: null,
    perfPath: null,
    stageMetrics: [],
    evaluation: null,
  },
  reportMarkdown: "",
  currentIndex: 0,
  timerId: null,
  stepsPerSecond: Number(speedRange.value),
  selectedDataset: datasetSelect?.value ?? "aapl_lobster",
  selectedReplay: engineButtons.find((button) => button.classList.contains("active"))?.dataset.engine ?? "optimized",
};

const statusNames = ["Accepted", "Partially Filled", "Filled", "Cancelled", "Rejected"];
const compactStatusNames = ["OK", "Part", "Fill", "Cxl", "Rej"];
const statusClassNames = ["accepted", "partial", "filled", "cancelled", "rejected"];

function formatNumber(value, digits = 0) {
  if (value === null || value === undefined || value === "") {
    return "-";
  }
  return Number(value).toLocaleString(undefined, { maximumFractionDigits: digits });
}

function safeText(value) {
  return value === undefined || value === null || value === "" ? "-" : String(value);
}

function escapeHtml(value) {
  return String(value)
    .replaceAll("&", "&amp;")
    .replaceAll("<", "&lt;")
    .replaceAll(">", "&gt;");
}

function parseCsvLine(line) {
  return line.split(",").map((value) => value.trim());
}

function numericValue(value) {
  const parsed = Number(value);
  return Number.isFinite(parsed) ? parsed : 0;
}

function activeDatasetLabel() {
  const source = datasetSources[state.selectedDataset] ?? datasetSources.balanced;
  return source.label;
}

function activeReplayLabel() {
  if (state.selectedReplay === "baseline") {
    return "Baseline";
  }
  if (state.selectedReplay === "pipeline") {
    return "Pipeline";
  }
  return "Optimized";
}

function setActiveButton(buttons, key, value) {
  buttons.forEach((button) => {
    button.classList.toggle("active", button.dataset[key] === value);
  });
}

function engineTitle(engine) {
  if (engine === "baseline_single_thread") {
    return "Baseline Engine";
  }
  if (engine === "optimized_single_thread") {
    return "Optimized Engine";
  }
  return "Concurrent Pipeline";
}

function engineNarrative(summary) {
  if (!summary) {
    return "";
  }
  if (summary.engine === "baseline_single_thread") {
    return "Reference implementation. Keeps the rules obvious and gives a real before-optimization benchmark.";
  }
  if (summary.engine === "optimized_single_thread") {
    return "Same matching behavior, less matcher work. Direct lookup removes cancel scans and improves service time.";
  }
  return "Same single matcher, but with queues around it. The useful comparison is service time versus queue-driven end-to-end delay.";
}

function engineHighlights(summary) {
  if (!summary) {
    return [];
  }
  return [
    ["Throughput", `${formatNumber(summary.throughput_ops_per_sec)} ops/s`],
    ["Service p50", `${formatNumber(summary.service_p50_ns)} ns`],
    ["Service p99", `${formatNumber(summary.service_p99_ns)} ns`],
    ["End-to-end p50", `${formatNumber(summary.end_to_end_p50_ns)} ns`],
    ["End-to-end p99", `${formatNumber(summary.end_to_end_p99_ns)} ns`],
    ["Queue p50", `${formatNumber(summary.queue_delay_p50_ns)} ns`],
    ["Queue p99", `${formatNumber(summary.queue_delay_p99_ns)} ns`],
    ["Queue depth", formatNumber(summary.max_queue_depth)],
    ["Fills", formatNumber(summary.total_fills)],
    ["Rejects", formatNumber(summary.total_rejects)],
  ];
}

function renderMiniBars(summary, rows) {
  const maxValue = Math.max(1, ...rows.map(([, key]) => numericValue(summary?.[key])));
  return `
    <div class="mini-bars">
      ${rows
        .map(([label, key, suffix = ""]) => {
          const value = numericValue(summary?.[key]);
          const width = `${Math.max(6, Math.round((value / maxValue) * 100))}%`;
          return `
            <div class="mini-bar-row">
              <span>${label}</span>
              <div class="mini-bar-track"><div class="mini-bar-fill" style="width:${width}"></div></div>
              <strong>${formatNumber(value)}${suffix}</strong>
            </div>
          `;
        })
        .join("")}
    </div>
  `;
}

function renderComparisonChart(summaries, key, title, suffix = "") {
  const filtered = summaries.filter(Boolean);
  if (!filtered.length) {
    return "";
  }
  const maxValue = Math.max(1, ...filtered.map((summary) => numericValue(summary[key])));
  return `
    <section class="chart-block">
      <div class="chart-header">
        <h3>${title}</h3>
      </div>
      <div class="chart-stack">
        ${filtered
          .map((summary) => {
            const value = numericValue(summary[key]);
            const width = `${Math.max(8, Math.round((value / maxValue) * 100))}%`;
            return `
              <div class="chart-row">
                <span class="chart-label">${engineTitle(summary.engine)}</span>
                <div class="chart-track"><div class="chart-fill" style="width:${width}"></div></div>
                <strong>${formatNumber(value)}${suffix}</strong>
              </div>
            `;
          })
          .join("")}
      </div>
    </section>
  `;
}

function renderArchitectureCard(summary) {
  if (!summary) {
    return "";
  }

  let stages = `
    <div class="flow-track">
      <div class="flow-node">Feed</div>
      <div class="flow-link"></div>
      <div class="flow-node core">Matcher</div>
      <div class="flow-link"></div>
      <div class="flow-node">Book</div>
    </div>
  `;
  let copy = "Single-thread path. Every event enters the matcher directly, then updates the book.";

  if (summary.engine === "optimized_single_thread") {
    stages = `
      <div class="flow-track">
        <div class="flow-node">Feed</div>
        <div class="flow-link"></div>
        <div class="flow-node core good">Matcher</div>
        <div class="flow-link"></div>
        <div class="flow-node">Book</div>
        <div class="flow-link faint"></div>
        <div class="flow-node accent">Order Map</div>
        <div class="flow-node accent">Cached Levels</div>
      </div>
    `;
    copy = "Same one-thread matcher, but cancel lookups and level totals are pre-indexed so less work happens per event.";
  } else if (summary.engine === "optimized_concurrent_pipeline") {
    stages = `
      <div class="flow-track">
        <div class="flow-node">Ingress</div>
        <div class="flow-link"></div>
        <div class="flow-node queue">Queue</div>
        <div class="flow-link"></div>
        <div class="flow-node core">Matcher</div>
        <div class="flow-link"></div>
        <div class="flow-node queue">Queue</div>
        <div class="flow-link"></div>
        <div class="flow-node">Egress</div>
      </div>
    `;
    copy = "The matcher itself stays fast, but work can pile up in the handoff queues. Good service time can still hide bad end-to-end latency.";
  }

  return `
    <article class="system-card">
      <div class="panel-header">
        <h3>${engineTitle(summary.engine)}</h3>
        <span class="ghost-pill">${formatNumber(summary.throughput_ops_per_sec)} ops/s</span>
      </div>
      <p class="system-kicker">Path of work</p>
      ${stages}
      <p class="engine-copy">${copy}</p>
      ${renderMiniBars(summary, [
        ["Service p99", "service_p99_ns", " ns"],
        ["E2E p99", "end_to_end_p99_ns", " ns"],
        ["Queue p99", "queue_delay_p99_ns", " ns"],
      ])}
    </article>
  `;
}

function renderMetrics() {
  const baseline = state.metrics.find((metric) => metric.engine === "baseline_single_thread");
  const optimized = state.metrics.find((metric) => metric.engine === "optimized_single_thread");
  const pipeline = state.metrics.find((metric) => metric.engine === "optimized_concurrent_pipeline");
  renderOverviewPanel({ baseline, optimized, pipeline });
  if (analysisPanel) {
    renderAnalysisPanel({ baseline, optimized, pipeline });
  }
}

function stageLabel(engine) {
  if (engine === "optimized_single_thread") {
    return "Matcher only";
  }
  if (engine === "replay_matcher") {
    return "Replay + matcher";
  }
  if (engine === "replay_features") {
    return "Replay + features";
  }
  if (engine === "replay_features_heuristic") {
    return "Replay + features + heuristic";
  }
  if (engine === "replay_features_logistic") {
    return "Replay + features + logistic";
  }
  if (engine === "replay_features_inference") {
    return "Replay + features + inference";
  }
  return engine;
}

function chooseBestReport(reports) {
  if (!reports?.length) {
    return null;
  }
  return reports.reduce(
    (best, report) => (
      report.macro_f1 > best.macro_f1
      || (report.macro_f1 === best.macro_f1 && report.accuracy > best.accuracy)
    ) ? report : best,
    reports[0],
  );
}

function featureSetCopy(featureSet) {
  if (featureSet === "pressure_core") {
    return "Pressure-heavy microstructure features: imbalance, spread, microprice, and flow.";
  }
  if (featureSet === "book_dynamics") {
    return "A wider book-state set including shape, depletion, and short-term dynamics.";
  }
  if (featureSet === "all_no_mid") {
    return "All engineered features except raw mid price to reduce absolute-price dependence.";
  }
  return "Replay-derived order-book state features.";
}

function renderOverviewVisual(step, deployable, replayInference) {
  if (!step) {
    return `
      <div class="hero-visual">
        <div class="hero-screen hero-screen-empty">
          <p>Replay preview unavailable.</p>
        </div>
      </div>
    `;
  }

  const asks = [...(step.depth?.asks ?? [])].slice(0, 4);
  const bids = [...(step.depth?.bids ?? [])].slice(0, 4);
  const allQty = [...asks, ...bids].map((level) => Number(level.qty));
  const maxQty = Math.max(1, ...allQty);
  const renderLevel = (level, side) => `
    <div class="hero-level ${side}">
      <span>${formatNumber(level.qty)}</span>
      <span>${formatNumber(level.price)}</span>
      <div class="hero-level-bar"><div style="width:${Math.max(10, Math.round((Number(level.qty) / maxQty) * 100))}%"></div></div>
    </div>
  `;

  return `
    <div class="hero-visual">
      <div class="hero-orb hero-orb-a"></div>
      <div class="hero-orb hero-orb-b"></div>
      <div class="hero-screen">
        <div class="hero-screen-top">
          <span class="hero-screen-label">${activeDatasetLabel()} replay</span>
          <span class="hero-screen-mode">${activeReplayLabel()}</span>
        </div>
        <div class="hero-price-band">
          <div class="hero-price-card buy">
            <span>Best bid</span>
            <strong>${formatNumber(step.top.best_bid)}</strong>
          </div>
          <div class="hero-price-center">
            <span>Mid</span>
            <strong>${step.top.best_bid !== null && step.top.best_ask !== null ? formatNumber((Number(step.top.best_bid) + Number(step.top.best_ask)) / 2, 2) : "-"}</strong>
          </div>
          <div class="hero-price-card sell">
            <span>Best ask</span>
            <strong>${formatNumber(step.top.best_ask)}</strong>
          </div>
        </div>
        <div class="hero-ladder">
          <div class="hero-ladder-side">
            <div class="hero-ladder-head">
              <span>Ask size</span>
              <span>Ask price</span>
            </div>
            ${asks.map((level) => renderLevel(level, "sell")).join("")}
          </div>
          <div class="hero-ladder-divider"></div>
          <div class="hero-ladder-side">
            <div class="hero-ladder-head">
              <span>Bid size</span>
              <span>Bid price</span>
            </div>
            ${bids.map((level) => renderLevel(level, "buy")).join("")}
          </div>
        </div>
        <div class="hero-bottom">
          <div class="hero-tag">
            <span>Deployable model</span>
            <strong>${deployable?.name ?? "-"}</strong>
          </div>
          <div class="hero-tag">
            <span>Stage throughput</span>
            <strong>${replayInference ? `${formatNumber(replayInference.throughput_ops_per_sec)} ops/s` : "-"}</strong>
          </div>
          <div class="hero-tag">
            <span>Current event</span>
            <strong>${step.event.type} ${step.event.side}</strong>
          </div>
        </div>
      </div>
    </div>
  `;
}

function renderOverviewPanel({ baseline, optimized, pipeline }) {
  if (!overviewPanel) {
    return;
  }

  const stageMetrics = state.analysis.stageMetrics ?? [];
  const evaluation = state.analysis.evaluation;
  const reports = evaluation?.reports ?? [];
  const bestModel = chooseBestReport(reports);
  const deployable = reports.find((report) => report.name === evaluation?.deployable_recommendation) ?? bestModel;
  const replayInference = stageMetrics.find((row) => row.engine === "replay_features_heuristic")
    ?? stageMetrics.find((row) => row.engine === "replay_features_logistic")
    ?? stageMetrics.find((row) => row.engine === "replay_features_inference");
  const heroStep = state.replay?.steps?.[Math.min(state.replay.steps.length - 1, Math.max(0, Math.floor((state.replay.steps.length - 1) * 0.62)))];

  if (!optimized) {
    overviewPanel.innerHTML = `
      <article class="analysis-card">
        <h2>Overview unavailable</h2>
        <p class="engine-copy">Load replay, benchmark, and evaluation artifacts to render the full project overview.</p>
      </article>
    `;
    return;
  }

  const overviewStats = [
    ["Matcher core", `${formatNumber(optimized.throughput_ops_per_sec)} ops/s`, "Optimized single-thread throughput"],
    ["Replay + inference", replayInference ? `${formatNumber(replayInference.throughput_ops_per_sec)} ops/s` : "-", "Throughput once the matcher is embedded in the full pipeline"],
    ["Best deployable model", evaluation?.deployable_recommendation ?? "-", deployable ? `Accuracy ${deployable.accuracy.toFixed(4)} · Macro F1 ${deployable.macro_f1.toFixed(4)}` : "No model evaluation loaded"],
    ["Historical source", activeDatasetLabel(), "Deterministic replay over normalized order-flow events"],
  ];

  overviewPanel.innerHTML = `
    <div class="overview-shell">
      <section class="overview-hero">
        <div class="overview-copy">
          <p class="eyebrow">${activeDatasetLabel()} market replay</p>
          <h2>Low-latency market replay, order-book intelligence, and deployable prediction in one system.</h2>
          <p class="hero-copy">LOB Terminal reconstructs a real order-driven market from historical events, tracks the live book at each step, derives microstructure features, and compares predictive quality against online cost.</p>
          <div class="hero-action-row">
            <span class="hero-badge">Deterministic replay</span>
            <span class="hero-badge">Low-latency matcher</span>
            <span class="hero-badge">Integrated inference</span>
          </div>
        </div>
        <div class="overview-hero-stack">
          ${renderOverviewVisual(heroStep, deployable, replayInference)}
          <div class="overview-stat-grid">
            ${overviewStats.map(([label, value, detail]) => `
              <div class="overview-stat">
                <span>${label}</span>
                <strong>${value}</strong>
                <p>${detail}</p>
              </div>
            `).join("")}
          </div>
        </div>
      </section>

      <section class="overview-section pipeline-section">
        <div class="section-head">
          <h3>System Pipeline</h3>
          <span class="ghost-pill">Replay to inference</span>
        </div>
        <p class="section-copy">Each market event moves through a deterministic path: ingest, match, featurize, score, and evaluate. The same sequence drives the simulator, the benchmark surfaces, and the model analysis.</p>
        <div class="pipeline-strip">
          <div class="pipeline-step">
            <span class="pipeline-index">01</span>
            <h4>Historical Events</h4>
            <p>Normalized LOBSTER message data is replayed in exact historical order.</p>
          </div>
          <div class="pipeline-strip-link"></div>
          <div class="pipeline-step">
            <span class="pipeline-index">02</span>
            <h4>Optimized Matcher</h4>
            <p>The single-book order book applies price-time priority and updates the live ladder.</p>
          </div>
          <div class="pipeline-strip-link"></div>
          <div class="pipeline-step">
            <span class="pipeline-index">03</span>
            <h4>Feature Extractor</h4>
            <p>Spread, microprice, imbalance, depletion, and flow features are computed after each event.</p>
          </div>
          <div class="pipeline-strip-link"></div>
          <div class="pipeline-step">
            <span class="pipeline-index">04</span>
            <h4>Predictor</h4>
            <p>Heuristic, logistic regression, and offline XGBoost baselines compete on the same target.</p>
          </div>
          <div class="pipeline-strip-link"></div>
          <div class="pipeline-step">
            <span class="pipeline-index">05</span>
            <h4>Evaluation</h4>
            <p>Offline quality and online replay latency stay separate so deployability stays honest.</p>
          </div>
        </div>
      </section>

      <section class="overview-section">
        <div class="section-head">
          <h3>Core Surfaces</h3>
          <span class="ghost-pill">Execution stack</span>
        </div>
        <div class="story-grid">
          <article class="story-block">
            <h4>Reference Engine</h4>
            <p>A transparent single-book implementation that anchors correctness, replay validation, and before/after latency measurements.</p>
            <strong>${baseline ? `${formatNumber(baseline.throughput_ops_per_sec)} ops/s` : "-"}</strong>
          </article>
          <article class="story-block">
            <h4>Optimized Matcher</h4>
            <p>The production path: same market rules, less work per event through direct lookup, cached depth, and pooled allocation.</p>
            <strong>${formatNumber(optimized.throughput_ops_per_sec)} ops/s</strong>
          </article>
          <article class="story-block">
            <h4>Inference Layer</h4>
            <p>A replay-driven feature and scoring layer that distinguishes offline model quality from live deployable latency.</p>
            <strong>${evaluation?.deployable_recommendation ?? "-"}</strong>
          </article>
        </div>
      </section>

      <section class="overview-section overview-two-column">
        <article class="focus-panel">
          <div class="section-head">
            <h3>Prediction Target</h3>
            <span class="ghost-pill">${evaluation?.target_name ?? "target"}</span>
          </div>
          <p class="section-copy">${evaluation?.label_mode === "thresholded_horizon"
            ? `The scoring target looks ${evaluation?.horizon_events ?? "-"} events ahead and ignores moves smaller than ${evaluation?.move_threshold ?? "-"} price units, keeping the label focused on meaningful short-horizon direction.`
            : "The scoring target predicts short-horizon direction directly from the present order-book state."}</p>
          <div class="focus-list">
            <div class="focus-row"><span>Deployment path</span><strong>${evaluation?.deployable_recommendation ?? "-"}</strong></div>
            <div class="focus-row"><span>Best offline model</span><strong>${evaluation?.best_model ?? "-"}</strong></div>
            <div class="focus-row"><span>Feature set</span><strong>${deployable?.feature_set ?? "-"}</strong></div>
            <div class="focus-row"><span>Feature profile</span><strong>${featureSetCopy(deployable?.feature_set)}</strong></div>
          </div>
        </article>
        <article class="focus-panel">
          <div class="section-head">
            <h3>Key Signals</h3>
            <span class="ghost-pill">Operational readout</span>
          </div>
          <div class="notes-grid compact-notes">
            <div class="note-block">
              <h3>Execution</h3>
              <p>The optimized single-thread matcher remains the primary latency win. Replay, feature extraction, and scoring add measurable overhead above that execution core.</p>
            </div>
            <div class="note-block">
              <h3>Prediction</h3>
              <p>The best offline model and the best deployable model are not necessarily the same. In this run, the heuristic remains the strongest cost-to-quality choice.</p>
            </div>
            <div class="note-block">
              <h3>Measurement</h3>
              <p>Offline predictive quality is kept separate from live replay latency, so model selection and systems performance stay independently interpretable.</p>
            </div>
          </div>
        </article>
      </section>
    </div>
  `;
}

function markdownToHtml(markdown) {
  const lines = markdown.split("\n");
  let html = "";
  let inList = false;

  for (const rawLine of lines) {
    const line = rawLine.trimEnd();
    const trimmed = line.trim();

    if (!trimmed) {
      if (inList) {
        html += "</ul>";
        inList = false;
      }
      continue;
    }

    if (trimmed.startsWith("### ")) {
      if (inList) {
        html += "</ul>";
        inList = false;
      }
      html += `<h3>${escapeHtml(trimmed.slice(4))}</h3>`;
      continue;
    }

    if (trimmed.startsWith("## ")) {
      if (inList) {
        html += "</ul>";
        inList = false;
      }
      html += `<h2>${escapeHtml(trimmed.slice(3))}</h2>`;
      continue;
    }

    if (trimmed.startsWith("# ")) {
      if (inList) {
        html += "</ul>";
        inList = false;
      }
      html += `<h1>${escapeHtml(trimmed.slice(2))}</h1>`;
      continue;
    }

    if (trimmed.startsWith("- ")) {
      if (!inList) {
        html += "<ul>";
        inList = true;
      }
      html += `<li>${escapeHtml(trimmed.slice(2))}</li>`;
      continue;
    }

    if (inList) {
      html += "</ul>";
      inList = false;
    }
    html += `<p>${escapeHtml(trimmed)}</p>`;
  }

  if (inList) {
    html += "</ul>";
  }

  return html;
}

function renderReportPanel() {
  if (!reportPanel) {
    return;
  }

  reportPanel.innerHTML = state.reportMarkdown
    ? `
      <article class="report-card">
        <div class="panel-header">
          <h2>Research Report</h2>
          <span class="ghost-pill">${activeDatasetLabel()}</span>
        </div>
        <div class="report-prose">
          ${markdownToHtml(state.reportMarkdown)}
        </div>
      </article>
    `
    : `
      <article class="report-card">
        <h2>Report unavailable</h2>
        <p class="engine-copy">The frontend report markdown could not be loaded.</p>
      </article>
    `;
}

function metricValue(summary, key, suffix) {
  if (!summary) {
    return "-";
  }
  return `${formatNumber(summary[key])}${suffix}`;
}

function percentDelta(baseValue, nextValue) {
  const base = Number(baseValue);
  const next = Number(nextValue);
  if (!Number.isFinite(base) || !Number.isFinite(next) || base === 0) {
    return null;
  }
  return ((next - base) / base) * 100;
}

function renderDeltaChip(delta, betterWhenLower = false) {
  if (delta === null) {
    return `<span class="delta-chip neutral">-</span>`;
  }
  const isGood = betterWhenLower ? delta < 0 : delta > 0;
  const direction = delta > 0 ? "+" : "";
  return `<span class="delta-chip ${isGood ? "good" : "bad"}">${direction}${delta.toFixed(1)}%</span>`;
}

function renderMetricMatrix(summary) {
  if (!summary) {
    return "";
  }
  return engineHighlights(summary)
    .map(([label, value]) => `<div class="matrix-row"><span>${label}</span><strong>${value}</strong></div>`)
    .join("");
}

function renderSimpleMetricsTable(rows, columns) {
  return `
    <div class="comparison-table analysis-table">
      <div class="comparison-row head">
        ${columns.map((column) => `<span>${column.label}</span>`).join("")}
      </div>
      ${rows
        .map(
          (row) => `
            <div class="comparison-row">
              ${columns.map((column) => `<span>${column.render(row)}</span>`).join("")}
            </div>
          `
        )
        .join("")}
    </div>
  `;
}

function renderConfusionMatrix(report) {
  if (!report?.confusion_matrix?.length) {
    return "";
  }
  const labels = report.labels ?? ["down", "flat", "up"];
  const gridStyle = `style="grid-template-columns: 1.2fr repeat(${labels.length}, minmax(0, 1fr));"`;
  const head = `<div class="confusion-grid header" ${gridStyle}><span>actual \\ pred</span>${labels.map((label) => `<span>${label}</span>`).join("")}</div>`;
  const body = report.confusion_matrix
    .map(
      (row, index) => `
        <div class="confusion-grid" ${gridStyle}>
          <span>${labels[index]}</span>
          ${row.map((value) => `<strong>${formatNumber(value)}</strong>`).join("")}
        </div>
      `
    )
    .join("");
  return `<div class="confusion-matrix">${head}${body}</div>`;
}

function renderAnalysisPanel({ baseline, optimized, pipeline }) {
  if (!analysisPanel) {
    return;
  }
  const hasAnySummary = Boolean(baseline || optimized || pipeline);
  const optimizationDelta = percentDelta(baseline?.throughput_ops_per_sec, optimized?.throughput_ops_per_sec);
  const queueDelta = percentDelta(optimized?.end_to_end_p99_ns, pipeline?.end_to_end_p99_ns);
  const flamegraphBlock = state.analysis.flamegraphPath
    ? `
      <div class="artifact-frame">
        <img src="${state.analysis.flamegraphPath}" alt="Flame graph artifact" />
      </div>
    `
    : `
      <div class="artifact-empty">
        <h3>No Flame Graph Yet</h3>
        <p>Drop an SVG named after this dataset into results and it will appear here automatically.</p>
      </div>
    `;
  const perfBlock = state.analysis.perfPath
    ? `<a class="artifact-link" href="${state.analysis.perfPath}" target="_blank" rel="noreferrer">Open profile artifact</a>`
    : `<span class="artifact-link disabled">No profile artifact detected</span>`;
  const reportLines = state.analysis.comparisonMarkdown
    .split("\n")
    .map((line) => line.trim())
    .filter((line) => line.startsWith("- "))
    .slice(0, 6)
    .map((line) => `<li>${line.slice(2)}</li>`)
    .join("");
  const stageMetrics = state.analysis.stageMetrics ?? [];
  const stageBenchmarkRows = stageMetrics.map((row) => ({ ...row, stage_label: stageLabel(row.engine) }));
  const summaryRows = [
    ["Baseline", baseline],
    ["Optimized", optimized],
    ["Pipeline", pipeline],
  ]
    .filter(([, summary]) => summary)
    .map(
      ([label, summary]) => `
        <div class="comparison-row">
          <span>${label}</span>
          <span>${metricValue(summary, "throughput_ops_per_sec", " ops/s")}</span>
          <span>${metricValue(summary, "service_p99_ns", " ns")}</span>
          <span>${metricValue(summary, "end_to_end_p99_ns", " ns")}</span>
        </div>
      `
    )
    .join("");

  analysisPanel.innerHTML = hasAnySummary ? `
    <div class="analysis-shell">
      <section class="analysis-ribbon">
        <div>
          <p class="eyebrow">${activeDatasetLabel()} performance</p>
          <h2>Performance</h2>
        </div>
        <div class="analysis-deltas">
          <div class="delta-panel">
            <span class="analysis-label">Optimized vs Baseline</span>
            ${renderDeltaChip(optimizationDelta)}
            <p>Direct lookup, cached levels, and pooled allocators reduced matcher work.</p>
          </div>
          <div class="delta-panel">
            <span class="analysis-label">Pipeline E2E vs Optimized</span>
            ${renderDeltaChip(queueDelta)}
            <p>The matcher stayed fast, but queueing inflated tail latency under burst load.</p>
          </div>
        </div>
      </section>

      <section class="analysis-section">
        <div class="section-head">
          <h3>Execution Paths</h3>
          <span class="ghost-pill">Where work happens</span>
        </div>
        <p class="section-copy">Trace the event path through execution, replay, feature extraction, and scoring to see where latency is introduced and where throughput compresses.</p>
        <div class="system-map">
          ${[baseline, optimized, pipeline].filter(Boolean).map((summary) => renderArchitectureCard(summary)).join("")}
        </div>
      </section>

      <section class="analysis-section">
        <div class="section-head">
          <h3>Core Engine Readout</h3>
          <span class="ghost-pill">Core metrics</span>
        </div>
        <div class="comparison-table analysis-table">
          <div class="comparison-row head">
            <span>Engine</span>
            <span>Throughput</span>
            <span>Service p99</span>
            <span>E2E p99</span>
          </div>
          ${summaryRows}
        </div>
        <div class="analysis-charts">
          ${renderComparisonChart([baseline, optimized, pipeline], "throughput_ops_per_sec", "Throughput", " ops/s")}
          ${renderComparisonChart([baseline, optimized, pipeline], "service_p99_ns", "Service tail", " ns")}
          ${renderComparisonChart([baseline, optimized, pipeline], "queue_delay_p99_ns", "Queue delay tail", " ns")}
        </div>
      </section>

      ${
        stageBenchmarkRows.length
          ? `
      <section class="analysis-section">
        <div class="section-head">
          <h3>Replay Pipeline Cost</h3>
          <span class="ghost-pill">Matcher vs features vs inference</span>
        </div>
        <p class="section-copy">This is the main V2 systems result. It shows how much each extra stage costs once the matcher sits inside the replay-and-inference flow.</p>
        ${renderSimpleMetricsTable(stageBenchmarkRows, [
          { label: "Stage", render: (row) => row.stage_label },
          { label: "Throughput", render: (row) => `${formatNumber(row.throughput_ops_per_sec)} ops/s` },
          { label: "Service p99", render: (row) => `${formatNumber(row.service_p99_ns)} ns` },
          { label: "E2E p99", render: (row) => `${formatNumber(row.end_to_end_p99_ns)} ns` },
        ])}
        <div class="analysis-charts">
          ${renderComparisonChart(stageBenchmarkRows, "throughput_ops_per_sec", "Stage throughput", " ops/s")}
          ${renderComparisonChart(stageBenchmarkRows, "end_to_end_p99_ns", "Stage end-to-end p99", " ns")}
        </div>
      </section>
      `
          : ""
      }

      <details class="analysis-section detail-disclosure">
        <summary class="detail-summary">
          <span>Open per-engine metric detail</span>
          <span class="ghost-pill">Expanded view</span>
        </summary>
        <div class="detail-body analysis-matrix">
        <div class="section-head">
          <h3>Per Engine Detail</h3>
          <span class="ghost-pill">Measured outputs</span>
        </div>
        <div class="matrix-columns">
          ${baseline ? `
            <div class="matrix-column">
              <div class="matrix-head">
                <h4>Baseline</h4>
                <p>${engineNarrative(baseline)}</p>
              </div>
              <div class="metric-matrix">${renderMetricMatrix(baseline)}</div>
            </div>
          ` : ""}
          ${optimized ? `
            <div class="matrix-column">
              <div class="matrix-head">
                <h4>Optimized</h4>
                <p>${engineNarrative(optimized)}</p>
              </div>
              <div class="metric-matrix">${renderMetricMatrix(optimized)}</div>
            </div>
          ` : ""}
          ${pipeline ? `
            <div class="matrix-column">
              <div class="matrix-head">
                <h4>Pipeline</h4>
                <p>${engineNarrative(pipeline)}</p>
              </div>
              <div class="metric-matrix">${renderMetricMatrix(pipeline)}</div>
            </div>
          ` : ""}
        </div>
        </div>
      </details>

      <section class="analysis-section analysis-bottom">
        <div class="notes-panel">
          <div class="section-head">
            <h3>Operational Notes</h3>
            <span class="ghost-pill">What matters</span>
          </div>
          <div class="notes-grid">
            <div class="note-block">
              <h3>Matcher</h3>
              <p>${optimized ? "Use the optimized service metrics to judge the execution core itself." : "Load a run to inspect matcher service time."}</p>
            </div>
            <div class="note-block">
              <h3>Queueing</h3>
              <p>${pipeline ? "If service time stays low while end-to-end explodes, backlog rather than matching is the system bottleneck." : "Pipeline stats will appear here when that mode is present."}</p>
            </div>
            <div class="note-block">
              <h3>Execution model</h3>
              <p>The design keeps one matching thread per book. The concurrency question here is transport and saturation, not shared mutation of one live book.</p>
            </div>
          </div>
          ${reportLines ? `<details class="report-block report-disclosure"><summary>Open run notes</summary><ul class="report-list">${reportLines}</ul></details>` : ""}
        </div>

        <div class="artifact-panel">
          <div class="section-head">
            <h3>Profile + Flame Graph</h3>
            <span class="ghost-pill">Artifacts</span>
          </div>
          <div class="artifact-toolbar">
            ${perfBlock}
          </div>
          ${flamegraphBlock}
        </div>
      </section>

    </div>
  ` : `
    <article class="analysis-card">
      <h2>No analysis data</h2>
      <p class="engine-copy">The run loaded replay data, but no benchmark summaries were available for analysis. Regenerate the results CSV and reload.</p>
    </article>
  `;
}

function renderModelsPanel() {
  if (!modelsPanel) {
    return;
  }

  const evaluation = state.analysis.evaluation;
  const reports = evaluation?.reports ?? [];
  const bestModel = chooseBestReport(reports);
  const targetLabel = evaluation?.target_name
    ? evaluation.target_name.replaceAll("_", " ")
    : (evaluation?.task === "binary" ? "next direction" : "mid-price move");
  const targetCopy = evaluation?.target_name === "next_non_zero"
    ? "Predict the direction of the next non-zero mid-price change. This strips out many noisy no-move states."
    : evaluation?.label_mode === "thresholded_horizon"
      ? `Predict whether the mid-price is up or down after ${evaluation?.horizon_events ?? "-"} events once moves smaller than ${evaluation?.move_threshold ?? "-"} are treated as flat.`
      : evaluation?.task === "binary"
        ? `Binary short-horizon prediction over ${evaluation?.horizon_events ?? "-"} events after filtering flat rows: will the next move be down or up?`
        : "Short-horizon mid-price move from the current order-book state.";

  modelsPanel.innerHTML = reports.length
    ? `
      <div class="analysis-shell">
        <section class="analysis-ribbon">
          <div>
            <p class="eyebrow">${activeDatasetLabel()} model surface</p>
            <h2>Prediction Quality</h2>
          </div>
          <div class="analysis-deltas model-deltas">
            <div class="delta-panel">
              <span class="analysis-label">Best offline model</span>
              <strong class="model-chip">${bestModel?.name ?? "-"}</strong>
              <p>Accuracy ${bestModel ? bestModel.accuracy.toFixed(4) : "-"} · Macro F1 ${bestModel ? bestModel.macro_f1.toFixed(4) : "-"}</p>
            </div>
            <div class="delta-panel">
              <span class="analysis-label">Deployment path</span>
              <strong class="model-chip">${evaluation?.deployable_recommendation ?? "-"}</strong>
              <p>${evaluation?.best_model && evaluation?.best_model !== evaluation?.deployable_recommendation ? "Offline model leadership and low-latency deployment are separated by design." : "The top-scoring model is also the preferred deployment path."}</p>
            </div>
            <div class="delta-panel">
              <span class="analysis-label">Prediction target</span>
              <strong class="model-chip">${targetLabel}</strong>
              <p>${targetCopy}</p>
            </div>
          </div>
        </section>

        <section class="analysis-section">
          <div class="section-head">
            <h3>Model Scoreboard</h3>
            <span class="ghost-pill">Offline evaluation</span>
          </div>
          <p class="section-copy">This table is the primary model view: predictive quality, feature family, and scoring cost in one read. Detailed error breakdowns remain available below on demand.</p>
          ${renderSimpleMetricsTable(reports, [
            { label: "Model", render: (row) => row.name },
            { label: "Feature set", render: (row) => row.feature_set ?? "-" },
            { label: "Accuracy", render: (row) => row.accuracy.toFixed(4) },
            { label: "Macro F1", render: (row) => row.macro_f1.toFixed(4) },
            { label: "p50 infer (ns)", render: (row) => formatNumber(row.inference_p50_ns) },
            { label: "p99 infer (ns)", render: (row) => formatNumber(row.inference_p99_ns) },
          ])}
        </section>

        <details class="analysis-section detail-disclosure">
          <summary class="detail-summary">
            <span>Open confusion matrices</span>
            <span class="ghost-pill">Detailed error breakdown</span>
          </summary>
          <div class="detail-body">
          <div class="section-head">
            <h3>Confusion Matrices</h3>
            <span class="ghost-pill">Prediction breakdown</span>
          </div>
          <div class="model-grid">
            ${reports
              .map(
                (report) => `
                  <article class="model-card">
                    <div class="panel-header">
                      <h3>${report.name}</h3>
                      <span class="ghost-pill">${(report.accuracy * 100).toFixed(1)}%</span>
                    </div>
                    <p class="engine-copy">${report.feature_set ? `Feature set ${report.feature_set}` : "No feature set"}</p>
                    <div class="model-metric-grid">
                      <div class="model-metric">
                        <span class="model-metric-label">Accuracy</span>
                        <strong>${report.accuracy.toFixed(4)}</strong>
                      </div>
                      <div class="model-metric">
                        <span class="model-metric-label">Macro F1</span>
                        <strong>${report.macro_f1.toFixed(4)}</strong>
                      </div>
                      <div class="model-metric">
                        <span class="model-metric-label">p50 Latency</span>
                        <strong>${formatNumber(report.inference_p50_ns)} ns</strong>
                      </div>
                      <div class="model-metric">
                        <span class="model-metric-label">p99 Latency</span>
                        <strong>${formatNumber(report.inference_p99_ns)} ns</strong>
                      </div>
                    </div>
                    ${renderConfusionMatrix(report)}
                  </article>
                `
              )
              .join("")}
          </div>
          </div>
        </details>

        <section class="analysis-section">
          <div class="section-head">
            <h3>Model Notes</h3>
            <span class="ghost-pill">Takeaway</span>
          </div>
          <div class="notes-grid">
            <div class="note-block">
              <h3>Target</h3>
              <p>${evaluation?.target_name === "next_non_zero" ? "The next non-zero move target removes dead periods and often produces a cleaner directional label." : evaluation?.label_mode === "thresholded_horizon" ? "The thresholded target removes tiny moves so the label focuses on more meaningful directional changes." : evaluation?.task === "binary" ? "Binary direction keeps the target compact and easier to interpret." : "The active target measures short-horizon mid-price movement."}</p>
            </div>
            <div class="note-block">
              <h3>Ranking</h3>
              <p>${bestModel?.name === "heuristic_imbalance" ? "The heuristic currently leads, suggesting label definition and feature quality matter more than extra model complexity in this setting." : "A learned model currently leads on held-out quality."}</p>
            </div>
            <div class="note-block">
              <h3>Evaluation design</h3>
              <p>These results come from a time-ordered train/validation/test split with fixed feature-set candidates and consistent target definitions.</p>
            </div>
            <div class="note-block">
              <h3>Latency readout</h3>
              <p>These latency figures measure per-example offline scoring cost. Use the Performance tab for replay-stage overhead inside the integrated C++ path.</p>
            </div>
            <div class="note-block">
              <h3>Selection logic</h3>
              <p>Use this page to compare model quality and scoring cost together, then validate the chosen deployment path on the replay benchmark in Performance.</p>
            </div>
          </div>
        </section>
      </div>
    `
    : `
      <article class="analysis-card">
        <h2>No model analysis data</h2>
        <p class="engine-copy">Run feature export, model evaluation, and write the JSON artifact before opening this tab.</p>
      </article>
    `;
}

function renderDetailGrid(container, items) {
  container.innerHTML = items
    .map(
      ([label, value]) => `
        <div class="detail-item">
          <span class="detail-label">${label}</span>
          <span class="detail-value">${safeText(value)}</span>
        </div>
      `
    )
    .join("");
}

function pulseMatcher() {
  matcherPulse.classList.remove("active");
  void matcherPulse.offsetWidth;
  matcherPulse.classList.add("active");
}

function renderPipelineState(step) {
  const totalLevels = step.depth.asks.length + step.depth.bids.length;
  const stepProgress = state.replay.steps.length > 1 ? state.currentIndex / (state.replay.steps.length - 1) : 0;
  const queueLoad = Math.min(100, Math.round((totalLevels / Math.max(1, state.replay.depth_levels * 2)) * 100));
  const resultIntensity = step.result.executions.length > 0 ? Math.min(100, step.result.executions.length * 24) : 12;

  ingressFill.style.width = `${Math.max(queueLoad, 8)}%`;
  egressFill.style.width = `${Math.max(resultIntensity, Math.round(stepProgress * 100 * 0.45))}%`;
  const ingressPreview = state.replay.steps.slice(state.currentIndex + 1, state.currentIndex + 6);
  const egressPreview = step.result.executions.slice(0, 5);
  ingressQueue.innerHTML = ingressPreview
    .map(
      (entry) => `
        <span class="queue-token ${entry.event.side === "BUY" ? "buy" : "sell"}">
          ${entry.event.type[0]}${entry.sequence}
        </span>
      `
    )
    .join("");
  egressQueue.innerHTML = egressPreview.length
    ? egressPreview
        .map(
          (fill) => `
            <span class="queue-token ${fill.aggressive_is_buy ? "buy" : "sell"}">
              F${fill.resting_order_id}
            </span>
          `
        )
        .join("")
    : `<span class="queue-token idle">Idle</span>`;
  matcherActive.textContent = `${step.event.type} ${step.event.side} · #${step.sequence}`;
  pulseMatcher();
}

function renderPressure(asks, bids) {
  const askTotal = asks.reduce((sum, level) => sum + Number(level.qty), 0);
  const bidTotal = bids.reduce((sum, level) => sum + Number(level.qty), 0);
  const total = Math.max(1, askTotal + bidTotal);
  const bidPct = (bidTotal / total) * 100;
  const askPct = (askTotal / total) * 100;

  buyPressure.textContent = `${bidPct.toFixed(2)}%`;
  sellPressure.textContent = `${askPct.toFixed(2)}%`;
  buyPressureBar.style.width = `${bidPct}%`;
  sellPressureBar.style.width = `${askPct}%`;
}

function renderUnifiedBook(asks, bids) {
  const askLevels = [...asks].reverse();
  const bidLevels = [...bids];
  const maxRows = Math.max(askLevels.length, bidLevels.length, 8);
  const maxQty = Math.max(
    1,
    ...askLevels.map((level) => Number(level.qty)),
    ...bidLevels.map((level) => Number(level.qty))
  );
  const matchedAskPrices = new Set();
  const matchedBidPrices = new Set();
  if (state.replay?.steps?.[state.currentIndex]) {
    const step = state.replay.steps[state.currentIndex];
    step.result.executions.forEach((execution) => {
      if (execution.aggressive_is_buy) {
        matchedAskPrices.add(Number(execution.price));
      } else {
        matchedBidPrices.add(Number(execution.price));
      }
    });
  }

  let rows = "";
  for (let index = 0; index < maxRows; index += 1) {
    const ask = askLevels[index];
    const bid = bidLevels[index];
    const askWidth = ask ? Math.max(6, Math.round((Number(ask.qty) / maxQty) * 100)) : 0;
    const bidWidth = bid ? Math.max(6, Math.round((Number(bid.qty) / maxQty) * 100)) : 0;

    rows += `
      <div class="book-row flash">
        <div class="book-cell size-cell">${ask ? formatNumber(ask.qty) : ""}</div>
        <div class="book-cell price-cell ask-price">
          ${
            ask
              ? `<div class="micro-bar ask-micro" style="width:${askWidth}%"></div>${
                  matchedAskPrices.has(Number(ask.price)) ? '<span class="match-arrow ask-arrow">→</span>' : ""
                }${formatNumber(ask.price)}`
              : ""
          }
        </div>
        <div class="book-cell price-cell bid-price">
          ${
            bid
              ? `<div class="micro-bar bid-micro" style="width:${bidWidth}%"></div>${
                  matchedBidPrices.has(Number(bid.price)) ? '<span class="match-arrow bid-arrow">←</span>' : ""
                }${formatNumber(bid.price)}`
              : ""
          }
        </div>
        <div class="book-cell size-cell">${bid ? formatNumber(bid.qty) : ""}</div>
      </div>
    `;
  }

  bookRows.innerHTML = rows;
}

function renderEvent(step) {
  eventBadge.textContent = `${step.event.type} ${step.event.side}`;
  eventBadge.className = `event-badge ${step.event.side === "BUY" ? "buy" : "sell"}`;

  renderDetailGrid(eventGrid, [
    ["Sequence", step.sequence],
    ["Time", step.timestamp],
    ["Order Id", step.event.order_id],
    ["Price", formatNumber(step.event.price)],
    ["Size", formatNumber(step.event.qty)],
    ["Mode", activeReplayLabel()],
  ]);

  const statusClass = statusClassNames[step.result.status] ?? "accepted";
  resultPill.textContent = statusNames[step.result.status];
  resultPill.className = `result-pill ${statusClass}`;

  renderDetailGrid(resultGrid, [
    ["Filled", formatNumber(step.result.filled_qty)],
    ["Remaining", formatNumber(step.result.remaining_qty)],
    ["Executions", formatNumber(step.result.executions.length)],
    ["Message", safeText(step.result.message)],
  ]);
}

function renderStream(container, items, formatter, emptyLabel) {
  if (!items.length) {
    container.innerHTML = `<div class="stream-item"><div class="stream-main">${emptyLabel}</div></div>`;
    return;
  }
  container.innerHTML = items.map(formatter).join("");
}

function renderTape(step) {
  const tapeStart = Math.max(0, state.currentIndex - 11);
  const tape = state.replay.steps.slice(tapeStart, state.currentIndex + 1).reverse();
  renderStream(
    orderTape,
    tape,
    (entry) => `
      <div class="stream-item ${entry.event.side === "BUY" ? "buy" : "sell"}">
        <div class="stream-main">
          <span class="stream-type"><span class="stream-dot"></span>${entry.event.type}</span>
          <span>#${entry.sequence}</span>
        </div>
        <div class="stream-sub">
          <span>${entry.event.side}</span>
          <span>${formatNumber(entry.event.price)}</span>
          <span>${formatNumber(entry.event.qty)}</span>
        </div>
      </div>
    `,
    "No events yet."
  );
}

function renderFills(step) {
  const fills = step.result.executions.slice(-8).reverse();
  renderStream(
    fillsList,
    fills,
    (fill) => `
      <div class="stream-item ${fill.aggressive_is_buy ? "fill-buy" : "fill-sell"}">
        <div class="stream-main">
          <span class="stream-type"><span class="stream-dot"></span>Execution</span>
          <span>${formatNumber(fill.price)}</span>
        </div>
        <div class="stream-sub">
          <span>Qty ${formatNumber(fill.qty)}</span>
          <span>R ${formatNumber(fill.resting_order_id)}</span>
          <span>A ${formatNumber(fill.aggressive_order_id)}</span>
        </div>
      </div>
    `,
    "No executions at this step."
  );
}

function renderStep(index) {
  if (!state.replay || !state.replay.steps.length) {
    return;
  }

  state.currentIndex = Math.max(0, Math.min(index, state.replay.steps.length - 1));
  timeline.value = String(state.currentIndex);

  const step = state.replay.steps[state.currentIndex];
  stepLabel.textContent = `Step ${state.currentIndex + 1} / ${state.replay.steps.length}`;
  statusBanner.textContent = `${activeDatasetLabel()} · ${compactStatusNames[step.result.status]}`;
  speedLabel.textContent = `${state.stepsPerSecond} steps/sec`;
  enginePill.textContent = activeReplayLabel();

  bestBid.textContent = formatNumber(step.top.best_bid);
  bestBidQty.textContent = formatNumber(step.top.best_bid_qty);
  bestAsk.textContent = formatNumber(step.top.best_ask);
  bestAskQty.textContent = formatNumber(step.top.best_ask_qty);

  renderEvent(step);
  renderPipelineState(step);
  renderPressure(step.depth.asks, step.depth.bids);
  renderUnifiedBook(step.depth.asks, step.depth.bids);
  renderTape(step);
  renderFills(step);

  if (step.top.best_bid !== null && step.top.best_ask !== null) {
    const gap = Number(step.top.best_ask) - Number(step.top.best_bid);
    const center = (Number(step.top.best_ask) + Number(step.top.best_bid)) / 2;
    spreadValue.textContent = formatNumber(gap, 2);
    midPrice.textContent = formatNumber(center, 2);
  } else {
    spreadValue.textContent = "-";
    midPrice.textContent = "-";
  }
}

async function fetchTextFromPaths(paths) {
  for (const path of paths) {
    try {
      const response = await fetch(path, { cache: "no-store" });
      if (response.ok) {
        return response.text();
      }
    } catch (error) {
      // Try the next candidate path.
    }
  }
  throw new Error(`failed to load any of: ${paths.join(", ")}`);
}

async function fetchJsonFromPaths(paths) {
  for (const path of paths) {
    try {
      const response = await fetch(path, { cache: "no-store" });
      if (response.ok) {
        return response.json();
      }
    } catch (error) {
      // Try the next candidate path.
    }
  }
  throw new Error(`failed to load any of: ${paths.join(", ")}`);
}

function activateTab(tabName) {
  tabButtons.forEach((button) => {
    button.classList.toggle("active", button.dataset.tab === tabName);
  });
  document.querySelectorAll(".tab-panel").forEach((panel) => {
    panel.classList.toggle("active", panel.id === `tab-${tabName}`);
  });
}

function stopPlayback() {
  if (state.timerId !== null) {
    clearInterval(state.timerId);
    state.timerId = null;
  }
}

function startPlayback() {
  stopPlayback();
  state.timerId = setInterval(() => {
    if (!state.replay) {
      return;
    }
    if (state.currentIndex >= state.replay.steps.length - 1) {
      stopPlayback();
      return;
    }
    renderStep(state.currentIndex + 1);
  }, Math.max(12, Math.floor(1000 / state.stepsPerSecond)));
}

async function fetchCsv(profile) {
  const source = datasetSources[profile] ?? datasetSources.balanced;
  const text = await fetchTextFromPaths([
    `/results/${source.csv}.csv`,
    `./results/${source.csv}.csv`,
  ]);
  const [headerLine, ...lines] = text.trim().split("\n");
  const headers = parseCsvLine(headerLine);
  return lines.filter(Boolean).map((line) => {
    const values = parseCsvLine(line);
    const row = {};
    headers.forEach((header, index) => {
      row[header] = values[index];
    });
    return row;
  });
}

async function fetchReplay(profile, engine) {
  const source = datasetSources[profile] ?? datasetSources.balanced;
  const replayEngine = engine === "baseline" ? "baseline" : "optimized";
  const replay = await fetchJsonFromPaths([
    `/results/replay_${replayEngine}_${source.replay}.json`,
    `./results/replay_${replayEngine}_${source.replay}.json`,
  ]);
  if (engine === "pipeline") {
    replay.engine = "optimized_concurrent_pipeline";
  }
  return replay;
}

async function fetchOptionalText(path) {
  try {
    const response = await fetch(path, { cache: "no-store" });
    if (!response.ok) {
      return "";
    }
    return response.text();
  } catch (error) {
    return "";
  }
}

async function fetchOptionalJson(path) {
  try {
    const response = await fetch(path, { cache: "no-store" });
    if (!response.ok) {
      return null;
    }
    return response.json();
  } catch (error) {
    return null;
  }
}

async function findExistingAsset(paths) {
  for (const path of paths) {
    try {
      const response = await fetch(path, { cache: "no-store" });
      if (response.ok) {
        return path;
      }
    } catch (error) {
      // Ignore optional artifact failures so the main simulator can still load.
    }
  }
  return null;
}

async function loadData() {
  stopPlayback();
  statusBanner.textContent = "Loading...";
  if (analysisPanel) {
    analysisPanel.innerHTML = `<article class="analysis-card"><h2>Loading analysis...</h2></article>`;
  }
  if (modelsPanel) {
    modelsPanel.innerHTML = `<article class="analysis-card"><h2>Loading model analysis...</h2></article>`;
  }
  if (overviewPanel) {
    overviewPanel.innerHTML = `<article class="analysis-card"><h2>Loading overview...</h2></article>`;
  }

  try {
    const profile = state.selectedDataset;
    const engine = state.selectedReplay;
    const source = datasetSources[profile] ?? datasetSources.balanced;
    const [metrics, replay, reportFromRoot, reportFromLocal] = await Promise.all([
      fetchCsv(profile),
      fetchReplay(profile, engine),
      fetchOptionalText("/frontend/report.md"),
      fetchOptionalText("./report.md"),
    ]);
    const [comparisonMarkdown, flamegraphPath, perfPath, stageMetrics, evaluation] = await Promise.all([
      fetchOptionalText(`/results/${source.analysis}_comparison.md`).then((text) => text || fetchOptionalText(`./results/${source.analysis}_comparison.md`)),
      findExistingAsset([
        `/results/${source.analysis}_flamegraph.svg`,
        `./results/${source.analysis}_flamegraph.svg`,
        `/results/flamegraph_${source.analysis}.svg`,
        `./results/flamegraph_${source.analysis}.svg`,
        `/results/${source.csv}_flamegraph.svg`,
        `./results/${source.csv}_flamegraph.svg`,
        `/results/balanced_flamegraph.svg`,
        `./results/balanced_flamegraph.svg`,
      ]),
      findExistingAsset([
        `/results/${source.analysis}_perf.txt`,
        `./results/${source.analysis}_perf.txt`,
        `/results/${source.analysis}_perf.md`,
        `./results/${source.analysis}_perf.md`,
        `/results/perf_${source.analysis}.txt`,
        `./results/perf_${source.analysis}.txt`,
        `/results/${source.analysis}_sample.txt`,
        `./results/${source.analysis}_sample.txt`,
        `/results/balanced_perf.txt`,
        `./results/balanced_perf.txt`,
      ]),
      source.stageBench
        ? fetchCsv(source.stageBench).catch(() => [])
        : Promise.resolve([]),
      source.evaluation
        ? fetchOptionalJson(`/results/${source.evaluation}.json`).then((json) => json || fetchOptionalJson(`./results/${source.evaluation}.json`))
        : Promise.resolve(null),
    ]);

    state.metrics = metrics;
    state.replay = replay;
    state.analysis = {
      comparisonMarkdown,
      flamegraphPath,
      perfPath,
      stageMetrics,
      evaluation,
    };
    state.reportMarkdown = reportFromRoot || reportFromLocal || "";
    state.currentIndex = 0;
    timeline.max = String(Math.max(0, replay.steps.length - 1));
    timeline.value = "0";

    renderMetrics();
    renderModelsPanel();
    renderStep(0);
    renderReportPanel();
  } catch (error) {
    statusBanner.textContent = "Missing data";
    eventBadge.textContent = "No replay";
    eventGrid.innerHTML = "";
    resultGrid.innerHTML = "";
    orderTape.innerHTML = "";
    fillsList.innerHTML = "";
    bookRows.innerHTML = "";
    if (analysisPanel) {
      analysisPanel.innerHTML = `
        <article class="analysis-card">
          <h2>Analysis unavailable</h2>
          <p class="engine-copy">The analysis view needs benchmark output and optional profiling artifacts.</p>
        </article>
      `;
    }
    if (modelsPanel) {
      modelsPanel.innerHTML = `
        <article class="analysis-card">
          <h2>Model analysis unavailable</h2>
          <p class="engine-copy">The models view needs the evaluation JSON artifact and current benchmark outputs.</p>
        </article>
      `;
    }
    if (overviewPanel) {
      overviewPanel.innerHTML = `
        <article class="analysis-card">
          <h2>Overview unavailable</h2>
          <p class="engine-copy">The overview needs replay, benchmark, and evaluation artifacts to describe the full project pipeline.</p>
        </article>
      `;
    }
    state.reportMarkdown = "";
    renderReportPanel();
  }
}

reloadButton.addEventListener("click", loadData);
datasetSelect?.addEventListener("change", () => {
  state.selectedDataset = datasetSelect.value;
  loadData();
});
engineButtons.forEach((button) => {
  button.addEventListener("click", () => {
    state.selectedReplay = button.dataset.engine;
    setActiveButton(engineButtons, "engine", state.selectedReplay);
    loadData();
  });
});
tabButtons.forEach((button) => {
  button.addEventListener("click", () => activateTab(button.dataset.tab));
});
playButton.addEventListener("click", startPlayback);
pauseButton.addEventListener("click", stopPlayback);
stepButton.addEventListener("click", () => renderStep(state.currentIndex + 1));
resetButton.addEventListener("click", () => renderStep(0));
timeline.addEventListener("input", () => renderStep(Number(timeline.value)));
speedRange.addEventListener("input", () => {
  state.stepsPerSecond = Number(speedRange.value);
  speedLabel.textContent = `${state.stepsPerSecond} steps/sec`;
  if (state.timerId !== null) {
    startPlayback();
  }
});

loadData();
