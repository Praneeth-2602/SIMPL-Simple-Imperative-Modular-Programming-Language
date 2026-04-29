#!/usr/bin/env python3
from __future__ import annotations

import argparse
import csv
import itertools
import os
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SIMPL_CANDIDATES = (ROOT / "simpl.exe", ROOT / "simpl")
SCRATCH_DIR = ROOT / ".tmp_rl_corpus"
SCRATCH_COUNTER = itertools.count()

LOOPS = ("none", "shallow", "deep")
ARITH = ("none", "few", "many")
ADT = ("none", "light", "dominated")
CONST = ("no", "has")
SIZE = ("tiny", "medium", "large")
SIZE_ORDER = {name: idx for idx, name in enumerate(SIZE)}

ARITH_OPS_BY_BUCKET = {
    "none": 0,
    "few": 2,
    "many": 6,
}

VARIANT_COUNT_DEFAULT = 3

ARITH_SEQUENCES = (
    (
        "{a} = {b} + {c}",
        "{b} = {a} - {c}",
        "{c} = {a} + {b}",
        "{a} = {c} - {b}",
        "{b} = {a} + {c}",
        "{c} = {b} - {a}",
    ),
    (
        "{a} = {a} + {b}",
        "{b} = {b} - {c}",
        "{c} = {a} + {c}",
        "{a} = {c} - {b}",
        "{b} = {a} + {b}",
        "{c} = {c} - {a}",
    ),
    (
        "{a} = {b} + {a}",
        "{b} = {a} - {b}",
        "{c} = {c} + {a}",
        "{a} = {c} - {b}",
        "{b} = {b} + {c}",
        "{c} = {a} - {c}",
    ),
)


@dataclass(frozen=True)
class State:
    loop: str
    arith: str
    adt: str
    const: str
    size: str

    def key(self) -> str:
        return (
            f"{self.loop}_{self.arith}_{self.adt}_{self.const}_{self.size}"
        )


@dataclass(frozen=True)
class CorpusSpec:
    label: str
    out_dir: Path
    manifest: Path
    variants_per_state: int
    file_prefix: str


def resolve_simpl_binary() -> Path | None:
    for candidate in SIMPL_CANDIDATES:
        if candidate.exists():
            return candidate
    return None


def run_checked(cmd: list[str]) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        cmd,
        cwd=ROOT,
        text=True,
        capture_output=True,
        check=False,
    )


def state_from_train_output(text: str) -> State | None:
    marker = "State:"
    for line in text.splitlines():
        if marker not in line:
            continue
        fields = {}
        for token in line.split():
            if "=" not in token:
                continue
            key, value = token.split("=", 1)
            fields[key.rstrip(":")] = value
        try:
            return State(
                loop=LOOPS[int(fields["loop"])],
                arith=ARITH[int(fields["arith"])],
                adt=ADT[int(fields["adt"])],
                const=CONST[int(fields["const"])],
                size=SIZE[int(fields["size"])],
            )
        except (KeyError, IndexError, ValueError):
            return None
    return None


def impossible_by_definition(target: State) -> str | None:
    if target.const == "has" and target.arith == "none":
        return "const=has requires at least one arithmetic binop in semantic.c"
    if target.adt == "light" and target.arith == "none":
        return "adt=light requires adt_ops>0 and adt_ops<=arithmetic_ops"
    return None


def choose_adt_kind(target: State, variant: int) -> str:
    if target.adt == "none":
        return "stack"
    kinds = ("stack", "queue", "tree", "graph")
    return kinds[variant % len(kinds)]


def adt_decl(kind: str, name: str) -> str:
    return f"let {name} be {kind}"


