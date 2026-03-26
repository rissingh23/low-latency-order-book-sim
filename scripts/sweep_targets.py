#!/usr/bin/env python3
import argparse
import json
import shutil
import subprocess
from pathlib import Path


TARGET_CONFIGS = [
    {"name": "fixed_h10", "label_mode": "fixed_horizon", "horizon": 10, "threshold": 0},
    {"name": "fixed_h25", "label_mode": "fixed_horizon", "horizon": 25, "threshold": 0},
    {"name": "fixed_h50", "label_mode": "fixed_horizon", "horizon": 50, "threshold": 0},
    {"name": "next_non_zero", "label_mode": "next_non_zero", "horizon": 10, "threshold": 0},
    {"name": "threshold_h10_t100", "label_mode": "thresholded_horizon", "horizon": 10, "threshold": 100},
    {"name": "threshold_h25_t100", "label_mode": "thresholded_horizon", "horizon": 25, "threshold": 100},
    {"name": "threshold_h50_t100", "label_mode": "thresholded_horizon", "horizon": 50, "threshold": 100},
]


def run(cmd, cwd):
    print("+", " ".join(str(part) for part in cmd))
    subprocess.run(cmd, cwd=cwd, check=True)


def best_non_majority_report(reports):
    filtered = [report for report in reports if report["name"] != "majority_class"]
    return max(filtered, key=lambda report: (report["macro_f1"], report["accuracy"]))


def main():
    parser = argparse.ArgumentParser(description="Sweep label targets and model families, then keep the best result.")
    parser.add_argument("--simulator", default="build/lob_simulator")
    parser.add_argument("--python", default="python3")
    parser.add_argument("--dataset", required=True)
    parser.add_argument("--results-dir", default="results")
    parser.add_argument("--depth", type=int, default=3)
    args = parser.parse_args()

    cwd = Path.cwd()
    results_dir = Path(args.results_dir)
    sweep_dir = results_dir / "target_sweep"
    sweep_dir.mkdir(parents=True, exist_ok=True)

    summary = {"runs": []}
    best_run = None

    for config in TARGET_CONFIGS:
      name = config["name"]
      features_path = sweep_dir / f"{name}_features.csv"
      linear_path = sweep_dir / f"{name}_linear.csv"
      mlp_path = sweep_dir / f"{name}_mlp.json"
      xgb_path = sweep_dir / f"{name}_xgb.json"
      eval_path = sweep_dir / f"{name}_eval.json"

      run(
          [
              args.simulator,
              "--mode",
              "export-features",
              "--dataset",
              args.dataset,
              "--depth",
              str(args.depth),
              "--label-mode",
              config["label_mode"],
              "--horizon-events",
              str(config["horizon"]),
              "--move-threshold",
              str(config["threshold"]),
              "--output",
              str(features_path),
          ],
          cwd,
      )

      run(
          [
              args.python,
              "scripts/train_logreg.py",
              "--input",
              str(features_path),
              "--task",
              "binary",
              "--output",
              str(linear_path),
          ],
          cwd,
      )

      run(
          [
              args.python,
              "scripts/train_mlp.py",
              "--input",
              str(features_path),
              "--task",
              "binary",
              "--output",
              str(mlp_path),
          ],
          cwd,
      )

      run(
          [
              args.python,
              "scripts/train_xgboost.py",
              "--input",
              str(features_path),
              "--output",
              str(xgb_path),
          ],
          cwd,
      )

      run(
          [
              args.python,
              "scripts/evaluate_predictions.py",
              "--input",
              str(features_path),
              "--task",
              "binary",
              "--model",
              str(linear_path),
              "--mlp-model",
              str(mlp_path),
              "--xgb-model",
              str(xgb_path),
              "--json-output",
              str(eval_path),
          ],
          cwd,
      )

      payload = json.loads(eval_path.read_text())
      run_summary = {
          "target_name": name,
          "label_mode": config["label_mode"],
          "horizon_events": config["horizon"],
          "move_threshold": config["threshold"],
          "evaluation_path": str(eval_path),
          "features_path": str(features_path),
          "reports": payload["reports"],
      }
      run_summary["best_report"] = best_non_majority_report(payload["reports"])
      summary["runs"].append(run_summary)
      if best_run is None or (
          run_summary["best_report"]["macro_f1"],
          run_summary["best_report"]["accuracy"],
      ) > (
          best_run["best_report"]["macro_f1"],
          best_run["best_report"]["accuracy"],
      ):
          best_run = run_summary

    if best_run is None:
        raise SystemExit("no target runs completed")

    summary["best_run"] = best_run
    (results_dir / "target_sweep_summary.json").write_text(json.dumps(summary, indent=2))

    best_eval = json.loads(Path(best_run["evaluation_path"]).read_text())
    best_eval["target_name"] = best_run["target_name"]
    best_eval["label_mode"] = best_run["label_mode"]
    best_eval["horizon_events"] = best_run["horizon_events"]
    best_eval["move_threshold"] = best_run["move_threshold"]
    best_eval["selection_metric"] = "macro_f1_then_accuracy"
    (results_dir / "aapl_eval_binary.json").write_text(json.dumps(best_eval, indent=2))

    shutil.copyfile(best_run["features_path"], results_dir / "aapl_features_best.csv")
    print(json.dumps({
        "best_target": best_run["target_name"],
        "best_model": best_run["best_report"]["name"],
        "accuracy": best_run["best_report"]["accuracy"],
        "macro_f1": best_run["best_report"]["macro_f1"],
        "summary": str(results_dir / "target_sweep_summary.json"),
        "best_eval": str(results_dir / "aapl_eval_binary.json"),
    }, indent=2))


if __name__ == "__main__":
    main()
