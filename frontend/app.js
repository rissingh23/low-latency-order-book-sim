const reloadButton = document.getElementById("reload-button");
const datasetButtons = [...document.querySelectorAll("[data-dataset]")];
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
const comparisonPanel = document.getElementById("comparison-panel");
const analysisPanel = document.getElementById("analysis-panel");

const datasetSources = {
  balanced: {
    label: "Balanced",
    csv: "balanced",
    replay: "balanced",
    analysis: "balanced",
  },
  cancel_heavy: {
    label: "Cancel Heavy",
    csv: "cancel_heavy",
    replay: "cancel_heavy",
    analysis: "cancel_heavy",
  },
  bursty: {
    label: "Bursty",
    csv: "bursty",
    replay: "bursty",
    analysis: "bursty",
  },
  aapl_lobster: {
    label: "AAPL",
    csv: "aapl_lobster",
    replay: "aapl_lobster_normalized",
    analysis: "aapl_lobster_normalized",
  },
};

const state = {
  replay: null,
  metrics: [],
  analysis: {
    comparisonMarkdown: "",
    flamegraphPath: null,
    perfPath: null,
  },
  currentIndex: 0,
  timerId: null,
  stepsPerSecond: Number(speedRange.value),
  selectedDataset: datasetButtons.find((button) => button.classList.contains("active"))?.dataset.dataset ?? "aapl_lobster",
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
  if (engine.startsWith("optimized_sharded_")) {
    return "Sharded Engine";
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
  if (summary.engine.startsWith("optimized_sharded_")) {
    return "Production-style scaling path. Each shard keeps a single-threaded matcher and parallelizes across symbols instead of inside one book.";
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
  } else if (summary.engine.startsWith("optimized_sharded_")) {
    stages = `
      <div class="flow-shards">
        <div class="flow-shard">
          <span>Shard A</span>
          <div class="flow-mini"></div>
          <strong>Book A</strong>
        </div>
        <div class="flow-shard">
          <span>Shard B</span>
          <div class="flow-mini"></div>
          <strong>Book B</strong>
        </div>
        <div class="flow-shard">
          <span>Shard C</span>
          <div class="flow-mini"></div>
          <strong>Book C</strong>
        </div>
      </div>
    `;
    copy = "Parallelism comes from splitting symbols across independent books, not by having many threads mutate one book.";
  }

  return `
    <article class="system-card">
      <div class="panel-header">
        <h3>${engineTitle(summary.engine)}</h3>
        <span class="ghost-pill">${formatNumber(summary.throughput_ops_per_sec)} ops/s</span>
      </div>
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

function renderEngineCard(summary, extraCopy) {
  const highlightRows = engineHighlights(summary);
  const rowsHtml = highlightRows
    .map(([label, value]) => `<div class="metric-row"><span>${label}</span><span>${value}</span></div>`)
    .join("");
  return `
    <article class="engine-card">
      <div class="engine-hero">
        <div>
          <p class="eyebrow">${activeDatasetLabel()} run</p>
          <h2>${engineTitle(summary.engine)}</h2>
          <p class="engine-copy">${engineNarrative(summary)}</p>
        </div>
        <div class="engine-chip">${safeText(summary.engine)}</div>
      </div>
      <div class="engine-layout">
        <section class="engine-metrics">
          <h3>Measured Results</h3>
          ${rowsHtml}
        </section>
        <section class="engine-explanation">
          <h3>How To Read It</h3>
          <p>${extraCopy}</p>
        </section>
      </div>
    </article>
  `;
}

function renderMetrics() {
  const baseline = state.metrics.find((metric) => metric.engine === "baseline_single_thread");
  const optimized = state.metrics.find((metric) => metric.engine === "optimized_single_thread");
  const pipeline = state.metrics.find((metric) => metric.engine === "optimized_concurrent_pipeline");
  const sharded = state.metrics.find((metric) => typeof metric.engine === "string" && metric.engine.startsWith("optimized_sharded_"));
  const summaryList = [baseline, optimized, pipeline, sharded];

  const sections = [];
  if (baseline) {
    sections.push(
      renderEngineCard(
          baseline,
          "Treat this as the simplest correct reference. Improvements elsewhere should come from less work per event, not different behavior.")
    );
  }
  if (optimized) {
    sections.push(
      renderEngineCard(
          optimized,
          "Compare this directly to the baseline on service time and throughput. This is the cleanest test of whether the matcher itself improved.")
    );
  }
  if (pipeline) {
    sections.push(
      renderEngineCard(
          pipeline,
          "Compare service time to end-to-end time. If those diverge sharply, the queue is growing faster than the matcher can drain it.")
    );
  }
  if (sharded) {
    sections.push(
      renderEngineCard(
          sharded,
          "Use this when the input has multiple symbols. The gain comes from parallel books, not from mutating one book with many threads.")
    );
  }

  if (comparisonPanel) {
    comparisonPanel.innerHTML = sections.length
      ? `
        <div class="comparison-shell">
          <div class="system-map">
            ${summaryList.filter(Boolean).map((summary) => renderArchitectureCard(summary)).join("")}
          </div>
          <article class="analysis-card">
            <div class="panel-header">
              <h2>Comparison</h2>
              <span class="ghost-pill">${activeDatasetLabel()}</span>
            </div>
            ${renderComparisonChart(summaryList, "throughput_ops_per_sec", "Throughput", " ops/s")}
            ${renderComparisonChart(summaryList, "service_p99_ns", "Service p99", " ns")}
            ${renderComparisonChart(summaryList, "end_to_end_p99_ns", "End-to-End p99", " ns")}
          </article>
          <div class="comparison-grid">${sections.join("")}</div>
        </div>
      `
      : `
        <article class="engine-card">
          <h2>No comparison data</h2>
          <p class="engine-copy">This dataset did not produce any benchmark rows. Try reloading or regenerating the results file.</p>
        </article>
      `;
  }

  if (analysisPanel) {
    renderAnalysisPanel({ baseline, optimized, pipeline, sharded });
  }
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

function renderAnalysisPanel({ baseline, optimized, pipeline, sharded }) {
  if (!analysisPanel) {
    return;
  }
  const hasAnySummary = Boolean(baseline || optimized || pipeline || sharded);
  const optimizationDelta = percentDelta(baseline?.throughput_ops_per_sec, optimized?.throughput_ops_per_sec);
  const queueDelta = percentDelta(optimized?.end_to_end_p99_ns, pipeline?.end_to_end_p99_ns);
  const shardedDelta = percentDelta(optimized?.throughput_ops_per_sec, sharded?.throughput_ops_per_sec);
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

  const summaryRows = [
    ["Baseline", baseline],
    ["Optimized", optimized],
    ["Pipeline", pipeline],
    ["Sharded", sharded],
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
    <div class="analysis-grid">
      <article class="analysis-card">
        <div class="panel-header">
          <h2>Analysis</h2>
          <span class="ghost-pill">${activeDatasetLabel()}</span>
        </div>
        <div class="analysis-hero">
          <div class="analysis-stat">
            <span class="analysis-label">Optimized vs Baseline</span>
            ${renderDeltaChip(optimizationDelta)}
            <p>Matcher/data-structure improvement.</p>
          </div>
          <div class="analysis-stat">
            <span class="analysis-label">Pipeline p99 vs Optimized</span>
            ${renderDeltaChip(queueDelta)}
            <p>Queueing impact on tail latency.</p>
          </div>
          <div class="analysis-stat">
            <span class="analysis-label">Sharded vs Optimized</span>
            ${renderDeltaChip(shardedDelta)}
            <p>Useful only on multi-symbol input.</p>
          </div>
        </div>
        <div class="comparison-table">
          <div class="comparison-row head">
            <span>Engine</span>
            <span>Throughput</span>
            <span>Service p99</span>
            <span>E2E p99</span>
          </div>
          ${summaryRows}
        </div>
      </article>

      <article class="analysis-card">
        <div class="panel-header">
          <h2>Charts</h2>
          <span class="ghost-pill">Visuals</span>
        </div>
        <div class="analysis-charts">
          ${renderComparisonChart([baseline, optimized, pipeline, sharded], "throughput_ops_per_sec", "Engine Throughput", " ops/s")}
          ${renderComparisonChart([baseline, optimized, pipeline, sharded], "queue_delay_p99_ns", "Queue Delay p99", " ns")}
        </div>
      </article>

      <article class="analysis-card">
        <div class="panel-header">
          <h2>Profile + Flame Graph</h2>
          <span class="ghost-pill">Artifacts</span>
        </div>
        <div class="artifact-toolbar">
          ${perfBlock}
        </div>
        ${flamegraphBlock}
      </article>

      <article class="analysis-card">
        <div class="panel-header">
          <h2>Interpretation</h2>
          <span class="ghost-pill">Notes</span>
        </div>
        <div class="notes-grid">
          <div class="note-block">
            <h3>Matcher</h3>
            <p>${optimized ? "Optimized service metrics tell you whether the matcher itself got faster." : "Load a run to inspect matcher service time."}</p>
          </div>
          <div class="note-block">
            <h3>Queueing</h3>
            <p>${pipeline ? "If pipeline service stays low while end-to-end spikes, backlog is dominating." : "Pipeline stats will appear here when that mode is present."}</p>
          </div>
          <div class="note-block">
            <h3>Scaling</h3>
            <p>${sharded ? "Sharding scales across books rather than inside one book." : "No sharded metrics detected for this dataset."}</p>
          </div>
        </div>
        ${reportLines ? `<div class="report-block"><h3>Run Report</h3><ul class="report-list">${reportLines}</ul></div>` : ""}
      </article>
    </div>
  ` : `
    <article class="analysis-card">
      <h2>No analysis data</h2>
      <p class="engine-copy">The run loaded replay data, but no benchmark summaries were available for analysis. Regenerate the results CSV and reload.</p>
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
  const response = await fetch(`/results/${source.csv}.csv`, { cache: "no-store" });
  if (!response.ok) {
    throw new Error(`failed to load /results/${source.csv}.csv`);
  }

  const text = await response.text();
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
  const response = await fetch(`/results/replay_${replayEngine}_${source.replay}.json`, { cache: "no-store" });
  if (!response.ok) {
    throw new Error(`failed to load /results/replay_${replayEngine}_${source.replay}.json`);
  }
  const replay = await response.json();
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
  if (comparisonPanel) {
    comparisonPanel.innerHTML = `<article class="engine-card"><h2>Loading comparison...</h2></article>`;
  }
  if (analysisPanel) {
    analysisPanel.innerHTML = `<article class="analysis-card"><h2>Loading analysis...</h2></article>`;
  }

  try {
    const profile = state.selectedDataset;
    const engine = state.selectedReplay;
    const source = datasetSources[profile] ?? datasetSources.balanced;
    const [metrics, replay] = await Promise.all([
      fetchCsv(profile),
      fetchReplay(profile, engine),
    ]);
    const [comparisonMarkdown, flamegraphPath, perfPath] = await Promise.all([
      fetchOptionalText(`/results/${source.analysis}_comparison.md`),
      findExistingAsset([
        `/results/${source.analysis}_flamegraph.svg`,
        `/results/flamegraph_${source.analysis}.svg`,
        `/results/${source.csv}_flamegraph.svg`,
        `/results/balanced_flamegraph.svg`,
      ]),
      findExistingAsset([
        `/results/${source.analysis}_perf.txt`,
        `/results/${source.analysis}_perf.md`,
        `/results/perf_${source.analysis}.txt`,
        `/results/${source.analysis}_sample.txt`,
        `/results/balanced_perf.txt`,
      ]),
    ]);

    state.metrics = metrics;
    state.replay = replay;
    state.analysis = {
      comparisonMarkdown,
      flamegraphPath,
      perfPath,
    };
    state.currentIndex = 0;
    timeline.max = String(Math.max(0, replay.steps.length - 1));
    timeline.value = "0";

    renderMetrics();
    renderStep(0);
  } catch (error) {
    statusBanner.textContent = "Missing data";
    eventBadge.textContent = "No replay";
    eventGrid.innerHTML = "";
    resultGrid.innerHTML = "";
    orderTape.innerHTML = "";
    fillsList.innerHTML = "";
    bookRows.innerHTML = "";
    if (comparisonPanel) {
      comparisonPanel.innerHTML = `
        <article class="engine-card">
          <h2>Comparison unavailable</h2>
          <p class="engine-copy">The benchmark file could not be loaded for this dataset.</p>
        </article>
      `;
    }
    if (analysisPanel) {
      analysisPanel.innerHTML = `
        <article class="analysis-card">
          <h2>Analysis unavailable</h2>
          <p class="engine-copy">The analysis view needs benchmark output and optional profiling artifacts.</p>
        </article>
      `;
    }
  }
}

reloadButton.addEventListener("click", loadData);
datasetButtons.forEach((button) => {
  button.addEventListener("click", () => {
    state.selectedDataset = button.dataset.dataset;
    setActiveButton(datasetButtons, "dataset", state.selectedDataset);
    loadData();
  });
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