def adt_statements(kind: str, name: str, count: int, seed: int) -> list[str]:
    statements: list[str] = []
    if kind == "stack":
        for idx in range(count):
            if idx % 3 == 2:
                statements.append(f"pop {name}")
            else:
                statements.append(f"push {name} {seed + idx + 1}")
    elif kind == "queue":
        for idx in range(count):
            if idx % 3 == 2:
                statements.append(f"dequeue {name}")
            else:
                statements.append(f"enqueue {name} {seed + idx + 1}")
    elif kind == "tree":
        live_values: list[int] = []
        for idx in range(count):
            value = seed + idx + 2
            if idx % 3 == 2 and live_values:
                remove_value = live_values.pop(0)
                statements.append(f"remove {name} {remove_value}")
            else:
                statements.append(f"insert {name} {value}")
                live_values.append(value)
    else:
        live_edges: list[tuple[int, int]] = []
        for idx in range(count):
            left = seed + idx + 1
            right = left + 1
            if idx % 3 == 2 and live_edges:
                remove_left, remove_right = live_edges.pop(0)
                statements.append(
                    f"remove_edge {name} {remove_left} {remove_right}"
                )
            else:
                statements.append(f"add_edge {name} {left} {right}")
                live_edges.append((left, right))
    return statements


def interleave(left: list[str], right: list[str]) -> list[str]:
    out: list[str] = []
    for idx in range(max(len(left), len(right))):
        if idx < len(left):
            out.append(left[idx])
        if idx < len(right):
            out.append(right[idx])
    return out


def pad_split(padding: int, variant: int) -> tuple[int, int]:
    if variant % 3 == 0:
        return padding, 0
    if variant % 3 == 1:
        before = padding // 2
        return before, padding - before
    return 0, padding


def body_for_state(target: State, variant: int) -> tuple[list[str], list[str]]:
    seed = 3 + (variant * 5)
    prefix: list[str] = []
    arith_body: list[str] = []
    adt_body: list[str] = []

    if target.arith != "none":
        names = {
            "a": f"a{variant}",
            "b": f"b{variant}",
            "c": f"c{variant}",
        }
        prefix.extend(
            [
                f"let {names['a']} be {seed + 1}",
                f"let {names['b']} be {seed + 2}",
                f"let {names['c']} be {seed + 3}",
            ]
        )

        sequence = ARITH_SEQUENCES[variant % len(ARITH_SEQUENCES)]
        arith_needed = ARITH_OPS_BY_BUCKET[target.arith]
        if target.const == "has":
            arith_body.append(f"let k{variant} be {seed} + {seed + 1}")
            arith_needed -= 1
        for template in sequence[:arith_needed]:
            rendered = template.format(**names)
            lhs, rhs = rendered.split(" = ", 1)
            arith_body.append(f"set {lhs} to {rhs}")
    else:
        prefix.append(f"let seed{variant} be {seed}")

    if target.adt != "none":
        kind = choose_adt_kind(target, variant)
        adt_name = f"{kind[0]}{variant}"
        prefix.append(adt_decl(kind, adt_name))
        adt_count = (
            1
            if target.adt == "light"
            else max(ARITH_OPS_BY_BUCKET[target.arith] + 1, 1)
        )
        adt_body = adt_statements(kind, adt_name, adt_count, seed)

    if target.adt == "none":
        body = arith_body
    elif target.arith == "none":
        body = adt_body
    elif variant % 3 == 0:
        body = arith_body + adt_body
    elif variant % 3 == 1:
        body = interleave(arith_body, adt_body)
    else:
        body = adt_body + arith_body

    if not body:
        body = [f"print seed{variant}"]

    return prefix, body


