import argparse
import random
import shutil
import statistics
import string
import subprocess
from pathlib import Path

import matplotlib.pyplot as plt


def find_tool(names: list[str], required: bool = True) -> str | None:
    for name in names:
        path = shutil.which(name)
        if path:
            return path
    if required:
        raise RuntimeError(f"Required tool not found in PATH: one of {names}")
    return None


def run_cmd(command: list[str], cwd: Path | None = None) -> str:
    try:
        result = subprocess.run(
            command,
            cwd=str(cwd) if cwd else None,
            check=True,
            text=True,
            capture_output=True,
        )
        return result.stdout
    except FileNotFoundError as ex:
        raise RuntimeError(f"Command not found: {command[0]}") from ex
    except subprocess.CalledProcessError as ex:
        msg = [f"Command failed: {' '.join(command)}"]
        if ex.stdout:
            msg.append(f"stdout:\n{ex.stdout}")
        if ex.stderr:
            msg.append(f"stderr:\n{ex.stderr}")
        raise RuntimeError("\n".join(msg)) from ex


def parse_key(stdout: str, key: str) -> int:
    prefix = f"{key}="
    for line in stdout.splitlines():
        if line.startswith(prefix):
            return int(line.split("=", 1)[1].strip())
    raise RuntimeError(f"Key '{key}' not found in output.\nOutput:\n{stdout}")


def random_word(length: int) -> str:
    return "".join(random.choices(string.ascii_lowercase, k=length))


def generate_wordcount_input(path: Path, count: int, seed: int) -> None:
    random.seed(seed)
    vocab = [random_word(random.randint(3, 9)) for _ in range(200)]
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8") as handle:
        for _ in range(count):
            words_per_line = random.randint(4, 10)
            line_words = [random.choice(vocab) for _ in range(words_per_line)]
            handle.write(" ".join(line_words) + "\n")


def configure_build(source_root: Path, build_dir: Path, use_mpi: bool, cmake_bin: str, ninja_bin: str | None) -> None:
    build_dir.mkdir(parents=True, exist_ok=True)
    configure_cmd = [
        cmake_bin,
        "-S",
        str(source_root),
        "-B",
        str(build_dir),
        f"-DMAPREDUCE_USE_MPI={'ON' if use_mpi else 'OFF'}",
        "-DMAPREDUCE_BUILD_TESTS=ON",
        "-DMAPREDUCE_BUILD_DEMO=ON",
    ]
    if ninja_bin:
        configure_cmd.extend(["-G", "Ninja"])
    run_cmd(configure_cmd)
    run_cmd([cmake_bin, "--build", str(build_dir)])


def exe_name(base: str) -> str:
    return f"{base}.exe" if shutil.which("where") else base


def run_demo(exe: Path, input_path: Path, mode: str, threads: int, runs: int) -> float:
    samples: list[int] = []
    for _ in range(runs):
        out = run_cmd(
            [
                str(exe),
                "--input",
                str(input_path),
                "--demo",
                "wordcount",
                "--mode",
                mode,
                "--threads",
                str(threads),
            ]
        )
        samples.append(parse_key(out, "time_us"))
    return float(statistics.mean(samples))


def find_mpi_launcher() -> str:
    launcher = find_tool(["mpiexec", "mpirun"], required=False)
    if launcher is None:
        raise RuntimeError("MPI launcher not found (expected mpiexec or mpirun in PATH).")
    return launcher


def run_mpi_demo(exe: Path, input_path: Path, mpi_ranks: int, runs: int) -> float:
    launcher = find_mpi_launcher()
    samples: list[int] = []
    for _ in range(runs):
        out = run_cmd(
            [
                launcher,
                "-n",
                str(mpi_ranks),
                str(exe),
                "--input",
                str(input_path),
            ]
        )
        samples.append(parse_key(out, "mpi_time_us"))
    return float(statistics.mean(samples))


def plot_no_mpi(out_path: Path, sizes: list[int], serial_us: list[float], threaded_us: list[float], threads: int) -> None:
    plt.figure(figsize=(10, 6))
    plt.plot(sizes, serial_us, marker="o", linewidth=2.8, color="#e63946", label="Serial")
    plt.plot(sizes, threaded_us, marker="s", linewidth=2.8, color="#1d3557", label=f"Threads x{threads}")
    plt.xlabel("Input lines", fontsize=12)
    plt.ylabel("Time (us)", fontsize=12)
    plt.title("No MPI: Serial vs Threaded", fontsize=16, fontweight="bold")
    plt.grid(True, alpha=0.25)
    plt.legend()
    plt.tight_layout()
    out_path.parent.mkdir(parents=True, exist_ok=True)
    plt.savefig(out_path, dpi=160)
    plt.close()


def plot_mpi(out_path: Path, sizes: list[int], threaded_us: list[float], mpi_us: list[float], mpi_ranks: int, threads: int) -> None:
    plt.figure(figsize=(10, 6))
    plt.plot(sizes, threaded_us, marker="s", linewidth=2.8, color="#ff7f11", label=f"Threads x{threads} (no MPI)")
    plt.plot(sizes, mpi_us, marker="^", linewidth=2.8, color="#06d6a0", label=f"MPI x{mpi_ranks}")
    plt.xlabel("Input lines", fontsize=12)
    plt.ylabel("Time (us)", fontsize=12)
    plt.title("With MPI: Threaded baseline vs MPI", fontsize=16, fontweight="bold")
    plt.grid(True, alpha=0.25)
    plt.legend()
    plt.tight_layout()
    out_path.parent.mkdir(parents=True, exist_ok=True)
    plt.savefig(out_path, dpi=160)
    plt.close()


