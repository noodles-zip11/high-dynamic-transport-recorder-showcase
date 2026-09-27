"""Build and verify the public V1 evidence package.

The generator reads the selected event backup without copying its raw bytes.
PNG rendering deliberately goes through the existing ReplayWidget export path.
"""

from __future__ import annotations

import csv
import hashlib
import json
import os
from pathlib import Path
import re
import stat
import subprocess
from typing import Any, Iterable, Mapping, Sequence


PACKAGE_VERSION = "v1.0.0"
PACKAGE_SCHEMA = "v1-public-evidence-package-v1"
CLASS_NAMES = ("background", "impact", "continuous_vibration", "drop")
DEFAULT_EVENT_IDS = (180, 181, 191, 195, 202)
EXPECTED_SOURCE_SUMMARY_SHA256 = (
    "e24a44b1c9c6b8df4e119cc23e35ca7c80034263683e076bf106f2a0a0392228"
)
SOURCE_SUMMARY_NAME = "full-qspi-event-backup/summary.json"
DROP_LABEL_SOURCE = "evidence/v1/2026-08-25/hardware-acceptance-001.md"

_PACKAGE_MANIFEST = "manifest.json"
_PACKAGE_CHECKSUMS = "SHA256SUMS.txt"
_MARKDOWN_LINK_RE = re.compile(r"(?<!!)\[[^\]]+\]\(([^)]+)\)")
_PRIVATE_RAW_SUFFIXES = {".terp-event", ".ev01", ".raw", ".npz", ".npy"}
_GENERATED_DESKTOP_RE = re.compile(r"^event-\d+\.(?:png|json)$")
_KNOWN_PACKAGE_FILES = {
    "README.md",
    "manifest.json",
    "SHA256SUMS.txt",
    "catalog/evidence-inventory.csv",
    "catalog/key-logs.csv",
    "software/software-gate.md",
    "hardware/hardware-acceptance.md",
    "ai/real-ai-pilot.md",
    "ai/training-report.json",
    "ai/model-manifest.json",
    "desktop/README.md",
    "desktop/index.json",
    "provenance/source-and-builds.md",
    "provenance/release-equivalence.md",
}
_KNOWN_PACKAGE_DIRS = {"catalog", "software", "hardware", "ai", "desktop", "provenance"}


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def _git(repo_root: Path, *args: str) -> str:
    completed = subprocess.run(
        ["git", "-C", str(repo_root), *args],
        check=True,
        capture_output=True,
        text=True,
        encoding="utf-8",
    )
    return completed.stdout.strip()


def _git_bytes(repo_root: Path, *args: str) -> bytes:
    completed = subprocess.run(
        ["git", "-C", str(repo_root), *args],
        check=True,
        capture_output=True,
    )
    return completed.stdout


def _git_blob(repo_root: Path, source_commit: str, relative_path: str) -> bytes:
    return _git_bytes(repo_root, "cat-file", "blob", f"{source_commit}:{relative_path}")


def _git_blobs(
    repo_root: Path, source_commit: str, relative_paths: Sequence[str]
) -> dict[str, bytes]:
    """Read several commit blobs through one git process."""

    paths = tuple(relative_paths)
    request = b"".join(
        f"{source_commit}:{relative_path}\n".encode("utf-8") for relative_path in paths
    )
    completed = subprocess.run(
        ["git", "-C", str(repo_root), "cat-file", "--batch"],
        input=request,
        check=True,
        capture_output=True,
    )
    output = completed.stdout
    offset = 0
    blobs: dict[str, bytes] = {}
    for relative_path in paths:
        line_end = output.find(b"\n", offset)
        if line_end < 0:
            raise ValueError(f"git cat-file returned no header for {relative_path}")
        header = output[offset:line_end].split()
        if len(header) != 3 or header[1] != b"blob":
            raise ValueError(f"git source blob is unavailable: {relative_path}")
        size = int(header[2])
        start = line_end + 1
        end = start + size
        if len(output) <= end or output[end : end + 1] != b"\n":
            raise ValueError(f"git source blob has an invalid boundary: {relative_path}")
        blobs[relative_path] = output[start:end]
        offset = end + 1
    if offset != len(output):
        raise ValueError("git cat-file returned unexpected trailing data")
    return blobs


def resolve_source_commits(repo_root: Path) -> tuple[str, str, str]:
    """Resolve the immutable tag baseline and prove it is still reachable."""

    repo_root = Path(repo_root).resolve()
    tag_ref = "refs/tags/v1.0.0"
    tag_object = _git(repo_root, "rev-parse", tag_ref)
    source_commit = _git(repo_root, "rev-parse", f"{tag_ref}^{{}}")
    current_main = _git(repo_root, "rev-parse", "main")
    try:
        _git(repo_root, "merge-base", "--is-ancestor", source_commit, current_main)
    except subprocess.CalledProcessError:
        current_head = _git(repo_root, "rev-parse", "HEAD")
        try:
            _git(repo_root, "merge-base", "--is-ancestor", source_commit, current_head)
        except subprocess.CalledProcessError as head_exc:
            raise ValueError(
                "v1.0.0 peeled commit is neither a main nor HEAD ancestor"
            ) from head_exc
    return source_commit, source_commit, tag_object


def _json_dump(path: Path, value: Any) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8", newline="\n") as stream:
        stream.write(json.dumps(value, ensure_ascii=False, indent=2, sort_keys=True) + "\n")


