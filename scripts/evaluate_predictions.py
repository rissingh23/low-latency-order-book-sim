#!/usr/bin/env python3
import argparse
import csv
import math
import json
import sys
import time

try:
    import xgboost as xgb
except Exception:
    xgb = None


FEATURE_NAMES = [
    "spread",
    "mid_price",
    "microprice",
    "imbalance_l1",
    "imbalance_l3",
    "depth_slope",
    "top_depletion_imbalance",
    "market_order_ratio",
    "cancel_ratio",
    "order_flow_imbalance",
    "last_mid_delta",
    "last_microprice_delta",
]
LABELS = ["down", "flat", "up"]
LABEL_TO_INDEX = {name: idx for idx, name in enumerate(LABELS)}


def load_rows(path):
    with open(path, newline="") as handle:
        reader = csv.DictReader(handle)
        rows = []
        for row in reader:
            features = {name: float(row[name]) for name in FEATURE_NAMES}
            rows.append((features, LABEL_TO_INDEX[row["label"]]))
        return rows


def split_rows(rows, train_fraction):
    split_index = max(1, int(len(rows) * train_fraction))
    return rows[:split_index], rows[split_index:]


def filter_binary(rows):
    filtered = []
    for features, label in rows:
      if label == LABEL_TO_INDEX["flat"]:
        continue
      filtered.append((features, 0 if label == LABEL_TO_INDEX["down"] else 1))
    return filtered


def majority_label(train_rows, class_count):
    counts = [0 for _ in range(class_count)]
    for _, label in train_rows:
        counts[label] += 1
    return max(range(class_count), key=lambda idx: counts[idx])

def heuristic_predict(features, binary=False):
    score = (0.75 * features["imbalance_l1"]) + (0.5 * features["imbalance_l3"]) + (0.05 * features["order_flow_imbalance"])
    if binary:
        return 1 if score >= 0.0 else 0
    if score > 0.15:
        return LABEL_TO_INDEX["up"]
    if score < -0.15:
        return LABEL_TO_INDEX["down"]
    return LABEL_TO_INDEX["flat"]


def load_model(path):
    with open(path, newline="") as handle:
        reader = list(csv.reader(handle))
    if not reader:
        raise SystemExit("model file is empty")
    offset = 0
    task = "ternary"
    if len(reader[0]) == 2 and reader[0][0] == "task":
        task = reader[0][1]
        offset = 1
    expected_rows = 5 if task == "binary" else 6
    if len(reader) - offset != expected_rows:
        raise SystemExit("unexpected model row count")
    means = [float(value) for value in reader[offset + 0]]
    scales = [float(value) for value in reader[offset + 1]]
    bias = [float(value) for value in reader[offset + 2]]
    weights = [[float(value) for value in reader[row_idx]] for row_idx in range(offset + 3, len(reader))]
    return task, means, scales, bias, weights


def load_mlp_model(path):
    with open(path) as handle:
        payload = json.load(handle)
    return payload


def load_xgb_model(path):
    if xgb is None:
        raise SystemExit("xgboost is not installed")
    booster = xgb.Booster()
    booster.load_model(path)
    return booster


def linear_predict(features, model):
    task, means, scales, bias, weights = model
    values = [features[name] for name in FEATURE_NAMES]
    normalized = [(values[i] - means[i]) / (scales[i] or 1.0) for i in range(len(values))]
    logits = []
    for cls in range(len(weights)):
        logits.append(bias[cls] + sum(weights[cls][i] * normalized[i] for i in range(len(values))))
    pred = max(range(len(weights)), key=lambda idx: logits[idx])
    if task == "binary":
        return pred
    return pred


def mlp_predict(features, payload):
    values = [features[name] for name in FEATURE_NAMES]
    normalized = [(values[i] - payload["mean"][i]) / (payload["scale"][i] or 1.0) for i in range(len(values))]
    model = payload["model"]
    hidden = []
    for h in range(model["hidden_size"]):
        raw = model["b1"][h] + sum(model["w1"][h][i] * normalized[i] for i in range(len(normalized)))
        hidden.append(math.tanh(raw))
    logits = [
        model["b2"][c] + sum(model["w2"][c][h] * hidden[h] for h in range(model["hidden_size"]))
        for c in range(len(model["b2"]))
    ]
    return max(range(len(logits)), key=lambda idx: logits[idx])


def xgb_predict(features, booster):
    values = [features[name] for name in FEATURE_NAMES]
    dmatrix = xgb.DMatrix([values], feature_names=FEATURE_NAMES)
    pred = booster.predict(dmatrix)[0]
    return 1 if pred >= 0.5 else 0


def confusion_matrix(rows, predictor, class_count):
    matrix = [[0 for _ in range(class_count)] for _ in range(class_count)]
    for features, label in rows:
        pred = predictor(features)
        matrix[label][pred] += 1
    return matrix


def accuracy(matrix):
    total = sum(sum(row) for row in matrix) or 1
    correct = sum(matrix[i][i] for i in range(len(matrix)))
    return correct / total


