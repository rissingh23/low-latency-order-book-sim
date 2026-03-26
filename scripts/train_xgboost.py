#!/usr/bin/env python3
import argparse
import csv
import json
import sys

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


def load_rows(path):
    with open(path, newline="") as handle:
        reader = csv.DictReader(handle)
        xs, ys = [], []
        for row in reader:
            label = LABEL_TO_INDEX[row["label"]]
            if label == LABEL_TO_INDEX["flat"]:
                continue
            xs.append([float(row[name]) for name in FEATURE_NAMES])
            ys.append(0 if label == LABEL_TO_INDEX["down"] else 1)
        return xs, ys


def main():
    parser = argparse.ArgumentParser(description="Train an XGBoost classifier on exported LOB features.")
    parser.add_argument("--input", required=True)
    parser.add_argument("--output", required=True)
    parser.add_argument("--rounds", type=int, default=50)
    args = parser.parse_args()

    if xgb is None:
        print("xgboost is not installed in this environment. Run this script on a machine with xgboost available.", file=sys.stderr)
        raise SystemExit(1)

    xs, ys = load_rows(args.input)
    split = max(1, int(len(xs) * 0.8))
    dtrain = xgb.DMatrix(xs[:split], label=ys[:split], feature_names=FEATURE_NAMES)
    dtest = xgb.DMatrix(xs[split:], label=ys[split:], feature_names=FEATURE_NAMES)
    params = {
        "objective": "binary:logistic",
        "eval_metric": "logloss",
        "max_depth": 3,
        "eta": 0.1,
        "subsample": 0.8,
        "colsample_bytree": 0.8,
    }
    booster = xgb.train(params, dtrain, num_boost_round=args.rounds, evals=[(dtest, "test")], verbose_eval=False)
    booster.save_model(args.output)
    print(json.dumps({"output": args.output, "rounds": args.rounds, "test_rows": len(xs) - split}, indent=2))


if __name__ == "__main__":
    main()
