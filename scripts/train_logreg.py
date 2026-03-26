#!/usr/bin/env python3
import argparse
import csv
import math
from pathlib import Path


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
LABEL_TO_INDEX = {"down": 0, "flat": 1, "up": 2}


def load_rows(path):
    with open(path, newline="") as handle:
        reader = csv.DictReader(handle)
        rows = []
        for row in reader:
            features = [float(row[name]) for name in FEATURE_NAMES]
            rows.append((features, LABEL_TO_INDEX[row["label"]]))
        return rows


def filter_binary(rows):
    filtered = []
    for features, label in rows:
        if label == LABEL_TO_INDEX["flat"]:
            continue
        filtered.append((features, 0 if label == LABEL_TO_INDEX["down"] else 1))
    return filtered


def split_rows(rows, train_fraction):
    split_index = max(1, int(len(rows) * train_fraction))
    return rows[:split_index], rows[split_index:]


def compute_norm(train_rows):
    means = []
    scales = []
    for idx in range(len(FEATURE_NAMES)):
        values = [row[0][idx] for row in train_rows]
        mean = sum(values) / len(values)
        variance = sum((value - mean) ** 2 for value in values) / len(values)
        scale = math.sqrt(variance) or 1.0
        means.append(mean)
        scales.append(scale)
    return means, scales


def normalize_rows(rows, means, scales):
    normalized = []
    for features, label in rows:
        normalized.append(([(features[i] - means[i]) / scales[i] for i in range(len(features))], label))
    return normalized


def softmax(logits):
    max_logit = max(logits)
    exps = [math.exp(value - max_logit) for value in logits]
    total = sum(exps) or 1.0
    return [value / total for value in exps]


def train_softmax(train_rows, epochs, learning_rate, class_count):
    weights = [[0.0 for _ in FEATURE_NAMES] for _ in range(class_count)]
    bias = [0.0 for _ in range(class_count)]

    for _ in range(epochs):
        for features, label in train_rows:
            logits = [bias[cls] + sum(weights[cls][i] * features[i] for i in range(len(features))) for cls in range(class_count)]
            probs = softmax(logits)
            for cls in range(class_count):
                target = 1.0 if cls == label else 0.0
                error = probs[cls] - target
                bias[cls] -= learning_rate * error
                for idx, value in enumerate(features):
                    weights[cls][idx] -= learning_rate * error * value

    return weights, bias


def accuracy(rows, weights, bias):
    if not rows:
        return 0.0
    correct = 0
    for features, label in rows:
        logits = [bias[cls] + sum(weights[cls][i] * features[i] for i in range(len(features))) for cls in range(len(weights))]
        pred = max(range(len(weights)), key=lambda idx: logits[idx])
        correct += pred == label
    return correct / len(rows)


def write_model(path, means, scales, bias, weights, task):
    with open(path, "w", newline="") as handle:
        writer = csv.writer(handle)
        writer.writerow(["task", task])
        writer.writerow(means)
        writer.writerow(scales)
        writer.writerow(bias)
        for row in weights:
            writer.writerow(row)


def main():
    parser = argparse.ArgumentParser(description="Train a simple softmax regression model on exported LOB features.")
    parser.add_argument("--input", required=True, help="Input feature CSV from export-features mode")
    parser.add_argument("--output", required=True, help="Output model CSV")
    parser.add_argument("--train-fraction", type=float, default=0.8)
    parser.add_argument("--epochs", type=int, default=40)
    parser.add_argument("--learning-rate", type=float, default=0.05)
    parser.add_argument("--task", choices=["ternary", "binary"], default="binary")
    args = parser.parse_args()

    rows = load_rows(args.input)
    if args.task == "binary":
        rows = filter_binary(rows)
    if len(rows) < 10:
        raise SystemExit("need at least 10 labeled rows to train")

    train_rows, test_rows = split_rows(rows, args.train_fraction)
    means, scales = compute_norm(train_rows)
    train_norm = normalize_rows(train_rows, means, scales)
    test_norm = normalize_rows(test_rows, means, scales)
    class_count = 2 if args.task == "binary" else 3
    weights, bias = train_softmax(train_norm, args.epochs, args.learning_rate, class_count)
    write_model(args.output, means, scales, bias, weights, args.task)

    print(f"trained {args.task} softmax model on {len(train_rows)} rows")
    print(f"test accuracy: {accuracy(test_norm, weights, bias):.4f}")
    print(f"wrote model to {Path(args.output)}")


if __name__ == "__main__":
    main()
