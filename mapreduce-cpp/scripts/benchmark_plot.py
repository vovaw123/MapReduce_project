import argparse
import random
import statistics
import string
import subprocess
from pathlib import Path

import matplotlib.pyplot as plt


def random_letters(length: int) -> str:
    return "".join(random.choices(string.ascii_letters, k=length))


def random_alpha_num(length: int) -> str:
    alphabet = string.ascii_letters + string.digits
    value = "".join(random.choices(alphabet, k=length))
    if not any(ch.isdigit() for ch in value):
        pos = random.randrange(len(value))
        value = value[:pos] + random.choice(string.digits) + value[pos + 1 :]
    return value


def generate_input(path: Path, count: int, seed: int | None) -> None:
    if seed is not None:
        random.seed(seed)

    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8") as handle:
        for _ in range(count):
            length = random.randint(6, 16)
            if random.random() < 0.5:
                handle.write(random_letters(length) + "\n")
            else:
                handle.write(random_alpha_num(length) + "\n")


def parse_time_us(stdout: str) -> int:
    for line in stdout.splitlines():
        if line.startswith("time_us="):
            return int(line.split("=", 1)[1].strip())
    raise RuntimeError("time_us not found in program output")


def run_once(exe: Path, input_file: Path, mode: str, threads: int | None = None) -> int:
    command = [
        str(exe),
        "--input",
        str(input_file),
        "--mode",
        mode,
    ]
    if threads is not None:
        command.extend(["--threads", str(threads)])
    result = subprocess.run(command, capture_output=True, text=True, check=True)
    return parse_time_us(result.stdout)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--exe", default="build/mapreduce_demo")
    parser.add_argument("--input", default="data/input.txt")
    parser.add_argument("--count", type=int, default=200000)
    parser.add_argument("--seed", type=int)
    parser.add_argument("--runs", type=int, default=5)
    parser.add_argument("--max-threads", type=int, default=4)
    parser.add_argument("--output", default="data/benchmark.png")
    args = parser.parse_args()

    exe = Path(args.exe)
    input_file = Path(args.input)
    if not exe.exists():
        raise FileNotFoundError(f"Executable not found: {exe}")
    if args.max_threads < 1:
        raise ValueError("--max-threads must be >= 1")

    generate_input(input_file, args.count, args.seed)
    seed_text = args.seed if args.seed is not None else "system-random"
    print(f"generated fresh input: {input_file} (count={args.count}, seed={seed_text})")

    x_threads = list(range(1, args.max_threads + 1))

    serial_times = [run_once(exe, input_file, "serial") for _ in range(args.runs)]
    serial_mean = statistics.mean(serial_times)
    serial_curve = [serial_mean for _ in x_threads]

    threaded_means = []
    for thread_count in x_threads:
        samples = [run_once(exe, input_file, "threads", thread_count) for _ in range(args.runs)]
        threaded_means.append(statistics.mean(samples))
        print(f"threads={thread_count}: {samples}")

    plt.figure(figsize=(6, 4))
    plt.plot(x_threads, serial_curve, marker="o", label="serial")
    plt.plot(x_threads, threaded_means, marker="o", label="threads")
    plt.xticks(x_threads)
    plt.xlabel("Thread count")
    plt.ylabel("Time (microseconds)")
    plt.title("MapReduce: serial baseline vs threads 1..N")
    plt.legend()
    plt.grid(True, axis="y", alpha=0.3)
    plt.tight_layout()

    output = Path(args.output)
    output.parent.mkdir(parents=True, exist_ok=True)
    plt.savefig(output)

    print(f"serial times:   {serial_times}")
    print(f"Saved chart to: {output}")


if __name__ == "__main__":
    main()
