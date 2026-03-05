import argparse
import random
import string
from pathlib import Path


def random_letters(length: int) -> str:
    return "".join(random.choices(string.ascii_letters, k=length))


def random_alpha_num(length: int) -> str:
    alphabet = string.ascii_letters + string.digits
    value = "".join(random.choices(alphabet, k=length))
    if not any(ch.isdigit() for ch in value):
        pos = random.randrange(len(value))
        value = value[:pos] + random.choice(string.digits) + value[pos + 1 :]
    return value


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", default="data/input.txt")
    parser.add_argument("--count", type=int, default=200000)
    parser.add_argument("--seed", type=int)
    args = parser.parse_args()

    if args.seed is not None:
        random.seed(args.seed)
    out_path = Path(args.output)
    out_path.parent.mkdir(parents=True, exist_ok=True)

    with out_path.open("w", encoding="utf-8") as handle:
        for _ in range(args.count):
            length = random.randint(6, 16)
            if random.random() < 0.5:
                handle.write(random_letters(length) + "\n")
            else:
                handle.write(random_alpha_num(length) + "\n")

    seed_text = args.seed if args.seed is not None else "system-random"
    print(f"Generated {args.count} strings into {out_path} (seed={seed_text})")


if __name__ == "__main__":
    main()
