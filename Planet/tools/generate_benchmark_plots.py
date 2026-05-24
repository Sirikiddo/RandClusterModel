from __future__ import annotations

from pathlib import Path

import matplotlib.pyplot as plt
import pandas as pd


ROOT = Path(__file__).resolve().parents[1]
CSV_PATH = ROOT / "dag_backend_benchmark_results.csv"
OUT_DIR = ROOT / "benchmark_charts"


def load_data() -> pd.DataFrame:
    df = pd.read_csv(CSV_PATH)
    numeric_columns = [
        "iteration",
        "elapsed_ms",
        "cell_count",
        "compatible",
        "selection_count",
        "tree_count",
        "model_count",
        "executed_nodes",
        "skipped_guard_nodes",
        "cache_hits",
        "cache_misses",
    ]
    for column in numeric_columns:
        if column in df.columns:
            df[column] = pd.to_numeric(df[column], errors="coerce")
    return df


def ensure_output_dir() -> None:
    OUT_DIR.mkdir(parents=True, exist_ok=True)


def save_figure(fig: plt.Figure, filename: str) -> None:
    fig.tight_layout()
    fig.savefig(OUT_DIR / filename, dpi=180, bbox_inches="tight")
    plt.close(fig)


def plot_terrain(df: pd.DataFrame) -> None:
    terrain = df[(df["category"] == "terrain") & (df["operation"] == "full_regenerate")].copy()
    summary = (
        terrain.groupby(["scenario", "backend"], as_index=False)["elapsed_ms"]
        .mean()
        .sort_values(["scenario", "backend"])
    )
    pivot = summary.pivot(index="scenario", columns="backend", values="elapsed_ms")

    fig, ax = plt.subplots(figsize=(11, 6))
    pivot.plot(kind="bar", ax=ax, color=["#D97706", "#2563EB"], width=0.75)
    ax.set_title("Terrain Full Regenerate: DAG vs Legacy", fontsize=15, weight="bold")
    ax.set_xlabel("Scenario")
    ax.set_ylabel("Average time (ms)")
    ax.grid(axis="y", linestyle="--", alpha=0.35)
    ax.legend(title="")
    ax.set_axisbelow(True)

    for container in ax.containers:
        ax.bar_label(container, fmt="%.1f", padding=3, fontsize=9)

    save_figure(fig, "terrain_benchmark_comparison.png")


def plot_scene_operations(df: pd.DataFrame) -> None:
    scene = df[df["category"] == "scene-derived"].copy()
    operations_order = [
        "baseline",
        "repeat_same",
        "selection_change",
        "selection_revert",
        "visual_change",
        "visual_revert",
        "terrain_edit",
        "terrain_revert",
    ]
    scene["operation"] = pd.Categorical(scene["operation"], categories=operations_order, ordered=True)

    scenarios = list(scene["scenario"].dropna().unique())
    fig, axes = plt.subplots(1, len(scenarios), figsize=(18, 5.8), sharey=False)
    if len(scenarios) == 1:
        axes = [axes]

    for ax, scenario in zip(axes, scenarios):
        subset = (
            scene[scene["scenario"] == scenario]
            .groupby(["operation", "backend"], as_index=False)["elapsed_ms"]
            .mean()
        )
        pivot = subset.pivot(index="operation", columns="backend", values="elapsed_ms").reindex(operations_order)
        pivot.plot(kind="bar", ax=ax, color=["#D97706", "#2563EB"], width=0.8)
        ax.set_title(scenario, fontsize=12, weight="bold")
        ax.set_xlabel("")
        ax.set_ylabel("Average time (ms)")
        ax.grid(axis="y", linestyle="--", alpha=0.35)
        ax.set_axisbelow(True)
        ax.tick_params(axis="x", rotation=35)
        ax.legend().remove()

    handles, labels = axes[0].get_legend_handles_labels()
    fig.legend(handles, labels, loc="upper center", ncol=2, frameon=False, bbox_to_anchor=(0.5, 1.03))
    fig.suptitle("Scene-Derived Operations: DAG vs Legacy", fontsize=16, weight="bold", y=1.08)
    save_figure(fig, "scene_benchmark_operations.png")


def plot_dag_metrics(df: pd.DataFrame) -> None:
    dag_scene = df[(df["category"] == "scene-derived") & (df["backend"] == "DAG scene")].copy()
    metrics = (
        dag_scene.groupby("scenario", as_index=False)[
            ["executed_nodes", "skipped_guard_nodes", "cache_hits", "cache_misses"]
        ]
        .sum()
        .set_index("scenario")
    )

    fig, ax = plt.subplots(figsize=(11, 6))
    metrics.plot(
        kind="bar",
        ax=ax,
        color=["#B91C1C", "#0F766E", "#2563EB", "#D97706"],
        width=0.75,
    )
    ax.set_title("DAG Internal Metrics for Scene-Derived Steps", fontsize=15, weight="bold")
    ax.set_xlabel("Scenario")
    ax.set_ylabel("Count")
    ax.grid(axis="y", linestyle="--", alpha=0.35)
    ax.legend(
        ["Executed nodes", "Guard-skipped nodes", "Cache hits", "Cache misses"],
        title="",
    )
    ax.set_axisbelow(True)

    for container in ax.containers:
        ax.bar_label(container, fmt="%.0f", padding=3, fontsize=9)

    save_figure(fig, "scene_benchmark_dag_metrics.png")


def plot_scene_speedup(df: pd.DataFrame) -> None:
    scene = df[df["category"] == "scene-derived"].copy()
    merged = (
        scene.groupby(["scenario", "operation", "backend"], as_index=False)["elapsed_ms"]
        .mean()
        .pivot(index=["scenario", "operation"], columns="backend", values="elapsed_ms")
        .reset_index()
    )
    merged["speedup"] = merged["Legacy scene"] / merged["DAG scene"]

    operations_order = [
        "baseline",
        "repeat_same",
        "selection_change",
        "selection_revert",
        "visual_change",
        "visual_revert",
        "terrain_edit",
        "terrain_revert",
    ]
    merged["operation"] = pd.Categorical(merged["operation"], categories=operations_order, ordered=True)
    merged = merged.sort_values(["scenario", "operation"])

    fig, ax = plt.subplots(figsize=(12, 6))
    colors = ["#2563EB" if value >= 1.0 else "#B91C1C" for value in merged["speedup"]]
    labels = [f"{scenario}\n{operation}" for scenario, operation in zip(merged["scenario"], merged["operation"])]
    bars = ax.bar(labels, merged["speedup"], color=colors)
    ax.axhline(1.0, color="#111827", linestyle="--", linewidth=1)
    ax.set_title("Legacy / DAG Speedup for Scene-Derived Operations", fontsize=15, weight="bold")
    ax.set_xlabel("Scenario and operation")
    ax.set_ylabel("Speedup ratio")
    ax.grid(axis="y", linestyle="--", alpha=0.35)
    ax.set_axisbelow(True)
    ax.tick_params(axis="x", rotation=45)
    ax.bar_label(bars, fmt="%.2f", padding=3, fontsize=8)

    save_figure(fig, "scene_benchmark_speedup.png")


def main() -> None:
    ensure_output_dir()
    df = load_data()
    plot_terrain(df)
    plot_scene_operations(df)
    plot_dag_metrics(df)
    plot_scene_speedup(df)
    print(f"Charts will be written to: {OUT_DIR}")


if __name__ == "__main__":
    main()