def _write_text(path: Path, value: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8", newline="\n") as stream:
        stream.write(value.rstrip() + "\n")


def tracked_evidence_inventory(
    repo_root: Path, *, source_commit: str | None = None
) -> list[dict[str, Any]]:
    """Index all evidence tracked by the selected source commit."""

    repo_root = Path(repo_root).resolve()
    source_commit = source_commit or _git(repo_root, "rev-parse", "refs/tags/v1.0.0^{}")
    names = _git(repo_root, "ls-tree", "-r", "--name-only", source_commit, "--", "evidence")
    relative_paths = tuple(
        Path(relative).as_posix() for relative in filter(None, names.splitlines())
    )
    blobs = _git_blobs(repo_root, source_commit, relative_paths)
    rows: list[dict[str, Any]] = []
    for relative_path in relative_paths:
        blob = blobs[relative_path]
        parts = relative_path.split("/")
        phase = parts[1] if len(parts) > 1 else "evidence"
        rows.append(
            {
                "relative_path": relative_path,
                "size_bytes": len(blob),
                "sha256": hashlib.sha256(blob).hexdigest(),
                "file_type": Path(relative_path).suffix.lower().lstrip(".")
                or "no_extension",
                "phase": phase,
            }
        )
    return rows


def write_inventory(path: Path, rows: Iterable[Mapping[str, Any]]) -> None:
    fieldnames = ("relative_path", "size_bytes", "sha256", "file_type", "phase")
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", newline="\n", encoding="utf-8") as stream:
        writer = csv.DictWriter(stream, fieldnames=fieldnames, lineterminator="\n")
        writer.writeheader()
        writer.writerows(rows)


def load_source_summary(source_backup: Path) -> tuple[dict[str, Any], str]:
    summary_path = Path(source_backup) / "summary.json"
    actual_hash = sha256_file(summary_path)
    if actual_hash != EXPECTED_SOURCE_SUMMARY_SHA256:
        raise ValueError(
            "source summary SHA-256 does not match the supplied read-only backup contract"
        )
    return json.loads(summary_path.read_text(encoding="utf-8")), actual_hash


def _source_event_entries(summary: Mapping[str, Any]) -> dict[int, Mapping[str, Any]]:
    entries: dict[int, Mapping[str, Any]] = {}
    for entry in summary.get("events", []):
        event_id = int(entry["event_id"])
        if event_id in entries:
            raise ValueError(f"duplicate source event id {event_id}")
        entries[event_id] = entry
    return entries


def _human_label(event_id: int) -> dict[str, Any]:
    if 191 <= event_id <= 195:
        return {
            "value": None,
            "planned_action": "drop",
            "verified_individual": False,
            "source": DROP_LABEL_SOURCE,
            "confidence": "planned action mapping only; not individually verified as event truth",
        }
    return {
        "value": None,
        "planned_action": None,
        "verified_individual": False,
        "source": None,
        "confidence": "not individually asserted; the field action sequence was not fully deterministic",
    }


def build_event_metadata(
    event_path: Path,
    source_entry: Mapping[str, Any],
    *,
    source_summary_sha256: str,
) -> dict[str, Any]:
    """Build public metadata without exposing a raw event payload."""

    from host.transport_recorder.analysis.event_record import load_event

    if source_entry.get("record_crc_verified_by_client") is not True:
        raise ValueError("release desktop evidence requires client-verified record CRC")

    decoded = source_entry.get("decoded")
    if not isinstance(decoded, Mapping):
        raise ValueError("source summary decoded metadata is missing")
    required_decoded = {
        "format_version": int(decoded.get("format_version", 0)),
        "sample_count": int(decoded.get("sample_count", 0)),
        "pretrigger_samples": int(decoded.get("pretrigger_samples", 0)),
        "posttrigger_samples": int(decoded.get("posttrigger_samples", 0)),
        "subtrigger_count": int(decoded.get("subtrigger_count", -1)),
        "lost_sample_count": int(decoded.get("lost_sample_count", -1)),
    }
    if required_decoded["format_version"] != 3:
        raise ValueError("release desktop evidence requires EV03")
    if required_decoded["sample_count"] != 2400:
        raise ValueError("release desktop evidence requires 2400 source samples")
    if required_decoded["pretrigger_samples"] + required_decoded["posttrigger_samples"] != 2400:
        raise ValueError("release desktop evidence requires 2400 pre/post samples")
    if required_decoded["subtrigger_count"] != 0:
        raise ValueError("release desktop evidence requires subtrigger_count=0")
    if required_decoded["lost_sample_count"] != 0:
        raise ValueError("release desktop evidence requires lost_sample_count=0")

    ai = source_entry.get("ai_result")
    if not isinstance(ai, Mapping) or int(ai.get("status", -1)) != 1:
        raise ValueError("release desktop evidence requires successful AI status=1")

    event = load_event(Path(event_path))
    event_id = int(source_entry["event_id"])
    if event.metadata.event_id != event_id:
        raise ValueError("decoded event id does not match source summary")
    if int(source_entry.get("bytes", 0)) != Path(event_path).stat().st_size:
        raise ValueError("source summary byte count does not match event file")
    source_hash = sha256_file(Path(event_path))
    if source_hash.lower() != str(source_entry["sha256"]).lower():
        raise ValueError("source summary event SHA-256 does not match event file")

    actual_decoded = {
        "format_version": event.metadata.format_version,
        "sample_count": event.sample_count,
        "pretrigger_samples": event.metadata.pretrigger_samples,
        "posttrigger_samples": event.metadata.posttrigger_samples,
        "subtrigger_count": event.metadata.subtrigger_count,
        "lost_sample_count": event.metadata.lost_sample_count,
    }
    if actual_decoded != required_decoded:
        raise ValueError("decoded event metadata does not match source summary")
    if (
        actual_decoded["pretrigger_samples"] + actual_decoded["posttrigger_samples"] != 2400
        or actual_decoded["sample_count"] != 2400
    ):
        raise ValueError("decoded event does not contain 2400 samples")

    if int(ai["event_id"]) != event_id or int(ai["class_count"]) != 4:
        raise ValueError("AI result identity or class count is invalid")
    if int(ai.get("sample_count", -1)) != event.sample_count:
        raise ValueError("AI result sample count does not match decoded event")
    class_index = int(ai["class_index"])
    if not 0 <= class_index < len(CLASS_NAMES):
        raise ValueError("AI result class index is outside the V1 contract")
    prediction = {
        "class_index": class_index,
        "class_name": CLASS_NAMES[class_index],
        "class_count": int(ai["class_count"]),
        "confidence": float(ai["confidence"]),
        "model_version": int(ai["model_version"]),
        "status": int(ai["status"]),
        "source": {
            "summary": SOURCE_SUMMARY_NAME,
            "summary_sha256": source_summary_sha256,
        },
    }

    human = _human_label(event_id)
    return {
        "event_id": event_id,
        "derived_from": {
            "summary": SOURCE_SUMMARY_NAME,
            "summary_sha256": source_summary_sha256,
            "raw_event": f"full-qspi-event-backup/event-{event_id:03d}.terp-event",
            "raw_event_sha256": source_hash,
            "decoder": "host.transport_recorder.analysis.event_record.load_event",
        },
        "source_raw": {
            "relative_name": f"full-qspi-event-backup/event-{event_id:03d}.terp-event",
            "sha256": source_hash,
            "summary_sha256": source_summary_sha256,
        },
        "decoded": {
            "format_version": event.metadata.format_version,
            "sample_count": event.sample_count,
            "sample_rate_hz": event.metadata.sample_rate_hz,
            "pretrigger_samples": event.metadata.pretrigger_samples,
            "posttrigger_samples": event.metadata.posttrigger_samples,
            "subtrigger_count": event.metadata.subtrigger_count,
            "lost_sample_count": event.metadata.lost_sample_count,
        },
        "crc": {
            "record_crc_verified": True,
            "device_crc32": f"0x{int(source_entry['device_crc32']):08X}",
            "payload_crc32": f"0x{event.metadata.payload_crc32:08X}",
        },
        "human_label": human,
        "ai_prediction": prediction,
        "label_semantics": (
            "human_label.value is populated only for individually verified action truth; "
            "planned_action is a non-truth action plan mapping; ai_prediction is the MCU "
            "model output and is never used as the label"
        ),
        "evidence_level": "derived",
    }


def export_replay_png(event_path: Path, target: Path) -> Path:
    """Export one event through the existing ReplayWidget PNG path."""

    os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")
    from PySide6.QtWidgets import QApplication

    from host.transport_recorder.analysis.event_record import load_event
    from host.transport_recorder.ui.replay_widget import ReplayWidget

    app = QApplication.instance() or QApplication([])
    choose_replay_font()
    widget = ReplayWidget()
    target = Path(target)
    export_target = target
    temporary_target: Path | None = None
    if target.exists():
        temporary_target = target.with_name(f".{target.stem}.replay-tmp{target.suffix}")
        if temporary_target.exists():
            temporary_target.unlink()
        export_target = temporary_target
    try:
        widget.resize(1200, 800)
        widget.set_event(load_event(Path(event_path)))
        app.processEvents()
        output = widget.export_png(export_target)
        app.processEvents()
        if not output.is_file() or output.stat().st_size == 0:
            raise ValueError("ReplayWidget produced an empty PNG")
        if output.read_bytes()[:8] != b"\x89PNG\r\n\x1a\n":
            raise ValueError("ReplayWidget output is not a PNG")
        if temporary_target is not None:
            target.write_bytes(output.read_bytes())
            output.unlink()
            output = target
        return output
    finally:
        if temporary_target is not None and temporary_target.exists():
            temporary_target.unlink()
        widget.close()


def choose_replay_font() -> str:
    """Select an installed Latin-capable font before creating a replay widget."""

    from PySide6.QtGui import QFont, QFontDatabase
    from PySide6.QtWidgets import QApplication

    app = QApplication.instance() or QApplication([])
    families = set(QFontDatabase.families())
    font_candidates = (
        (Path("C:/Windows/Fonts/arial.ttf"), "Arial"),
        (Path("C:/Windows/Fonts/segoeui.ttf"), "Segoe UI"),
        (Path("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf"), "DejaVu Sans"),
        (Path("/usr/share/fonts/truetype/liberation2/LiberationSans-Regular.ttf"), "Liberation Sans"),
    )
    for font_path, preferred_family in font_candidates:
        if preferred_family in families:
            app.setFont(QFont(preferred_family, 10))
            return preferred_family
        if font_path.is_file():
            font_id = QFontDatabase.addApplicationFont(str(font_path))
            if font_id >= 0:
                families.update(QFontDatabase.applicationFontFamilies(font_id))
        if preferred_family in families:
            app.setFont(QFont(preferred_family, 10))
            return preferred_family
    for family in ("Arial", "Segoe UI", "DejaVu Sans", "Liberation Sans"):
        if family in families:
            app.setFont(QFont(family, 10))
            return family
    if not families:
        raise RuntimeError("no installed Qt font is available for replay export")
    fallback = sorted(families)[0]
    app.setFont(QFont(fallback, 10))
    return fallback


def _source_hashes(repo_root: Path, source_commit: str) -> dict[str, str]:
    paths = (
        "evidence/v1/2026-08-24/software-gate.md",
        "evidence/v1/2026-08-25/hardware-acceptance-001.md",
        "evidence/phase2-ai/2026-08-23/real-ai-pilot.md",
        "docs/ai/model_contract_v1.md",
        "firmware/app/ai_inference/ai_model_data.c",
    )
    return {
        relative: hashlib.sha256(_git_blob(repo_root, source_commit, relative)).hexdigest()
        for relative in paths
    }


def _write_public_documents(
    repo_root: Path,
    output: Path,
    *,
    source_commit: str,
    tag_commit: str,
    tag_object: str,
    source_summary_sha256: str,
    source_hashes: Mapping[str, str],
    selected_events: Sequence[Mapping[str, Any]],
) -> None:
    _write_text(
        output / "README.md",
        f"""# V1.0.0 公开证据包

这是高动态运输事件记录器 V1 的精选、可公开、可复核证据包。包内保存软件/硬件/AI 摘要、完整 tracked evidence 索引、来源哈希、代表波形 PNG 和机器元数据；外部 QSPI raw 没有复制进 Git。

## 能力结论

- V1 候选已取得启动、自然触发、固定 EV03 事件、QSPI 持久化、TERP 下载、CRC、真实四分类推理和 Cortex-M7 WFI 状态链的实板证据。
- 软件候选 gate、硬件验收和 AI pilot 的来源与限制分别见 [software gate](software/software-gate.md)、[hardware acceptance](hardware/hardware-acceptance.md) 和 [AI pilot](ai/real-ai-pilot.md)。
- 代表波形由现有 Host 解码器和 PySide6 `ReplayWidget.export_png` 离屏导出；PNG 只表达派生可视化，不能替代原始事件。

## 明确限制

- AI 结果是受控真实数据 pilot：测试为 10/13，macro-F1 为 0.755952，四类 recall 见 AI 摘要；`within_session_pilot=true`，每类 session 数不足以证明独立 session 泛化。
- 低功耗证据只覆盖普通 Cortex-M7 Sleep/WFI、唤醒和 blocker 软件链；没有实测电流、真实节电百分比或 9000 mAh 续航结论，STOP/Standby 也不在本包的 V1 声明内。
- 本轮 V1 HIL 动作没有可核实的连续振动人工动作真值，因此不伪造该类波形或标签；受控 AI pilot 的连续振动训练/测试标签仍按 AI 摘要保留。AI 预测不是人工真值。
- G2 100 次断电、物理断连和 72 小时长稳属于 deferred reliability matrix，不写成 V1 核心 PASS。

## 公开边界和 provenance

- 不可变基线提交：`v1.0.0^{{}}={source_commit}`；当前 `main`/HEAD 可以是该 tag 之后的后续提交（tag object `{tag_object}`）。证据包本身是后续文档提交，不改变固件/协议/模型。
- 外部完整 QSPI 备份只以来源身份和 `summary.json` SHA-256 `{source_summary_sha256}` 表示；204 条事件的原始 bytes 不在仓库。
- 包文件的大小和 SHA-256 见 [manifest](manifest.json) 与 [SHA256SUMS](SHA256SUMS.txt)。manifest 不对自身做自引用，SHA256SUMS 覆盖 manifest 和其他包文件，清单自身不在自身列表中。
- [provenance/source-and-builds.md](provenance/source-and-builds.md) 记录候选构建身份、产物哈希和可回滚边界；[catalog/evidence-inventory.csv](catalog/evidence-inventory.csv) 索引基线提交下所有 tracked evidence，而不是复制历史 evidence。

## 桌面派生图

图、逐图 metadata 和选择说明见 [desktop README](desktop/README.md) 与 [desktop index](desktop/index.json)。每个 metadata 都分开保存 `human_label`、`planned_action` 与 `ai_prediction`，并追溯 event ID、raw SHA-256、CRC、sample/loss/subtrigger。
""",
    )

    software_source = source_hashes["evidence/v1/2026-08-24/software-gate.md"]
    _write_text(
        output / "software/software-gate.md",
        f"""# V1 软件门禁发布快照

来源：`evidence/v1/2026-08-24/software-gate.md`，SHA-256 `{software_source}`。

- Debug 总门禁 exit 0：native 47 个 executable、Host 131、AI 78；固件/vector/memory-map/ICM alignment 通过。
- Release 构建 exit 0，无编译 warning；ROM 135,760 B，RAM 271,388 B，D2_SRAM1 2,144 B。
- Release ELF 222,568 B，SHA-256 `1DF1E8D7C629FB8947502098473595DCAE5361E5B9BAE06B0F069A8D8EEF24C7`。
- Release BIN 135,760 B，SHA-256 `C610DDE842B8F6358AC2DA686221DAA042A7C78D434ABCAD174595ED30E997CF`。
- Release MAP 838,917 B，SHA-256 `CD40463967C03EB1D2CC985E4517DC54481E62B91124DD365FA8B77B9ECE17E5`。
- 软件证据包含四分类模型、EV03/2400、4112 B FIFO 分块、自然触发 one-shot/30 s cooldown 和 WFI-only power policy。

这是一份来源摘要，不把软件 gate 当作所有实板可靠性矩阵；真实电流/续航、G2、物理断连和 72 小时长稳仍有独立边界。
""",
    )

    hardware_source = source_hashes["evidence/v1/2026-08-25/hardware-acceptance-001.md"]
    _write_text(
        output / "hardware/hardware-acceptance.md",
        f"""# V1 实板验收发布快照

来源：`evidence/v1/2026-08-25/hardware-acceptance-001.md`，SHA-256 `{hardware_source}`。

- mandatory checklist 1/3/4/5/6：PASS；书包 30 分钟按清单为 SKIP，不写成 PASS 或 FAIL。
- 10 分钟静置：21 次每 30 s 采样保持 event 179，无误触发，storage/export error 为 0；WFI attempts/entries/wakes 递增，blocker holds 为 0，STOP 编译和允许值均为 0。
- event 180–202 连续、共 23 条；全部 EV03、2400 samples、CRC 通过、subtrigger=0、lost=0、AI status=1、class_count=4。
- 计划最后 5 次掉落对应 event 191–195，`planned_action=drop` 只是验收记录中的计划映射，不是逐事件人工真值；五条 MCU 预测均为 `drop`。event 180–195 中其他动作的逐条真值不完全确定，不计算动作分类准确率。
- 本轮没有 continuous_vibration 动作，因而不提供该类人工真值结论。
- 外部 full-QSPI backup 包含 event 001–204，summary SHA-256 为 `{source_summary_sha256}`；原始文件和逐条 readback 没有纳入 Git。

V1 硬件结论不声称 AI 泛化准确率、不声称低功耗电流/续航，也不把 format 后的容量状态误写成原始日志仍在 QSPI。
""",
    )

    ai_source = source_hashes["evidence/phase2-ai/2026-08-23/real-ai-pilot.md"]
    _write_text(
        output / "ai/real-ai-pilot.md",
        f"""# Real AI pilot 发布摘要

来源：`evidence/phase2-ai/2026-08-23/real-ai-pilot.md`，SHA-256 `{ai_source}`。

- 类别顺序：`background`, `impact`, `continuous_vibration`, `drop`。
- 真实合格事件 91 条；显式 controlled-pilot split 为 train 64、validation 14、test 13。
- Release int8 test accuracy：`0.7692308`（10/13）；macro-F1：`0.755952`。
- 四类 test recall：background `0.75`、impact `1.0`、continuous_vibration `0.3333`、drop `1.0`。
- `within_session_pilot=true`；每类目前只有一个受控 session，不代表独立 session 泛化，也不替代至少三个独立 session/类的正式门禁。
- float/int8 test prediction parity 为 `1.0`；模型 CRC32 为 `0xAD10980E`；权重 SHA-256 为 `F4EF4C310F808F0DC76E59E4F2296FC97D5A90118C73506E34C995E47E53FB78`。

当前仓库中的 `ai/artifacts/pilot-v1/` 是历史二分类兼容工件；本四分类发布快照依据本页来源、`docs/ai/model_contract_v1.md` 和固件模型数据合同生成，不把二分类文件冒充四分类 manifest。结构化摘要见 `training-report.json` 和 `model-manifest.json`。
""",
    )

    _json_dump(
        output / "ai/training-report.json",
        {
            "snapshot_kind": "canonical-evidence-summary",
            "source": {
                "path": "evidence/phase2-ai/2026-08-23/real-ai-pilot.md",
                "sha256": ai_source,
            },
            "class_names": list(CLASS_NAMES),
            "eligible_events": 91,
            "split": {
                "mode": "pilot_event_stratified",
                "within_session_pilot": True,
                "train": 64,
                "validation": 14,
                "test": 13,
            },
            "metrics": {
                "test_accuracy": 0.7692308,
                "test_correct": 10,
                "test_total": 13,
                "macro_f1": 0.755952,
                "recall": {
                    "background": 0.75,
                    "impact": 1.0,
                    "continuous_vibration": 0.3333,
                    "drop": 1.0,
                },
                "float_int8_test_prediction_parity": 1.0,
            },
            "limitations": [
                "每类只有一个受控 session",
                "结果不代表独立 session 泛化",
                "continuous_vibration recall 偏低",
            ],
        },
    )
    _json_dump(
        output / "ai/model-manifest.json",
        {
            "snapshot_kind": "canonical-evidence-summary",
            "source": {
                "pilot_report": {
                    "path": "evidence/phase2-ai/2026-08-23/real-ai-pilot.md",
                    "sha256": ai_source,
                },
                "model_contract": {
                    "path": "docs/ai/model_contract_v1.md",
                    "sha256": source_hashes["docs/ai/model_contract_v1.md"],
                },
                "firmware_model_data": {
                    "path": "firmware/app/ai_inference/ai_model_data.c",
                    "sha256": source_hashes["firmware/app/ai_inference/ai_model_data.c"],
                },
            },
            "class_names": list(CLASS_NAMES),
            "feature_version": "counts_v1",
            "input_shape": [6],
            "runtime_version": "feature_mlp_runtime_v2",
            "model_crc32": "0xAD10980E",
            "weights_sha256": "F4EF4C310F808F0DC76E59E4F2296FC97D5A90118C73506E34C995E47E53FB78",
            "within_session_pilot": True,
            "artifact_note": "此文件是四分类 canonical evidence snapshot，不是对历史二分类 pilot-v1 manifest 的改写。",
        },
    )

    _write_text(
        output / "desktop/README.md",
        """# Desktop ReplayWidget 派生图

PNG 由现有 Host `load_event` 严格解码后，经 PySide6 `ReplayWidget.set_event` 和 `ReplayWidget.export_png` 在 offscreen Qt 中生成。raw 只从外部备份读取，不写入此目录。

`index.json` 列出每张图的 event ID、PNG 哈希和 metadata 文件。逐图 JSON 的 `human_label` 只表示逐事件核实的人工真值（本包均为 null），`planned_action` 是动作计划映射，`ai_prediction` 是 MCU 四分类模型输出；三者始终分开，AI 预测不能反向充当真值。

event 191、195 的 `planned_action=drop` 来自 V1 验收中明确的最后五次掉落计划，但不是逐事件人工真值；event 180、181、202 只保留模型输出和数据完整性字段，没有猜测人工标签。本轮 V1 HIL 动作没有已核实的连续振动人工动作，因此没有为该类制造图片或标签；这不否定受控 AI pilot 已有的连续振动标签。
""",
    )
    desktop_index = []
    for metadata in selected_events:
        event_id = int(metadata["event_id"])
        desktop_index.append(
            {
                "event_id": event_id,
                "png": f"event-{event_id:03d}.png",
                "metadata": f"event-{event_id:03d}.json",
                "human_label": metadata["human_label"]["value"],
                "planned_action": metadata["human_label"]["planned_action"],
                "verified_individual": metadata["human_label"]["verified_individual"],
                "ai_prediction": metadata["ai_prediction"],
                "evidence_level": "derived",
            }
        )
    _json_dump(output / "desktop/index.json", {"events": desktop_index})

    _write_text(
        output / "provenance/source-and-builds.md",
        f"""# 来源与构建身份

- V1 不可变基线：`v1.0.0^{{}}` = `{source_commit}`；当前 `main`/HEAD 可以位于该 tag 之后。
- 既有 tag object：`{tag_object}`；本包不移动 tag。
- 外部 QSPI source identity：`{SOURCE_SUMMARY_NAME}`，summary SHA-256 `{source_summary_sha256}`，204 条连续事件，raw 不入 Git。
- 候选固件 revision：`46a131af376033c26b0ebf59f724c1fbcfeb0004`。
- Release ELF：222,568 B，SHA-256 `1DF1E8D7C629FB8947502098473595DCAE5361E5B9BAE06B0F069A8D8EEF24C7`。
- Release BIN：135,760 B，SHA-256 `C610DDE842B8F6358AC2DA686221DAA042A7C78D434ABCAD174595ED30E997CF`。
- Release MAP：838,917 B，SHA-256 `CD40463967C03EB1D2CC985E4517DC54481E62B91124DD365FA8B77B9ECE17E5`。

产物哈希来自 V1 software gate；本证据包提交不重新构建、不刷板、不修改固件。板上候选等价性以该 revision、software gate 和硬件验收记录共同界定。
""",
    )
    _write_text(
        output / "provenance/release-equivalence.md",
        f"""# 发布等价性说明

`v1.0.0` 的 peeled commit 是发布包基线 `{source_commit}`。本分支新增的是文档、索引、派生 PNG、Host 生成/校验工具和测试；没有改变 firmware、protocol、AI model logic、Flash layout、私有 raw 或已有历史 evidence。

Release BIN/ELF/MAP 哈希和候选 revision 取自 V1 software gate。包内 PNG 是从外部 QSPI backup 派生的公开图，不是可回刷镜像，也不包含原始事件 bytes。任何重新构建都必须重新记录产物哈希，不能沿用本快照。
""",
    )

    key_log_paths = (
        ("evidence/v1/2026-08-24/software-gate.md", "V1 software gate", "software"),
        ("evidence/v1/2026-08-25/hardware-acceptance-001.md", "V1 hardware acceptance", "hardware"),
        ("evidence/phase2-ai/2026-08-23/real-ai-pilot.md", "real AI controlled pilot", "ai_pilot"),
        ("evidence/hardware-bringup/2026-08-17/ai-pilot-hil/summary.md", "earlier AI HIL smoke", "hardware"),
    )
    key_log_file = output / "catalog/key-logs.csv"
    key_log_file.parent.mkdir(parents=True, exist_ok=True)
    with key_log_file.open("w", newline="\n", encoding="utf-8") as stream:
        writer = csv.DictWriter(
            stream,
            fieldnames=("relative_path", "size_bytes", "sha256", "role", "evidence_level"),
            lineterminator="\n",
        )
        writer.writeheader()
        for relative, role, level in key_log_paths:
            source = _git_blob(repo_root, source_commit, relative)
            writer.writerow(
                {
                    "relative_path": relative,
                    "size_bytes": len(source),
                    "sha256": hashlib.sha256(source).hexdigest(),
                    "role": role,
                    "evidence_level": level,
                }
            )


def _package_evidence_level(relative: str) -> str:
    if relative.startswith("software/"):
        return "software"
    if relative.startswith("hardware/") or relative.startswith("provenance/"):
        return "hardware"
    if relative.startswith("ai/"):
        return "ai_pilot"
    if relative.startswith("desktop/"):
        return "derived"
    return "catalog"


def _package_entries(output: Path) -> list[dict[str, Any]]:
    entries: list[dict[str, Any]] = []
    for path in sorted(output.rglob("*")):
        if not path.is_file():
            continue
        relative = path.relative_to(output).as_posix()
        if relative in {_PACKAGE_MANIFEST, _PACKAGE_CHECKSUMS}:
            continue
        entries.append(
            {
                "path": relative,
                "size_bytes": path.stat().st_size,
                "sha256": sha256_file(path),
                "evidence_level": _package_evidence_level(relative),
            }
        )
    return entries


def write_manifest(
    output: Path,
    *,
    source_commit: str,
    tag_commit: str,
    tag_object: str = "",
    source_summary_sha256: str = EXPECTED_SOURCE_SUMMARY_SHA256,
) -> Path:
    entries = _package_entries(output)
    manifest = {
        "package_version": PACKAGE_VERSION,
        "schema": PACKAGE_SCHEMA,
        "source_commit": source_commit,
        "tag": {
            "name": "v1.0.0",
            "peeled_commit": tag_commit,
            "object": tag_object,
        },
        "generated_by": {
            "module": "host/tools/v1_evidence_package.py",
            "version": 1,
            "raw_included": False,
        },
        "source_boundary": {
            "summary_name": SOURCE_SUMMARY_NAME,
            "summary_sha256": source_summary_sha256,
            "event_file_count": 204,
            "event_id_range": [1, 204],
            "raw_files_copied": 0,
        },
        "package_file_count_excluding_manifests": len(entries),
        "package_file_count_including_manifests": len(entries) + 2,
        "package_files": entries,
        "checksum_scope": {
            "manifest_self_hash": "excluded",
            "sha256sums_self_hash": "excluded",
            "sha256sums_covers_manifest": True,
        },
    }
    path = output / _PACKAGE_MANIFEST
    _json_dump(path, manifest)
    return path


def write_checksums(output: Path) -> Path:
    output = Path(output)
    lines: list[str] = []
    for path in sorted(output.rglob("*")):
        if not path.is_file() or path.name == _PACKAGE_CHECKSUMS:
            continue
        relative = path.relative_to(output).as_posix()
        lines.append(f"{sha256_file(path)}  {relative}")
    path = output / _PACKAGE_CHECKSUMS
    with path.open("w", encoding="utf-8", newline="\n") as stream:
        stream.write("\n".join(lines) + "\n")
    return path


def validate_markdown_links(output: Path) -> None:
    output = Path(output)
    for markdown in output.rglob("*.md"):
        text = markdown.read_text(encoding="utf-8")
        for target in _MARKDOWN_LINK_RE.findall(text):
            target = target.split("#", 1)[0]
            if not target or "://" in target:
                continue
            resolved = (markdown.parent / target).resolve()
            if output.resolve() not in resolved.parents and resolved != output.resolve():
                raise ValueError(f"markdown link escapes package: {markdown} -> {target}")
            if not resolved.exists():
                raise FileNotFoundError(f"broken markdown link: {markdown} -> {target}")


def _absolute_without_resolving(path: Path) -> Path:
    return Path(os.path.abspath(os.fspath(path)))


def _is_reparse_point(path: Path) -> bool:
    try:
        stat_result = path.lstat()
    except FileNotFoundError:
        return False
    if stat.S_ISLNK(stat_result.st_mode):
        return True
    reparse_flag = getattr(stat, "FILE_ATTRIBUTE_REPARSE_POINT", 0x0400)
    return bool(getattr(stat_result, "st_file_attributes", 0) & reparse_flag)


def _ensure_no_reparse_components(path: Path) -> None:
    absolute = _absolute_without_resolving(path)
    current = Path(absolute.anchor) if absolute.anchor else Path()
    for part in absolute.parts:
        if part == absolute.anchor:
            continue
        current /= part
        if _is_reparse_point(current):
            raise ValueError(f"output path contains a symlink/reparse point: {current}")


def _ensure_no_reparse_tree(root: Path) -> None:
    root = _absolute_without_resolving(root)
    if not root.exists():
        return
    for directory, directories, files in os.walk(root, followlinks=False):
        for name in (*directories, *files):
            path = Path(directory) / name
            if _is_reparse_point(path):
                raise ValueError(f"output contains a symlink/reparse point: {path}")


def _is_known_package_file(relative: str) -> bool:
    if relative in _KNOWN_PACKAGE_FILES:
        return True
    if not relative.startswith("desktop/"):
        return False
    desktop_relative = relative.removeprefix("desktop/")
    return "/" not in desktop_relative and bool(
        _GENERATED_DESKTOP_RE.fullmatch(desktop_relative)
    )


def _validate_output_directory(output: Path, *, overwrite: bool) -> Path:
    output = _absolute_without_resolving(output)
    _ensure_no_reparse_components(output)
    if not output.exists():
        return output
    if not output.is_dir():
        raise ValueError(f"package output is not a directory: {output}")
    _ensure_no_reparse_tree(output)
    if overwrite:
        unknown = sorted(
            path.relative_to(output).as_posix()
            for path in output.rglob("*")
            if (
                (path.is_dir() and path.relative_to(output).as_posix() not in _KNOWN_PACKAGE_DIRS)
                or (
                    path.is_file()
                    and not _is_known_package_file(path.relative_to(output).as_posix())
                )
            )
        )
        if unknown:
            raise ValueError(
                "force output contains unknown files; refusing nondeterministic package: "
                + ", ".join(unknown)
            )
    elif any(output.iterdir()):
        raise FileExistsError(f"refusing to overwrite non-empty package directory: {output}")
    return output


def clear_generated_desktop_outputs(output: Path) -> None:
    """Remove only files this generator can create in an explicit package dir."""

    output = _absolute_without_resolving(output)
    _ensure_no_reparse_components(output)
    desktop = output / "desktop"
    if not desktop.is_dir():
        return
    _ensure_no_reparse_tree(desktop)
    for path in desktop.iterdir():
        if not path.is_file():
            continue
        if path.name == "index.json" or _GENERATED_DESKTOP_RE.fullmatch(path.name):
            path.unlink()


def _validate_public_boundary(output: Path) -> None:
    for path in output.rglob("*"):
        if not path.is_file():
            continue
        if path.suffix.lower() in _PRIVATE_RAW_SUFFIXES:
            raise ValueError(f"private/raw suffix in public package: {path.name}")
        if path.stat().st_size >= 100 * 1024 * 1024:
            raise ValueError(f"public package file is at least 100 MiB: {path}")
        if path.suffix.lower() in {".md", ".json", ".csv", ".txt"}:
            text = path.read_text(encoding="utf-8")
            if "C:\\" in text or "C:/" in text:
                raise ValueError(f"absolute Windows path in public package: {path}")


def verify_package(output: Path) -> dict[str, int]:
    output = Path(output)
    manifest_path = output / _PACKAGE_MANIFEST
    checksums_path = output / _PACKAGE_CHECKSUMS
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    expected_entries = {entry["path"]: entry for entry in manifest["package_files"]}
    actual_entries = {
        path.relative_to(output).as_posix(): path
        for path in output.rglob("*")
        if path.is_file() and path.name not in {_PACKAGE_MANIFEST, _PACKAGE_CHECKSUMS}
    }
    if set(expected_entries) != set(actual_entries):
        raise ValueError("manifest package file coverage mismatch")
    for relative, entry in expected_entries.items():
        path = actual_entries[relative]
        if path.stat().st_size != int(entry["size_bytes"]):
            raise ValueError(f"manifest size mismatch: {relative}")
        if sha256_file(path) != str(entry["sha256"]):
            raise ValueError(f"manifest hash mismatch: {relative}")

    checksum_lines = {}
    for line in checksums_path.read_text(encoding="utf-8").splitlines():
        digest, relative = line.split("  ", 1)
        checksum_lines[relative] = digest
    all_non_checksum = {
        path.relative_to(output).as_posix(): path
        for path in output.rglob("*")
        if path.is_file() and path.name != _PACKAGE_CHECKSUMS
    }
    if set(checksum_lines) != set(all_non_checksum):
        raise ValueError("SHA256SUMS coverage mismatch")
    for relative, path in all_non_checksum.items():
        if sha256_file(path) != checksum_lines[relative]:
            raise ValueError(f"SHA256SUMS hash mismatch: {relative}")

    validate_markdown_links(output)
    _validate_public_boundary(output)
    return {
        "file_count": len(all_non_checksum) + 1,
        "size_bytes": sum(path.stat().st_size for path in output.rglob("*") if path.is_file()),
    }


def generate_package(
    repo_root: Path,
    source_backup: Path,
    output: Path,
    *,
    event_ids: Sequence[int] = DEFAULT_EVENT_IDS,
    overwrite: bool = False,
) -> dict[str, int]:
    repo_root = Path(repo_root).resolve()
    source_backup = Path(source_backup).resolve()
    output = _validate_output_directory(Path(output), overwrite=overwrite)
    output.mkdir(parents=True, exist_ok=True)

    source_summary, source_summary_sha256 = load_source_summary(source_backup)
    source_entries = _source_event_entries(source_summary)
    source_commit, tag_commit, tag_object = resolve_source_commits(repo_root)
    source_hashes = _source_hashes(repo_root, source_commit)
    if overwrite:
        clear_generated_desktop_outputs(output)

    selected_metadata: list[dict[str, Any]] = []
    for event_id in event_ids:
        event_id = int(event_id)
        source_entry = source_entries.get(event_id)
        if source_entry is None:
            raise ValueError(f"selected event {event_id} is absent from source summary")
        event_path = source_backup / f"event-{event_id:03d}.terp-event"
        metadata = build_event_metadata(
            event_path,
            source_entry,
            source_summary_sha256=source_summary_sha256,
        )
        png_path = output / "desktop" / f"event-{event_id:03d}.png"
        export_replay_png(event_path, png_path)
        metadata["png"] = {
            "relative_path": f"desktop/event-{event_id:03d}.png",
            "size_bytes": png_path.stat().st_size,
            "sha256": sha256_file(png_path),
        }
        _json_dump(output / "desktop" / f"event-{event_id:03d}.json", metadata)
        selected_metadata.append(metadata)

    inventory = tracked_evidence_inventory(repo_root, source_commit=source_commit)
    write_inventory(output / "catalog/evidence-inventory.csv", inventory)
    _write_public_documents(
        repo_root,
        output,
        source_commit=source_commit,
        tag_commit=tag_commit,
        tag_object=tag_object,
        source_summary_sha256=source_summary_sha256,
        source_hashes=source_hashes,
        selected_events=selected_metadata,
    )
    write_manifest(
        output,
        source_commit=source_commit,
        tag_commit=tag_commit,
        tag_object=tag_object,
        source_summary_sha256=source_summary_sha256,
    )
    write_checksums(output)
    return verify_package(output)


__all__ = [
    "CLASS_NAMES",
    "DEFAULT_EVENT_IDS",
    "EXPECTED_SOURCE_SUMMARY_SHA256",
    "build_event_metadata",
    "clear_generated_desktop_outputs",
    "choose_replay_font",
    "export_replay_png",
    "generate_package",
    "load_source_summary",
    "resolve_source_commits",
    "sha256_file",
    "tracked_evidence_inventory",
    "validate_markdown_links",
    "verify_package",
    "write_checksums",
    "write_inventory",
    "write_manifest",
]