def macro_f1(matrix):
    scores = []
    for cls in range(len(matrix)):
        tp = matrix[cls][cls]
        fp = sum(matrix[row][cls] for row in range(len(matrix)) if row != cls)
        fn = sum(matrix[cls][col] for col in range(len(matrix)) if col != cls)
        precision = tp / (tp + fp) if (tp + fp) else 0.0
        recall = tp / (tp + fn) if (tp + fn) else 0.0
        if precision + recall == 0.0:
            scores.append(0.0)
        else:
            scores.append(2.0 * precision * recall / (precision + recall))
    return sum(scores) / len(scores)


def percentile_ns(values, percentile):
    if not values:
        return 0.0
    ordered = sorted(values)
    index = int(percentile * (len(ordered) - 1))
    return ordered[index]


def print_report(name, matrix, labels, latency_ns):
    print(f"\n{name}")
    print(f"  accuracy: {accuracy(matrix):.4f}")
    print(f"  macro_f1: {macro_f1(matrix):.4f}")
    print(f"  inference_p50_ns: {percentile_ns(latency_ns, 0.50):.0f}")
    print(f"  inference_p99_ns: {percentile_ns(latency_ns, 0.99):.0f}")
    print("  confusion_matrix:")
    for row_name, row in zip(labels, matrix):
        print(f"    {row_name:>4}: {row}")


def report_dict(name, matrix, labels, latency_ns):
    return {
        "name": name,
        "accuracy": accuracy(matrix),
        "macro_f1": macro_f1(matrix),
        "inference_p50_ns": percentile_ns(latency_ns, 0.50),
        "inference_p95_ns": percentile_ns(latency_ns, 0.95),
        "inference_p99_ns": percentile_ns(latency_ns, 0.99),
        "confusion_matrix": matrix,
        "labels": labels,
    }


def evaluate_predictor(rows, predictor, class_count):
    matrix = [[0 for _ in range(class_count)] for _ in range(class_count)]
    latency_ns = []
    for features, label in rows:
        start = time.perf_counter_ns()
        pred = predictor(features)
        end = time.perf_counter_ns()
        latency_ns.append(end - start)
        matrix[label][pred] += 1
    return matrix, latency_ns


def main():
    parser = argparse.ArgumentParser(description="Evaluate heuristic and linear LOB predictors.")
    parser.add_argument("--input", required=True, help="Input feature CSV from export-features mode")
    parser.add_argument("--train-fraction", type=float, default=0.8)
    parser.add_argument("--model", help="Optional linear model CSV")
    parser.add_argument("--mlp-model", help="Optional MLP model JSON")
    parser.add_argument("--xgb-model", help="Optional XGBoost model")
    parser.add_argument("--json-output", help="Optional JSON output path")
    parser.add_argument("--task", choices=["ternary", "binary"], default="binary")
    args = parser.parse_args()

    rows = load_rows(args.input)
    labels = LABELS if args.task == "ternary" else ["down", "up"]
    if args.task == "binary":
        rows = filter_binary(rows)
    train_rows, test_rows = split_rows(rows, args.train_fraction)
    class_count = len(labels)
    majority = majority_label(train_rows, class_count)

    reports = []
    majority_matrix, majority_latency = evaluate_predictor(test_rows, lambda _features: majority, class_count)
    reports.append(report_dict("majority_class", majority_matrix, labels, majority_latency))
    print_report("majority_class", majority_matrix, labels, majority_latency)

    heuristic_matrix, heuristic_latency = evaluate_predictor(
        test_rows,
        lambda features: heuristic_predict(features, binary=args.task == "binary"),
        class_count,
    )
    reports.append(report_dict("heuristic_imbalance", heuristic_matrix, labels, heuristic_latency))
    print_report("heuristic_imbalance", heuristic_matrix, labels, heuristic_latency)

    if args.model:
        model = load_model(args.model)
        linear_matrix, linear_latency = evaluate_predictor(test_rows, lambda features: linear_predict(features, model), class_count)
        reports.append(report_dict("linear_model", linear_matrix, labels, linear_latency))
        print_report("linear_model", linear_matrix, labels, linear_latency)

    if args.mlp_model:
        payload = load_mlp_model(args.mlp_model)
        mlp_matrix, mlp_latency = evaluate_predictor(test_rows, lambda features: mlp_predict(features, payload), class_count)
        reports.append(report_dict("tiny_mlp", mlp_matrix, labels, mlp_latency))
        print_report("tiny_mlp", mlp_matrix, labels, mlp_latency)

    if args.xgb_model:
        if xgb is None:
            print("xgboost is not installed; skipping xgboost evaluation", file=sys.stderr)
        else:
            booster = load_xgb_model(args.xgb_model)
            xgb_matrix, xgb_latency = evaluate_predictor(test_rows, lambda features: xgb_predict(features, booster), class_count)
            reports.append(report_dict("xgboost", xgb_matrix, labels, xgb_latency))
            print_report("xgboost", xgb_matrix, labels, xgb_latency)

    if args.json_output:
        with open(args.json_output, "w") as handle:
            json.dump(
                {
                    "input": args.input,
                    "task": args.task,
                    "train_fraction": args.train_fraction,
                    "test_rows": len(test_rows),
                    "reports": reports,
                },
                handle,
                indent=2,
            )


if __name__ == "__main__":
    main()
