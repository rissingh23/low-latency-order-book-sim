#!/usr/bin/env python3
import argparse
import csv
import json
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


def tanh_vector(values):
    return [math.tanh(value) for value in values]


def train_mlp(train_rows, epochs, learning_rate, class_count, hidden_size):
    in_dim = len(FEATURE_NAMES)
    w1 = [[0.01 * ((row + col + 1) % 7 - 3) for col in range(in_dim)] for row in range(hidden_size)]
    b1 = [0.0 for _ in range(hidden_size)]
    w2 = [[0.01 * ((row + col + 2) % 5 - 2) for col in range(hidden_size)] for row in range(class_count)]
    b2 = [0.0 for _ in range(class_count)]

    for _ in range(epochs):
        for features, label in train_rows:
            hidden_raw = [b1[h] + sum(w1[h][i] * features[i] for i in range(in_dim)) for h in range(hidden_size)]
            hidden = tanh_vector(hidden_raw)
            logits = [b2[c] + sum(w2[c][h] * hidden[h] for h in range(hidden_size)) for c in range(class_count)]
            probs = softmax(logits)

            delta_out = [probs[c] - (1.0 if c == label else 0.0) for c in range(class_count)]
            delta_hidden = []
            for h in range(hidden_size):
                downstream = sum(delta_out[c] * w2[c][h] for c in range(class_count))
                delta_hidden.append((1.0 - hidden[h] * hidden[h]) * downstream)

            for c in range(class_count):
                b2[c] -= learning_rate * delta_out[c]
                for h in range(hidden_size):
                    w2[c][h] -= learning_rate * delta_out[c] * hidden[h]

            for h in range(hidden_size):
                b1[h] -= learning_rate * delta_hidden[h]
                for i in range(in_dim):
                    w1[h][i] -= learning_rate * delta_hidden[h] * features[i]

    return {"hidden_size": hidden_size, "w1": w1, "b1": b1, "w2": w2, "b2": b2}


def predict(model, features):
    hidden_raw = [model["b1"][h] + sum(model["w1"][h][i] * features[i] for i in range(len(features))) for h in range(model["hidden_size"])]
    hidden = tanh_vector(hidden_raw)
    logits = [model["b2"][c] + sum(model["w2"][c][h] * hidden[h] for h in range(model["hidden_size"])) for c in range(len(model["b2"]))]
    return max(range(len(logits)), key=lambda idx: logits[idx])


def accuracy(rows, model):
    if not rows:
        return 0.0
    correct = 0
    for features, label in rows:
        correct += predict(model, features) == label
    return correct / len(rows)


def main():
    parser = argparse.ArgumentParser(description="Train a tiny pure-Python MLP on exported LOB features.")
    parser.add_argument("--input", required=True)
    parser.add_argument("--output", required=True)
    parser.add_argument("--task", choices=["ternary", "binary"], default="binary")
    parser.add_argument("--train-fraction", type=float, default=0.8)
    parser.add_argument("--epochs", type=int, default=30)
    parser.add_argument("--learning-rate", type=float, default=0.01)
    parser.add_argument("--hidden-size", type=int, default=12)
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
    model = train_mlp(train_norm, args.epochs, args.learning_rate, class_count, args.hidden_size)

    payload = {
        "task": args.task,
        "feature_names": FEATURE_NAMES,
        "mean": means,
        "scale": scales,
        "model": model,
        "test_accuracy": accuracy(test_norm, model),
    }
    Path(args.output).write_text(json.dumps(payload, indent=2))
    print(f"trained {args.task} mlp on {len(train_rows)} rows")
    print(f"test accuracy: {payload['test_accuracy']:.4f}")
    print(f"wrote model to {args.output}")


if __name__ == "__main__":
    main()
