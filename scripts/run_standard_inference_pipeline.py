#!/usr/bin/env python3
import argparse
import csv
import json
import math
import time
from pathlib import Path

import numpy as np

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

LABEL_TO_INDEX = {"down": 0, "flat": 1, "up": 2}

FEATURE_SETS = {
    "pressure_core": [
        "spread",
        "microprice",
        "imbalance_l1",
        "imbalance_l3",
        "top_depletion_imbalance",
        "order_flow_imbalance",
        "last_microprice_delta",
    ],
    "book_dynamics": [
        "spread",
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
    ],
    "all_no_mid": [
        "spread",
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
    ],
}


def load_binary_rows(path):
    features = []
    labels = []
    with open(path, newline="") as handle:
        reader = csv.DictReader(handle)
        for row in reader:
            label = LABEL_TO_INDEX[row["label"]]
            if label == LABEL_TO_INDEX["flat"]:
                continue
            features.append([float(row[name]) for name in FEATURE_NAMES])
            labels.append(0 if label == LABEL_TO_INDEX["down"] else 1)
    return np.asarray(features, dtype=np.float64), np.asarray(labels, dtype=np.int64)


def split_time_series(xs, ys, train_frac=0.7, val_frac=0.15):
    n = len(xs)
    train_end = max(1, int(n * train_frac))
    val_end = max(train_end + 1, int(n * (train_frac + val_frac)))
    val_end = min(val_end, n - 1)
    return {
        "train": (xs[:train_end], ys[:train_end]),
        "val": (xs[train_end:val_end], ys[train_end:val_end]),
        "test": (xs[val_end:], ys[val_end:]),
    }


def feature_indices(feature_names):
    return [FEATURE_NAMES.index(name) for name in feature_names]


def macro_f1(y_true, y_pred):
    scores = []
    for cls in (0, 1):
        tp = np.sum((y_true == cls) & (y_pred == cls))
        fp = np.sum((y_true != cls) & (y_pred == cls))
        fn = np.sum((y_true == cls) & (y_pred != cls))
        precision = tp / (tp + fp) if (tp + fp) else 0.0
        recall = tp / (tp + fn) if (tp + fn) else 0.0
        scores.append(0.0 if precision + recall == 0.0 else (2.0 * precision * recall / (precision + recall)))
    return float(sum(scores) / len(scores))


def confusion_matrix(y_true, y_pred):
    matrix = [[0, 0], [0, 0]]
    for truth, pred in zip(y_true, y_pred):
        matrix[int(truth)][int(pred)] += 1
    return matrix


def accuracy(y_true, y_pred):
    return float(np.mean(y_true == y_pred))


def percentile_ns(values, percentile):
    ordered = sorted(values)
    index = int(percentile * (len(ordered) - 1))
    return float(ordered[index])


def time_predictor(predict_fn, xs, warmup=32, sample_limit=1024):
    sample = xs[: min(sample_limit, len(xs))]
    for row in sample[: min(warmup, len(sample))]:
        predict_fn(row)
    timings = []
    for row in sample:
        start = time.perf_counter_ns()
        predict_fn(row)
        end = time.perf_counter_ns()
        timings.append(end - start)
    return {
        "inference_p50_ns": percentile_ns(timings, 0.50),
        "inference_p95_ns": percentile_ns(timings, 0.95),
        "inference_p99_ns": percentile_ns(timings, 0.99),
    }


def normalize_fit(xs):
    mean = xs.mean(axis=0)
    scale = xs.std(axis=0)
    scale[scale == 0.0] = 1.0
    return mean, scale


def normalize_apply(xs, mean, scale):
    return (xs - mean) / scale


def sigmoid(z):
    return 1.0 / (1.0 + np.exp(-np.clip(z, -40.0, 40.0)))


def train_logistic_l2(x_train, y_train, x_val, y_val):
    candidates = []
    class_counts = np.bincount(y_train, minlength=2)
    total = len(y_train)
    class_weights = total / (2.0 * np.maximum(class_counts, 1))
    sample_weights = np.where(y_train == 0, class_weights[0], class_weights[1])

    for reg in (0.01, 0.1, 1.0):
        for lr in (0.03, 0.05):
            w = np.zeros(x_train.shape[1], dtype=np.float64)
            b = 0.0
            for _ in range(250):
                logits = x_train @ w + b
                probs = sigmoid(logits)
                error = (probs - y_train) * sample_weights
                grad_w = (x_train.T @ error) / len(x_train) + reg * w
                grad_b = float(np.mean(error))
                w -= lr * grad_w
                b -= lr * grad_b
            val_pred = (sigmoid(x_val @ w + b) >= 0.5).astype(np.int64)
            candidates.append({
                "reg": reg,
                "lr": lr,
                "weights": w.copy(),
                "bias": b,
                "val_accuracy": accuracy(y_val, val_pred),
                "val_macro_f1": macro_f1(y_val, val_pred),
            })
    return max(candidates, key=lambda row: (row["val_macro_f1"], row["val_accuracy"]))