def build_program(target: State, padding: int, variant: int) -> str:
    lines: list[str] = []
    prefix, body = body_for_state(target, variant)
    before_padding, after_padding = pad_split(padding, variant)

    if target.loop in ("shallow", "deep"):
        lines.append(f"let i{variant} be 1")
    if target.loop == "deep":
        lines.append(f"let j{variant} be 1")

    lines.extend(prefix)

    for idx in range(before_padding):
        lines.append(f"let pad{variant}_{idx} be {variant * 100 + idx + 10}")

    if target.loop == "none":
        lines.extend(body)
    elif target.loop == "shallow":
        lines.append(f"while i{variant} > 0 do")
        lines.extend(f"    {stmt}" for stmt in body)
        lines.append(f"    set i{variant} to 0")
        lines.append("end")
    else:
        lines.append(f"while i{variant} > 0 do")
        lines.append(f"    while j{variant} > 0 do")
        lines.extend(f"        {stmt}" for stmt in body)
        lines.append(f"        set j{variant} to 0")
        lines.append("    end")
        lines.append(f"    set i{variant} to 0")
        lines.append("end")

    for idx in range(after_padding):
        pad_idx = before_padding + idx
        lines.append(f"let pad{variant}_{pad_idx} be {variant * 100 + pad_idx + 10}")

    return "\n".join(lines) + "\n"


def observed_state(simpl_bin: Path, program_text: str) -> State | None:
    SCRATCH_DIR.mkdir(exist_ok=True)
    token = f"{os.getpid()}_{next(SCRATCH_COUNTER)}"
    program = SCRATCH_DIR / f"candidate_{token}.simpl"
    qtable = SCRATCH_DIR / f"candidate_{token}.bin"
    try:
        program.write_text(program_text, encoding="utf-8")
        train = run_checked(
            [str(simpl_bin), str(program), "--train", "--qtable", str(qtable), "--quiet"]
        )
        if train.returncode != 0:
            return None
        return state_from_train_output(train.stdout)
    finally:
        if program.exists():
            program.unlink()
        if qtable.exists():
            qtable.unlink()


def matches_non_size_dimensions(actual: State, target: State) -> bool:
    return (
        actual.loop == target.loop
        and actual.arith == target.arith
        and actual.adt == target.adt
        and actual.const == target.const
    )


def generate_verified_program(
    simpl_bin: Path,
    target: State,
    variant: int,
) -> tuple[str | None, State | None, str]:
    definition_issue = impossible_by_definition(target)
    if definition_issue:
        return None, None, definition_issue

    cache: dict[int, State | None] = {}

    def observe(padding: int) -> State | None:
        if padding not in cache:
            cache[padding] = observed_state(
                simpl_bin, build_program(target, padding, variant)
            )
        return cache[padding]

    baseline_actual = observe(0)
    if baseline_actual is None:
        return None, None, "candidate failed to parse or train"
    if not matches_non_size_dimensions(baseline_actual, target):
        return None, None, "variant drifts outside the target non-size dimensions"
    if baseline_actual == target:
        return build_program(target, 0, variant), baseline_actual, "verified"

    if SIZE_ORDER[baseline_actual.size] > SIZE_ORDER[target.size]:
        return (
            None,
            None,
            "minimum IR for this variant already exceeds the requested size bucket",
        )

    low_pad = 0
    high_pad = 1
    while high_pad <= 256:
        actual = observe(high_pad)
        if actual is None:
            return None, None, "candidate failed to parse or train"
        if not matches_non_size_dimensions(actual, target):
            return None, None, "padding changed non-size dimensions unexpectedly"
        if SIZE_ORDER[actual.size] >= SIZE_ORDER[target.size]:
            break
        low_pad = high_pad
        high_pad *= 2
    else:
        return None, None, "no verified program found for this target size bucket"

    for padding in range(low_pad + 1, high_pad + 1):
        actual = observe(padding)
        if actual == target:
            return build_program(target, padding, variant), actual, "verified"

    return None, None, "no verified program found for this target state"


