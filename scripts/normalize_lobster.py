#!/usr/bin/env python3

import argparse
import csv
from pathlib import Path


def parse_args():
    parser = argparse.ArgumentParser(
        description="Convert a LOBSTER message file into the normalized event CSV used by the simulator."
    )
    parser.add_argument("--input", required=True, help="Path to the LOBSTER message CSV")
    parser.add_argument("--output", required=True, help="Path for the normalized output CSV")
    parser.add_argument("--limit", type=int, default=0, help="Optional max number of input rows to process")
    return parser.parse_args()


def lobster_side(direction_value: str) -> str:
    direction = int(float(direction_value))
    return "buy" if direction == 1 else "sell"


def normalize_row(row, synthetic_order_id):
    timestamp = row[0].strip()
    event_type = int(float(row[1]))
    order_id = row[2].strip()
    size = int(float(row[3]))
    price = int(float(row[4]))
    side = lobster_side(row[5])

    if event_type == 1:
        return {
            "type": "limit",
            "side": side,
            "order_id": order_id,
            "price": price,
            "qty": size,
            "timestamp": timestamp,
        }, synthetic_order_id

    if event_type == 2:
        return {
            "type": "cancel",
            "side": side,
            "order_id": order_id,
            "price": 0,
            "qty": size,
            "timestamp": timestamp,
        }, synthetic_order_id

    if event_type == 3:
        return {
            "type": "cancel",
            "side": side,
            "order_id": order_id,
            "price": 0,
            "qty": 0,
            "timestamp": timestamp,
        }, synthetic_order_id

    if event_type == 4:
        return {
            "type": "market",
            "side": "sell" if side == "buy" else "buy",
            "order_id": synthetic_order_id,
            "price": 0,
            "qty": size,
            "timestamp": timestamp,
        }, synthetic_order_id + 1

    return None, synthetic_order_id


def main():
    args = parse_args()

    input_path = Path(args.input)
    output_path = Path(args.output)
    output_path.parent.mkdir(parents=True, exist_ok=True)

    written = 0
    skipped_hidden = 0
    skipped_other = 0
    synthetic_order_id = 10_000_000_000

    with input_path.open("r", newline="") as infile, output_path.open("w", newline="") as outfile:
        sample = infile.read(2048)
        infile.seek(0)
        try:
            dialect = csv.Sniffer().sniff(sample, delimiters=",\t")
        except csv.Error:
            dialect = csv.excel
        reader = csv.reader(infile, dialect)
        writer = csv.DictWriter(
            outfile,
            fieldnames=["timestamp", "sequence", "type", "side", "order_id", "price", "qty"],
        )
        writer.writeheader()

        for row_index, row in enumerate(reader, start=1):
            if args.limit and row_index > args.limit:
                break
            if not row or all(not cell.strip() for cell in row):
                continue
            if len(row) < 6:
                raise ValueError(f"expected 6 columns in LOBSTER row {row_index}, got {len(row)}")

            event_type = int(float(row[1]))
            if event_type == 5:
                skipped_hidden += 1
                continue
            if event_type not in {1, 2, 3, 4}:
                skipped_other += 1
                continue

            normalized, synthetic_order_id = normalize_row(row, synthetic_order_id)
            if normalized is None:
                continue
            normalized["sequence"] = written + 1
            writer.writerow(normalized)
            written += 1

    print(f"wrote {written} normalized events to {output_path}")
    if skipped_hidden:
        print(f"skipped {skipped_hidden} hidden executions (LOBSTER event type 5)")
    if skipped_other:
        print(f"skipped {skipped_other} unsupported rows (for example halts/cross trades)")


if __name__ == "__main__":
    main()