def export_linear_model(path, feature_names, mean, scale, weights, bias):
    full_mean = np.zeros(len(FEATURE_NAMES), dtype=np.float64)
    full_scale = np.ones(len(FEATURE_NAMES), dtype=np.float64)
    full_down = np.zeros(len(FEATURE_NAMES), dtype=np.float64)
    full_up = np.zeros(len(FEATURE_NAMES), dtype=np.float64)
    for idx, name in enumerate(feature_names):
        global_idx = FEATURE_NAMES.index(name)
        full_mean[global_idx] = mean[idx]
        full_scale[global_idx] = scale[idx]
        full_down[global_idx] = -weights[idx]
        full_up[global_idx] = weights[idx]

    with open(path, "w", newline="") as handle:
        writer = csv.writer(handle)
        writer.writerow(["task", "binary"])
        writer.writerow(full_mean.tolist())
        writer.writerow(full_scale.tolist())
        writer.writerow([-bias, bias])
        writer.writerow(full_down.tolist())
        writer.writerow(full_up.tolist())


def heuristic_predict_matrix(xs, names):
    idx = {name: names.index(name) for name in names}
    imbalance_l1 = xs[:, idx["imbalance_l1"]]
    imbalance_l3 = xs[:, idx["imbalance_l3"]]
    order_flow = xs[:, idx["order_flow_imbalance"]]
    score = (0.75 * imbalance_l1) + (0.5 * imbalance_l3) + (0.05 * order_flow)
    return (score >= 0.0).astype(np.int64)


def standard_report(name, y_true, y_pred, latency_metrics, extra=None):
    payload = {
        "name": name,
        "accuracy": accuracy(y_true, y_pred),
        "macro_f1": macro_f1(y_true, y_pred),
        "confusion_matrix": confusion_matrix(y_true, y_pred),
        "labels": ["down", "up"],
    }
    payload.update(latency_metrics)
    if extra:
        payload.update(extra)
    return payload