def emit_corpus(spec: CorpusSpec, simpl_bin: Path) -> tuple[int, int]:
    spec.out_dir.mkdir(parents=True, exist_ok=True)

    for stale in spec.out_dir.glob("*.simpl"):
        stale.unlink()
    if spec.manifest.exists():
        spec.manifest.unlink()

    targets = [
        State(*combo)
        for combo in itertools.product(LOOPS, ARITH, ADT, CONST, SIZE)
    ]

    manifest_rows: list[dict[str, str]] = []
    emitted = 0
    reachable_states: set[str] = set()

    for target in targets:
        for variant in range(spec.variants_per_state):
            program, actual, status = generate_verified_program(
                simpl_bin, target, variant
            )
            file_name = ""
            actual_loop = ""
            actual_arith = ""
            actual_adt = ""
            actual_const = ""
            actual_size = ""

            if program and actual:
                file_name = (
                    f"{spec.file_prefix}_{target.key()}_v{variant + 1:02d}.simpl"
                )
                out_file = spec.out_dir / file_name
                out_file.write_text(program, encoding="utf-8")
                actual_loop = actual.loop
                actual_arith = actual.arith
                actual_adt = actual.adt
                actual_const = actual.const
                actual_size = actual.size
                emitted += 1
                reachable_states.add(target.key())

            manifest_rows.append(
                {
                    "target_loop": target.loop,
                    "target_arith": target.arith,
                    "target_adt": target.adt,
                    "target_const": target.const,
                    "target_size": target.size,
                    "variant": str(variant + 1),
                    "status": status,
                    "actual_loop": actual_loop,
                    "actual_arith": actual_arith,
                    "actual_adt": actual_adt,
                    "actual_const": actual_const,
                    "actual_size": actual_size,
                    "file": file_name,
                }
            )

    with spec.manifest.open("w", newline="", encoding="utf-8") as fh:
        writer = csv.DictWriter(
            fh,
            fieldnames=[
                "target_loop",
                "target_arith",
                "target_adt",
                "target_const",
                "target_size",
                "variant",
                "status",
                "actual_loop",
                "actual_arith",
                "actual_adt",
                "actual_const",
                "actual_size",
                "file",
            ],
        )
        writer.writeheader()
        writer.writerows(manifest_rows)

    return emitted, len(reachable_states)


def build_specs(args: argparse.Namespace) -> list[CorpusSpec]:
    specs: list[CorpusSpec] = []
    if args.mode in {"train", "both"}:
        train_dir = ROOT / args.train_dir
        specs.append(
            CorpusSpec(
                label="train",
                out_dir=train_dir,
                manifest=train_dir / "coverage_manifest.csv",
                variants_per_state=args.train_variants,
                file_prefix="train",
            )
        )
    if args.mode in {"eval", "both"}:
        eval_dir = ROOT / args.eval_dir
        specs.append(
            CorpusSpec(
                label="eval",
                out_dir=eval_dir,
                manifest=eval_dir / "coverage_manifest.csv",
                variants_per_state=args.eval_variants,
                file_prefix="eval",
            )
        )
    return specs


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Generate verified SIMPL RL training/eval corpora."
    )
    parser.add_argument(
        "--mode",
        choices=("train", "eval", "both"),
        default="both",
        help="Which corpora to generate.",
    )
    parser.add_argument(
        "--train-dir",
        default="tests/train_files",
        help="Training corpus output directory, relative to repo root.",
    )
    parser.add_argument(
        "--eval-dir",
        default="eval_files",
        help="Evaluation corpus output directory, relative to repo root.",
    )
    parser.add_argument(
        "--train-variants",
        type=int,
        default=VARIANT_COUNT_DEFAULT,
        help="Verified source variants to emit per reachable training state.",
    )
    parser.add_argument(
        "--eval-variants",
        type=int,
        default=1,
        help="Verified source variants to emit per reachable eval state.",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    simpl_bin = resolve_simpl_binary()
    if simpl_bin is None:
        searched = ", ".join(str(path) for path in SIMPL_CANDIDATES)
        print(f"error: missing compiler binary; searched {searched}", file=sys.stderr)
        return 1

    for spec in build_specs(args):
        emitted, reachable = emit_corpus(spec, simpl_bin)
        print(f"{spec.label}_files={emitted}")
        print(f"{spec.label}_reachable_states={reachable}")
        print(f"{spec.label}_manifest={spec.manifest}")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