def main() -> None:
    parser = argparse.ArgumentParser(description="Build and benchmark MapReduce without MPI and with MPI.")
    parser.add_argument("--source-root", default=".")
    parser.add_argument("--build-nompi", default="build_nompi")
    parser.add_argument("--build-mpi", default="build_mpi")
    parser.add_argument("--demo-exe", default="", help="Path to prebuilt non-MPI mapreduce_demo executable")
    parser.add_argument("--mpi-exe", default="", help="Path to prebuilt MPI mapreduce_mpi_demo executable")
    parser.add_argument("--cmake-bin", default="", help="Path to cmake executable (optional)")
    parser.add_argument("--skip-build", action="store_true", help="Do not run CMake, use provided/prebuilt executables")
    parser.add_argument("--sizes", default="50000,100000,200000,400000")
    parser.add_argument("--threads", type=int, default=4)
    parser.add_argument("--mpi-ranks", type=int, default=4)
    parser.add_argument("--runs", type=int, default=3)
    parser.add_argument("--seed", type=int, default=42)
    parser.add_argument("--work-dir", default="data/bench")
    parser.add_argument("--out-dir", default="data/plots")
    args = parser.parse_args()

    source_root = Path(args.source_root).resolve()
    build_nompi = Path(args.build_nompi).resolve()
    build_mpi = Path(args.build_mpi).resolve()
    work_dir = Path(args.work_dir).resolve()
    out_dir = Path(args.out_dir).resolve()
    sizes = [int(x.strip()) for x in args.sizes.split(",") if x.strip()]

    cmake_bin = args.cmake_bin.strip() if args.cmake_bin else find_tool(["cmake"], required=False)
    ninja_bin = find_tool(["ninja"], required=False)

    demo_nompi = Path(args.demo_exe).resolve() if args.demo_exe else (build_nompi / exe_name("mapreduce_demo"))
    demo_mpi = Path(args.mpi_exe).resolve() if args.mpi_exe else (build_mpi / exe_name("mapreduce_mpi_demo"))

    need_nompi_build = not demo_nompi.exists()
    need_mpi_build = not demo_mpi.exists()
    need_any_build = need_nompi_build or need_mpi_build

    if need_any_build and args.skip_build:
        raise RuntimeError(
            "--skip-build is set, but required executable(s) are missing:\n"
            f"  no-MPI demo: {demo_nompi} (exists={demo_nompi.exists()})\n"
            f"  MPI demo:    {demo_mpi} (exists={demo_mpi.exists()})"
        )

    if need_any_build and not cmake_bin:
        raise RuntimeError(
            "CMake is not in PATH and build is required.\n"
            "Either:\n"
            "  1) install CMake and add it to PATH, or\n"
            "  2) pass prebuilt executables: --demo-exe <path> --mpi-exe <path>, or\n"
            "  3) provide explicit CMake path via --cmake-bin <path-to-cmake.exe>."
        )

    if need_any_build:
        if ninja_bin is None:
            print("[info] Ninja not found, using default CMake generator.")
        else:
            print(f"[info] Using Ninja generator: {ninja_bin}")

        if need_nompi_build:
            print("[1/4] Configuring and building no-MPI target...")
            configure_build(source_root, build_nompi, use_mpi=False, cmake_bin=cmake_bin, ninja_bin=ninja_bin)
        else:
            print("[1/4] Using prebuilt no-MPI executable...")

        if need_mpi_build:
            print("[2/4] Configuring and building MPI target...")
            configure_build(source_root, build_mpi, use_mpi=True, cmake_bin=cmake_bin, ninja_bin=ninja_bin)
        else:
            print("[2/4] Using prebuilt MPI executable...")
    else:
        print("[1/4] Using prebuilt no-MPI and MPI executables (build skipped).")

    if not demo_nompi.exists():
        raise FileNotFoundError(f"Executable not found: {demo_nompi}")
    if not demo_mpi.exists():
        raise FileNotFoundError(f"Executable not found: {demo_mpi}")

    serial_us: list[float] = []
    threaded_us: list[float] = []
    mpi_us: list[float] = []

    print("[3/4] Running benchmarks...")
    for idx, size in enumerate(sizes):
        input_path = work_dir / f"input_{size}.txt"
        generate_wordcount_input(input_path, size, seed=args.seed + idx)

        serial_t = run_demo(demo_nompi, input_path, mode="serial", threads=args.threads, runs=args.runs)
        threaded_t = run_demo(demo_nompi, input_path, mode="threads", threads=args.threads, runs=args.runs)
        mpi_t = run_mpi_demo(demo_mpi, input_path, mpi_ranks=args.mpi_ranks, runs=args.runs)

        serial_us.append(serial_t)
        threaded_us.append(threaded_t)
        mpi_us.append(mpi_t)

        print(
            f"size={size}: serial={int(serial_t)} us, "
            f"threads={int(threaded_t)} us, mpi={int(mpi_t)} us"
        )

    print("[4/4] Plotting charts...")
    no_mpi_png = out_dir / "benchmark_no_mpi.png"
    mpi_png = out_dir / "benchmark_with_mpi.png"
    plot_no_mpi(no_mpi_png, sizes, serial_us, threaded_us, args.threads)
    plot_mpi(mpi_png, sizes, threaded_us, mpi_us, args.mpi_ranks, args.threads)

    print(f"Saved: {no_mpi_png}")
    print(f"Saved: {mpi_png}")


if __name__ == "__main__":
    main()