def main():
    parser = argparse.ArgumentParser(description="Run a standard train/val/test inference pipeline on exported LOB features.")
    parser.add_argument("--input", required=True)
    parser.add_argument("--json-output", required=True)
    parser.add_argument("--linear-output", required=True)
    parser.add_argument("--xgb-output", required=True)
    args = parser.parse_args()

    xs, ys = load_binary_rows(args.input)
    splits = split_time_series(xs, ys)
    x_train_full, y_train = splits["train"]
    x_val_full, y_val = splits["val"]
    x_test_full, y_test = splits["test"]

    heuristic_names = FEATURE_SETS["pressure_core"]
    heuristic_idx = feature_indices(heuristic_names)
    heuristic_test = x_test_full[:, heuristic_idx]
    heuristic_pred = heuristic_predict_matrix(heuristic_test, heuristic_names)
    heuristic_latency = time_predictor(
        lambda row: int((0.75 * row[heuristic_names.index("imbalance_l1")]) + (0.5 * row[heuristic_names.index("imbalance_l3")]) + (0.05 * row[heuristic_names.index("order_flow_imbalance")]) >= 0.0),
        heuristic_test,
    )
    reports = [standard_report("heuristic_imbalance", y_test, heuristic_pred, heuristic_latency, {"feature_set": "pressure_core"})]

    majority_value = int(np.bincount(y_train).argmax())
    reports.append(
        standard_report(
            "majority_class",
            y_test,
            np.full_like(y_test, majority_value),
            time_predictor(lambda _row: majority_value, x_test_full[:, :1]),
            {"feature_set": "none"},
        )
    )

    best_logistic = None
    best_xgb = None

    for feature_set_name, feature_names in FEATURE_SETS.items():
        idx = feature_indices(feature_names)
        x_train = x_train_full[:, idx]
        x_val = x_val_full[:, idx]
        x_test = x_test_full[:, idx]

        mean, scale = normalize_fit(x_train)
        x_train_norm = normalize_apply(x_train, mean, scale)
        x_val_norm = normalize_apply(x_val, mean, scale)
        x_test_norm = normalize_apply(x_test, mean, scale)

        logistic_candidate = train_logistic_l2(x_train_norm, y_train, x_val_norm, y_val)
        if best_logistic is None or (
            logistic_candidate["val_macro_f1"],
            logistic_candidate["val_accuracy"],
        ) > (
            best_logistic["val_macro_f1"],
            best_logistic["val_accuracy"],
        ):
            best_logistic = {
                **logistic_candidate,
                "feature_set": feature_set_name,
                "feature_names": feature_names,
                "mean": mean,
                "scale": scale,
                "x_test_norm": x_test_norm,
            }

        if xgb is not None:
            dtrain = xgb.DMatrix(x_train, label=y_train, feature_names=feature_names)
            dval = xgb.DMatrix(x_val, label=y_val, feature_names=feature_names)
            dtest = xgb.DMatrix(x_test, label=y_test, feature_names=feature_names)
            for max_depth in (3, 4):
                for eta in (0.05, 0.1):
                    params = {
                        "objective": "binary:logistic",
                        "eval_metric": "logloss",
                        "max_depth": max_depth,
                        "eta": eta,
                        "subsample": 0.8,
                        "colsample_bytree": 0.8,
                        "min_child_weight": 4,
                    }
                    booster = xgb.train(
                        params,
                        dtrain,
                        num_boost_round=200,
                        evals=[(dval, "val")],
                        early_stopping_rounds=20,
                        verbose_eval=False,
                    )
                    val_prob = booster.inplace_predict(x_val)
                    val_pred = (val_prob >= 0.5).astype(np.int64)
                    candidate = {
                        "booster": booster,
                        "feature_set": feature_set_name,
                        "feature_names": feature_names,
                        "x_test": x_test,
                        "val_accuracy": accuracy(y_val, val_pred),
                        "val_macro_f1": macro_f1(y_val, val_pred),
                    }
                    if best_xgb is None or (
                        candidate["val_macro_f1"],
                        candidate["val_accuracy"],
                    ) > (
                        best_xgb["val_macro_f1"],
                        best_xgb["val_accuracy"],
                    ):
                        best_xgb = candidate

    if best_logistic is None:
        raise SystemExit("logistic training failed")

    logistic_pred = (sigmoid(best_logistic["x_test_norm"] @ best_logistic["weights"] + best_logistic["bias"]) >= 0.5).astype(np.int64)
    logistic_latency = time_predictor(
        lambda row: int(sigmoid(np.dot(row, best_logistic["weights"]) + best_logistic["bias"]) >= 0.5),
        best_logistic["x_test_norm"],
    )
    reports.append(
        standard_report(
            "logistic_regression",
            y_test,
            logistic_pred,
            logistic_latency,
            {
                "feature_set": best_logistic["feature_set"],
                "validation_accuracy": best_logistic["val_accuracy"],
                "validation_macro_f1": best_logistic["val_macro_f1"],
            },
        )
    )
    export_linear_model(
        args.linear_output,
        best_logistic["feature_names"],
        best_logistic["mean"],
        best_logistic["scale"],
        best_logistic["weights"],
        best_logistic["bias"],
    )

    if best_xgb is not None:
        xgb_prob = best_xgb["booster"].inplace_predict(best_xgb["x_test"])
        xgb_pred = (xgb_prob >= 0.5).astype(np.int64)
        xgb_latency = time_predictor(
            lambda row: int(best_xgb["booster"].inplace_predict(np.asarray(row, dtype=np.float32).reshape(1, -1))[0] >= 0.5),
            best_xgb["x_test"],
        )
        reports.append(
            standard_report(
                "xgboost",
                y_test,
                xgb_pred,
                xgb_latency,
                {
                    "feature_set": best_xgb["feature_set"],
                    "validation_accuracy": best_xgb["val_accuracy"],
                    "validation_macro_f1": best_xgb["val_macro_f1"],
                },
            )
        )
        best_xgb["booster"].save_model(args.xgb_output)

    reports.sort(key=lambda row: (row["macro_f1"], row["accuracy"]), reverse=True)
    best_model = reports[0]

    payload = {
        "input": args.input,
        "task": "binary",
        "target_name": "threshold_h25_t100",
        "label_mode": "thresholded_horizon",
        "horizon_events": 25,
        "move_threshold": 100,
        "split": {
            "train_rows": int(len(y_train)),
            "val_rows": int(len(y_val)),
            "test_rows": int(len(y_test)),
        },
        "selection_metric": "validation_macro_f1_then_accuracy",
        "reports": reports,
        "best_model": best_model["name"],
        "deployable_recommendation": "heuristic_imbalance" if best_model["name"] != "logistic_regression" else "logistic_regression",
    }
    Path(args.json_output).write_text(json.dumps(payload, indent=2))
    print(json.dumps({
        "best_model": payload["best_model"],
        "deployable_recommendation": payload["deployable_recommendation"],
        "reports": [{k: row[k] for k in ("name", "feature_set", "accuracy", "macro_f1", "inference_p50_ns", "inference_p99_ns")} for row in reports],
    }, indent=2))


if __name__ == "__main__":
    main()
