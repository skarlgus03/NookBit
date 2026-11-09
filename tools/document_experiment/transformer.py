"""Run the E5 document comparison with local model and tokenizer files."""

import argparse
import csv
import ctypes
from ctypes import wintypes
import json
from pathlib import Path
from time import perf_counter, process_time

import numpy as np
import onnxruntime as ort
from tokenizers import Tokenizer


class ProcessMemoryCounters(ctypes.Structure):
    _fields_ = [
        ("size", wintypes.DWORD),
        ("page_faults", wintypes.DWORD),
        ("peak_working_set", ctypes.c_size_t),
        ("working_set", ctypes.c_size_t),
        ("peak_paged_pool", ctypes.c_size_t),
        ("paged_pool", ctypes.c_size_t),
        ("peak_nonpaged_pool", ctypes.c_size_t),
        ("nonpaged_pool", ctypes.c_size_t),
        ("pagefile", ctypes.c_size_t),
        ("peak_pagefile", ctypes.c_size_t),
    ]


def peak_working_set_mib():
    """Read the Windows peak resident memory for this experiment process."""
    kernel = ctypes.WinDLL("kernel32", use_last_error=True)
    kernel.GetCurrentProcess.restype = wintypes.HANDLE
    read_counters = kernel.K32GetProcessMemoryInfo
    read_counters.argtypes = (wintypes.HANDLE, ctypes.POINTER(ProcessMemoryCounters), wintypes.DWORD)
    read_counters.restype = wintypes.BOOL
    counters = ProcessMemoryCounters()
    counters.size = ctypes.sizeof(counters)
    if not read_counters(kernel.GetCurrentProcess(), ctypes.byref(counters), counters.size):
        raise ctypes.WinError(ctypes.get_last_error())
    return round(counters.peak_working_set / 2**20, 2)


def read_documents(manifest, config, mask_filenames):
    """Build model inputs from the same filename and body excerpt as the C++ tool."""
    folders = {profile["folder"] for profile in config["candidates"]}
    documents = []
    for index, row in enumerate(config["documents"], 1):
        source = manifest.parent / row["path"]
        if source.suffix.lower() not in {".txt", ".md"}:
            raise ValueError(f"Unsupported document type: {source.suffix}")
        expected = row["expected"]
        if expected is not None and expected not in folders:
            raise ValueError(f"Unknown expected folder: {expected}")
        body = row["text"] if "text" in row else source.read_bytes().decode("utf-8-sig")
        excerpt = body.encode("utf-16-le")[:8000].decode("utf-16-le", errors="ignore")
        filename = f"sample-{index:02}{source.suffix}" if mask_filenames else source.name
        documents.append((filename, expected, f"{filename}\n{excerpt}"))
    return documents


def embed(texts, tokenizer, session):
    """Create normalized E5 vectors using attention-mask mean pooling."""
    encoded = tokenizer.encode_batch(texts)
    values = {
        "input_ids": [item.ids for item in encoded],
        "attention_mask": [item.attention_mask for item in encoded],
        "token_type_ids": [item.type_ids for item in encoded],
    }
    feed = {item.name: np.asarray(values[item.name], dtype=np.int64) for item in session.get_inputs()}
    hidden, = session.run(["last_hidden_state"], feed)
    mask = np.asarray(values["attention_mask"], dtype=np.float32)[..., None]
    pooled = (hidden * mask).sum(axis=1) / mask.sum(axis=1)
    return pooled / np.linalg.norm(pooled, axis=1, keepdims=True)


def run(args):
    """Rank the candidate folders and report cold setup and per-document timings."""
    started = perf_counter()
    cpu_started = process_time()
    config = json.loads(args.manifest.read_text(encoding="utf-8-sig"))
    profiles = config["candidates"]
    folders = [profile["folder"] for profile in profiles]
    documents = read_documents(args.manifest, config, args.mask_filenames)
    if not folders or len(set(folders)) != len(folders) or not documents:
        raise ValueError("The manifest needs unique candidate folders and at least one document.")
    loaded_input = perf_counter()

    tokenizer = Tokenizer.from_file(str(args.model_dir / "tokenizer.json"))
    tokenizer.enable_truncation(max_length=512)
    tokenizer.enable_padding(pad_id=1, pad_token="<pad>")
    options = ort.SessionOptions()
    options.intra_op_num_threads = args.threads
    options.add_session_config_entry("session.intra_op.allow_spinning", "0")
    session = ort.InferenceSession(
        str(args.model_dir / "model.onnx"), sess_options=options, providers=["CPUExecutionProvider"]
    )
    loaded_model = perf_counter()
    vectors = embed(
        [f"passage: {profile['folder']} {profile['description']}" for profile in profiles],
        tokenizer, session,
    )
    prepared_candidates = perf_counter()
    rows = []
    for filename, expected, document in documents:
        prediction_started = perf_counter()
        scores = vectors @ embed([f"query: {document}"], tokenizer, session)[0]
        order = np.argsort(-scores, kind="stable")[:3]
        ranked = [folders[index] for index in order if scores[index] >= args.min_score]
        elapsed = (perf_counter() - prediction_started) * 1000
        first = ranked[0] if ranked else None
        top3 = expected in ranked if expected is not None else not ranked
        rows.append({
            "file": filename, "expected": expected, "top1": first,
            "top2": ranked[1] if len(ranked) > 1 else None,
            "top3": ranked[2] if len(ranked) > 2 else None,
            "best_score": round(float(scores[order[0]]), 6),
            "correct": int(first == expected), "top3_correct": int(top3),
            "prediction_ms": round(elapsed, 4),
        })
    finished = perf_counter()
    with args.csv.open("x", encoding="utf-8-sig", newline="") as output:
        writer = csv.DictWriter(output, fieldnames=list(rows[0]))
        writer.writeheader()
        writer.writerows(rows)
    return {
        "documents": len(rows), "threads": args.threads, "min_score": args.min_score,
        "runtime_version": ort.__version__, "top1": sum(row["correct"] for row in rows),
        "top3": sum(row["top3_correct"] for row in rows),
        "suggested": sum(row["top1"] is not None for row in rows),
        "input_ms": round((loaded_input - started) * 1000, 2),
        "model_setup_ms": round((loaded_model - loaded_input) * 1000, 2),
        "candidate_ms": round((prepared_candidates - loaded_model) * 1000, 2),
        "prediction_ms": round((finished - prepared_candidates) * 1000, 2),
        "total_ms": round((finished - started) * 1000, 2),
        "process_cpu_ms": round((process_time() - cpu_started) * 1000, 2),
        "peak_working_set_mib": peak_working_set_mib(),
    }


def main():
    """Parse an explicit local benchmark invocation and print the run summary."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("manifest", type=Path)
    parser.add_argument("model_dir", type=Path)
    parser.add_argument("csv", type=Path)
    parser.add_argument("--threads", type=int, default=4)
    parser.add_argument("--min-score", type=float, default=-1.0)
    parser.add_argument("--mask-filenames", action="store_true")
    args = parser.parse_args()
    if args.threads < 1 or not -1.0 <= args.min_score <= 1.0:
        parser.error("threads must be positive and min-score must be between -1 and 1")
    print(json.dumps(run(args)))


if __name__ == "__main__":
    main()
